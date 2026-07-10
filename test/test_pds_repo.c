/*
 * test_pds_repo.c — offline tests for the self-hosted PDS repo store.
 *
 * Covers the first coherent PDS slice: a durable, writable repo store
 * backed by SQLite that reuses the SDK's signed-commit / MST machinery,
 * plus the com.atproto.repo XRPC route handlers.
 *
 * Three phases, all offline:
 *   1. Unit (no server): create/put/get/delete/applyWrites + describeRepo,
 *      including persistence across reopen and commit verification with
 *      the store's own signing key.
 *   2. Invariant: the produced head commit verifies with wf_repo_verify.
 *   3. Server round-trip: start an XRPC server, call createRecord /
 *      getRecord / describeRepo over HTTP and assert field-level results.
 */

#include "wolfram/repo_store.h"
#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include "test.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void temp_path(char *buf, size_t n, const char *tag) {
    snprintf(buf, n, "/tmp/wolfram_pds_repo_%s_%lu.db", tag,
             (unsigned long)getpid());
    unlink(buf);
}

/* ── Phase 1 + 2: unit tests (no server) ──────────────────────────── */

static int run_unit(void) {
    int failures = 0;
    char path[256];
    temp_path(path, sizeof(path), "unit");

    wf_repo_store *store = NULL;
    wf_status s = wf_repo_store_open(path, "did:plc:testpds",
                                     "test.example.com", &store);
    WF_CHECK(s == WF_OK && store != NULL);
    if (s != WF_OK) {
        unlink(path);
        return failures + 1;
    }

    /* createRecord in a first collection (rkey auto-generated). */
    char *uri1 = NULL, *cid1 = NULL;
    s = wf_repo_store_create_record(
        store, "com.example.posts", NULL,
        "{\"$type\":\"com.example.posts\",\"text\":\"hello\"}",
        &uri1, &cid1);
    WF_CHECK(s == WF_OK && uri1 && cid1);
    const char *rkey1 = strrchr(uri1, '/') + 1;
    WF_CHECK(strcmp(uri1, "at://did:plc:testpds/com.example.posts/") != 0);
    WF_CHECK(strlen(rkey1) > 0 && strchr(rkey1, '/') == NULL);

    /* getRecord returns the same data + stable CID. */
    char *recj = NULL, *reccid = NULL;
    s = wf_repo_store_get_record(store, "com.example.posts", rkey1,
                                 &recj, &reccid);
    WF_CHECK(s == WF_OK && recj && reccid);
    WF_CHECK(strstr(recj, "\"text\"") != NULL && strstr(recj, "hello") != NULL);
    WF_CHECK(strcmp(reccid, cid1) == 0);
    free(recj);
    free(reccid);

    /* createRecord in a second collection. */
    char *uri2 = NULL, *cid2 = NULL;
    s = wf_repo_store_create_record(
        store, "com.example.likes", NULL,
        "{\"$type\":\"com.example.likes\",\"subject\":\"at://x\"}",
        &uri2, &cid2);
    WF_CHECK(s == WF_OK && uri2 && cid2);
    const char *rkey2 = strrchr(uri2, '/') + 1;

    /* putRecord updates in place; CID must change but rkey is stable. */
    char *uri3 = NULL, *cid3 = NULL;
    s = wf_repo_store_put_record(
        store, "com.example.likes", rkey2,
        "{\"$type\":\"com.example.likes\",\"subject\":\"at://y\",\"extra\":5}",
        &uri3, &cid3);
    WF_CHECK(s == WF_OK && uri3 && cid3);
    WF_CHECK(strcmp(cid3, cid2) != 0);
    WF_CHECK(strcmp(strrchr(uri3, '/') + 1, rkey2) == 0);
    char *recj2 = NULL, *reccid2 = NULL;
    s = wf_repo_store_get_record(store, "com.example.likes", rkey2,
                                 &recj2, &reccid2);
    WF_CHECK(s == WF_OK && recj2 && strstr(recj2, "at://y") != NULL &&
            strstr(recj2, "\"extra\":5") != NULL);
    free(recj2);
    free(reccid2);

    /* deleteRecord removes the record (not found afterwards). */
    s = wf_repo_store_delete_record(store, "com.example.likes", rkey2);
    WF_CHECK(s == WF_OK);
    s = wf_repo_store_get_record(store, "com.example.likes", rkey2,
                                 &recj, &reccid);
    WF_CHECK(s == WF_ERR_NOT_FOUND);
    s = wf_repo_store_delete_record(store, "com.example.likes", "nope");
    WF_CHECK(s == WF_ERR_NOT_FOUND);

    /* applyWrites: create (new rkey) + update rkey1 + delete rkey1. */
    char writes[512];
    snprintf(writes, sizeof(writes),
             "["
             "{\"$type\":\"com.atproto.repo.applyWrites#create\","
             "\"collection\":\"com.example.posts\","
             "\"value\":{\"$type\":\"com.example.posts\",\"text\":\"aa\"}},"
             "{\"$type\":\"com.atproto.repo.applyWrites#update\","
             "\"collection\":\"com.example.posts\",\"rkey\":\"%s\","
             "\"value\":{\"$type\":\"com.example.posts\",\"text\":\"bb\"}},"
             "{\"$type\":\"com.atproto.repo.applyWrites#delete\","
             "\"collection\":\"com.example.posts\",\"rkey\":\"%s\"}"
             "]", rkey1, rkey1);
    char *ccid = NULL, *crev = NULL, *cres = NULL;
    s = wf_repo_store_apply_writes(store, writes, &ccid, &crev, &cres);
    WF_CHECK(s == WF_OK && ccid && crev && cres);
    WF_CHECK(strlen(crev) > 0);
    cJSON *resarr = cJSON_Parse(cres);
    WF_CHECK(resarr && cJSON_IsArray(resarr) && cJSON_GetArraySize(resarr) == 3);
    cJSON_Delete(resarr);
    /* rkey1 must now be gone; a fresh post was created in com.example.posts. */
    s = wf_repo_store_get_record(store, "com.example.posts", rkey1,
                                 &recj, &reccid);
    WF_CHECK(s == WF_ERR_NOT_FOUND);

    /* describeRepo: did + handle + collections + rev. */
    char *desc = NULL;
    s = wf_repo_store_describe(store, &desc);
    WF_CHECK(s == WF_OK && desc);
    cJSON *d = cJSON_Parse(desc);
    WF_CHECK(d != NULL);
    cJSON *did = cJSON_GetObjectItemCaseSensitive(d, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(d, "handle");
    cJSON *cols = cJSON_GetObjectItemCaseSensitive(d, "collections");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(d, "rev");
    WF_CHECK(did && cJSON_IsString(did) &&
             strcmp(did->valuestring, "did:plc:testpds") == 0);
    WF_CHECK(handle && cJSON_IsString(handle) &&
             strcmp(handle->valuestring, "test.example.com") == 0);
    WF_CHECK(cols && cJSON_IsArray(cols));
    int has_posts = 0;
    for (int i = 0; cols && i < cJSON_GetArraySize(cols); i++) {
        cJSON *e = cJSON_GetArrayItem(cols, i);
        if (e && cJSON_IsString(e) &&
            strcmp(e->valuestring, "com.example.posts") == 0)
            has_posts = 1;
    }
    WF_CHECK(has_posts);
    WF_CHECK(rev && cJSON_IsString(rev) && strlen(rev->valuestring) > 0);
    cJSON_Delete(d);
    free(desc);

    /* Phase 2 invariant: the head commit verifies with the store key. */
    int verified = 0;
    wf_commit cm;
    s = wf_repo_store_verify_head(store, &verified, &cm);
    if (s != WF_OK || !verified)
        fprintf(stderr, "DEBUG verify: status=%d verified=%d\n", (int)s, verified);
    WF_CHECK(s == WF_OK && verified == 1);
    WF_CHECK(strcmp(cm.did, "did:plc:testpds") == 0);
    WF_CHECK(cm.sig_len == 64);

    /* Persistence: reopen and the head still verifies, DID preserved. */
    wf_repo_store_free(store);
    store = NULL;
    s = wf_repo_store_open(path, "did:plc:testpds", "test.example.com", &store);
    WF_CHECK(s == WF_OK && store != NULL);
    WF_CHECK(strcmp(wf_repo_store_did(store), "did:plc:testpds") == 0);
    WF_CHECK(strcmp(wf_repo_store_handle(store), "test.example.com") == 0);
    int verified2 = 0;
    s = wf_repo_store_verify_head(store, &verified2, NULL);
    WF_CHECK(s == WF_OK && verified2 == 1);

    free(uri1);
    free(cid1);
    free(uri2);
    free(cid2);
    free(uri3);
    free(cid3);
    free(ccid);
    free(crev);
    free(cres);
    wf_repo_store_free(store);
    unlink(path);
    return failures;
}

/* ── Phase 3: server round-trip ───────────────────────────────────── */

static int run_server(void) {
    int failures = 0;
    char path[256];
    temp_path(path, sizeof(path), "srv");

    wf_repo_store *store = NULL;
    wf_status s = wf_repo_store_open(path, "did:plc:srvpds",
                                     "srv.example.com", &store);
    WF_CHECK(s == WF_OK && store != NULL);
    if (s != WF_OK) {
        unlink(path);
        return failures + 1;
    }

    wf_xrpc_server *server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    WF_CHECK(server != NULL);
    if (!server) {
        wf_repo_store_free(store);
        unlink(path);
        return failures + 1;
    }
    uint16_t port = wf_xrpc_server_port(server);
    WF_CHECK(port != 0);
    WF_CHECK(wf_xrpc_server_register_pds_repo(server, store) == WF_OK);

    char base[64];
    snprintf(base, sizeof(base), "http://127.0.0.1:%u", (unsigned)port);
    wf_xrpc_client *client = wf_xrpc_client_new(base);
    WF_CHECK(client != NULL);

    /* createRecord over HTTP. */
    wf_response res = {0};
    s = wf_xrpc_procedure(
        client, "com.atproto.repo.createRecord",
        "{\"repo\":\"did:plc:srvpds\",\"collection\":\"com.example.posts\","
        "\"record\":{\"$type\":\"com.example.posts\",\"text\":\"viahttp\"}}",
        &res);
    WF_CHECK(s == WF_OK && res.status == 200);
    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    WF_CHECK(root != NULL);
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(root, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
    cJSON *commit = cJSON_GetObjectItemCaseSensitive(root, "commit");
    WF_CHECK(uri && cJSON_IsString(uri) && cid && cJSON_IsString(cid) &&
            commit && cJSON_IsObject(commit));
    const char *uri_str = uri ? uri->valuestring : "";
    const char *sl = strrchr(uri_str, '/');
    char rk_buf[32];
    const char *rk = "";
    if (sl && sl[1]) {
        snprintf(rk_buf, sizeof(rk_buf), "%s", sl + 1);
        rk = rk_buf;
    }
    cJSON_Delete(root);
    wf_response_free(&res);

    /* getRecord over HTTP (GET query). */
    wf_xrpc_param params[] = {
        {"collection", "com.example.posts"},
        {"rkey", (char *)rk},
    };
    s = wf_xrpc_query_params(client, "com.atproto.repo.getRecord", params,
                              2, &res);
    WF_CHECK(s == WF_OK && res.status == 200);
    root = cJSON_ParseWithLength(res.body, res.body_len);
    cJSON *val = root ? cJSON_GetObjectItemCaseSensitive(root, "value") : NULL;
    cJSON *text = val ? cJSON_GetObjectItemCaseSensitive(val, "text") : NULL;
    WF_CHECK(text && cJSON_IsString(text) &&
            strcmp(text->valuestring, "viahttp") == 0);
    cJSON_Delete(root);
    wf_response_free(&res);

    /* describeRepo over HTTP. */
    s = wf_xrpc_query(client, "com.atproto.repo.describeRepo", NULL, &res);
    WF_CHECK(s == WF_OK && res.status == 200);
    root = cJSON_ParseWithLength(res.body, res.body_len);
    cJSON *did = root ? cJSON_GetObjectItemCaseSensitive(root, "did") : NULL;
    WF_CHECK(did && cJSON_IsString(did) &&
             strcmp(did->valuestring, "did:plc:srvpds") == 0);
    cJSON_Delete(root);
    wf_response_free(&res);

    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);
    wf_repo_store_free(store);
    unlink(path);
    return failures;
}

int main(void) {
    int failures = 0;
    failures += run_unit();
    failures += run_server();
    WF_TEST_SUMMARY();
}
