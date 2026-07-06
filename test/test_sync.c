#include "wolfram/sync.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static void test_sync_get_blob_invalid(void) {
    unsigned char *data = NULL;
    size_t len = 0;

    WF_CHECK(wf_sync_get_blob(NULL, "did:plc:test", "bafkreig24b7ydl37jltqjfpirwe7j3xcts7qum7u5er7jlx3awz6ia7fqy", &data, &len) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(data == NULL);
    WF_CHECK(len == 0);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_get_blob(client, NULL, "cid", &data, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blob(client, "", "cid", &data, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blob(client, "did:plc:test", NULL, &data, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blob(client, "did:plc:test", "", &data, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blob(client, "did:plc:test", "cid", NULL, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blob(client, "did:plc:test", "cid", &data, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_get_blocks_invalid(void) {
    wf_car out = {0};
    const char *cids[] = {"bafkreig24b7ydl37jltqjfpirwe7j3xcts7qum7u5er7jlx3awz6ia7fqy"};

    WF_CHECK(wf_sync_get_blocks(NULL, "did:plc:test", cids, 1, &out) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_get_blocks(client, NULL, cids, 1, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blocks(client, "", cids, 1, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blocks(client, "did:plc:test", NULL, 1, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blocks(client, "did:plc:test", cids, 0, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_blocks(client, "did:plc:test", cids, 1, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_get_record_invalid(void) {
    wf_car out = {0};

    WF_CHECK(wf_sync_get_record(NULL, "did:plc:test", "app.bsky.feed.post", "x", &out) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_get_record(client, NULL, "app.bsky.feed.post", "x", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_record(client, "", "app.bsky.feed.post", "x", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_record(client, "did:plc:test", NULL, "x", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_record(client, "did:plc:test", "", "x", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_record(client, "did:plc:test", "app.bsky.feed.post", NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_record(client, "did:plc:test", "app.bsky.feed.post", "", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_record(client, "did:plc:test", "app.bsky.feed.post", "x", NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_list_blobs_invalid(void) {
    wf_sync_blob_list list = {0};

    WF_CHECK(wf_sync_list_blobs(NULL, "did:plc:test", NULL, 0, NULL, &list) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_list_blobs(client, NULL, NULL, 0, NULL, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_blobs(client, "", NULL, 0, NULL, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_blobs(client, "did:plc:test", "", 0, NULL, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_blobs(client, "did:plc:test", NULL, -1, NULL, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_blobs(client, "did:plc:test", NULL, 1001, NULL, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_blobs(client, "did:plc:test", NULL, 0, "", &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_blobs(client, "did:plc:test", NULL, 0, NULL, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_list_blobs_free_empty(void) {
    wf_sync_blob_list list = {0};

    wf_sync_blob_list_free(&list);
    wf_sync_blob_list_free(NULL);
}

static void test_sync_get_head_invalid(void) {
    wf_sync_head head = {0};

    WF_CHECK(wf_sync_get_head(NULL, "did:plc:test", &head) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_get_head(client, NULL, &head) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_head(client, "", &head) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_head(client, "did:plc:test", NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_get_head_free_empty(void) {
    wf_sync_head head = {0};

    wf_sync_head_free(&head);
    wf_sync_head_free(NULL);
}

static void test_sync_get_latest_commit_invalid(void) {
    wf_sync_commit_info info = {0};

    WF_CHECK(wf_sync_get_latest_commit(NULL, "did:plc:test", &info) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_get_latest_commit(client, NULL, &info) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_latest_commit(client, "", &info) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_latest_commit(client, "did:plc:test", NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_commit_info_free_empty(void) {
    wf_sync_commit_info info = {0};

    wf_sync_commit_info_free(&info);
    wf_sync_commit_info_free(NULL);
}

static void test_sync_get_repo_status_invalid(void) {
    wf_sync_repo_status status = {0};

    WF_CHECK(wf_sync_get_repo_status(NULL, "did:plc:test", &status) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_get_repo_status(client, NULL, &status) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_repo_status(client, "", &status) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_get_repo_status(client, "did:plc:test", NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_repo_status_free_empty(void) {
    wf_sync_repo_status status = {0};

    wf_sync_repo_status_free(&status);
    wf_sync_repo_status_free(NULL);
}

static void test_sync_list_repos_invalid(void) {
    wf_sync_repo_list list = {0};

    WF_CHECK(wf_sync_list_repos(NULL, NULL, 0, &list) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);

    WF_CHECK(wf_sync_list_repos(client, NULL, -1, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_repos(client, NULL, 1001, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_repos(client, "", 0, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_sync_list_repos(client, NULL, 0, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_sync_repo_list_free_empty(void) {
    wf_sync_repo_list list = {0};

    wf_sync_repo_list_free(&list);
    wf_sync_repo_list_free(NULL);
}

int main(void) {
    test_sync_get_blob_invalid();
    test_sync_get_blocks_invalid();
    test_sync_get_record_invalid();
    test_sync_list_blobs_invalid();
    test_sync_list_blobs_free_empty();
    test_sync_get_head_invalid();
    test_sync_get_head_free_empty();
    test_sync_get_latest_commit_invalid();
    test_sync_commit_info_free_empty();
    test_sync_get_repo_status_invalid();
    test_sync_repo_status_free_empty();
    test_sync_list_repos_invalid();
    test_sync_repo_list_free_empty();

    {
        wf_car output = {0};
        wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
        WF_CHECK(client != NULL);
        WF_CHECK(wf_sync_get_repo(client, NULL, NULL, &output) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_get_repo(client, "", NULL, &output) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_get_repo(client, "did:plc:test", "", &output) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_get_repo(client, "did:plc:test", NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_xrpc_client_free(client);
    }

    {
        wf_signing_key key = {0};
        key.type = WF_KEY_TYPE_P256;
        key.bytes[31] = 1;
        const char *did = "did:plc:syncdifftest";
        const char *did_key =
            "did:key:zDnaepsL7AXenJkVYdkh5KuKsSU7Ykh7kyXaLLU7auN9FWSiZ";
        unsigned char record[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x01};
        wf_car base = {0};
        wf_cid commit = {0}, record_cid = {0};
        WF_CHECK(wf_repo_create_record(&base, NULL, did,
                                        "com.example.posts", "one",
                                        record, sizeof(record), &key, &commit,
                                        &record_cid) == WF_OK);
        base.roots = malloc(sizeof(wf_cid));
        WF_CHECK(base.roots != NULL);
        base.roots[0] = commit;
        base.root_count = 1;

        wf_car sparse = {0};
        sparse.roots = &commit;
        sparse.root_count = 1;
        unsigned char *bytes = NULL;
        size_t len = 0;
        WF_CHECK(wf_car_write(&sparse, &bytes, &len) == WF_OK);
        wf_repo_verify_options options = {did, did_key, NULL};
        wf_repo_diff diff = {0};
        WF_CHECK(wf_sync_verify_diff_car(&base, &commit, bytes, len, &options,
                                          &diff) == WF_OK);
        WF_CHECK(diff.operation_count == 0);
        WF_CHECK(diff.new_blocks.block_count == 0);
        WF_CHECK(diff.removed_count == 0);
        WF_CHECK(wf_repo_diff_apply(&base, &diff) == WF_OK);
        wf_repo_diff_free(&diff);

        WF_CHECK(wf_sync_verify_diff_car(NULL, &commit, bytes, len, &options,
                                          &diff) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_verify_diff_car(&base, &commit, NULL, len, &options,
                                          &diff) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_verify_diff_car(&base, &commit,
                                          (const unsigned char *)"bad", 3,
                                          &options, &diff) != WF_OK);
        free(bytes);
        wf_car_free(&base);
    }

    WF_TEST_SUMMARY();
}
