#include "wolfram/sync_verify.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/xrpc.h"
#include "wolfram/crypto.h"
#include "wolfram/repo.h"
#include "test.h"

#include <string.h>
#include <stdlib.h>

static wf_xrpc_client *test_client(void) {
    return wf_xrpc_client_new("https://example.com");
}

/* Helper: build a minimal CAR with a single signed commit block (no MST). */
static int build_commit_car(const char *did, const char *rev,
                            unsigned char **out_car, size_t *out_len,
                            wf_cid *out_commit_cid) {
    wf_signing_key key;
    memset(&key, 0, sizeof(key));
    wf_status s = wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key);
    if (s != WF_OK) return 0;

    wf_cid mst_root = {{0}, 0};
    mst_root.bytes[0] = 0x01; mst_root.bytes[1] = 0x71;
    mst_root.bytes[2] = 0x12; mst_root.bytes[3] = 0x20;
    mst_root.len = 36;

    wf_car car;
    memset(&car, 0, sizeof(car));
    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    s = wf_commit_create(did, rev, &mst_root, NULL, &key, &car, &commit);
    if (s != WF_OK) { wf_car_free(&car); return 0; }

    s = wf_car_write(&car, out_car, out_len);
    if (s != WF_OK) { wf_car_free(&car); return 0; }

    *out_commit_cid = commit.cid;
    wf_car_free(&car);
    return 1;
}

/* ── argument validation ── */

static void test_verify_null_args(void) {
    wf_xrpc_client *client = test_client();
    int verified = 0;
    wf_commit commit;
    WF_CHECK(wf_sync_verify_commit(NULL, client, &verified, &commit) == WF_ERR_INVALID_ARG);
    {
        wf_subscribe_commit c = {0};
        WF_CHECK(wf_sync_verify_commit(&c, NULL, &verified, &commit) == WF_ERR_INVALID_ARG);
    }
    {
        wf_subscribe_commit c = {0};
        WF_CHECK(wf_sync_verify_commit(&c, client, NULL, &commit) == WF_ERR_INVALID_ARG);
    }
    {
        wf_subscribe_commit c = {0};
        WF_CHECK(wf_sync_verify_commit(&c, client, &verified, NULL) == WF_ERR_INVALID_ARG);
    }
    wf_xrpc_client_free(client);
}

static void test_verify_empty_blocks(void) {
    wf_xrpc_client *client = test_client();
    int verified = 0;
    wf_commit commit;
    wf_subscribe_commit c = {0};
    c.did[0] = 'd'; c.did[1] = '\0';
    WF_CHECK(wf_sync_verify_commit(&c, client, &verified, &commit) == WF_ERR_INVALID_ARG);
    wf_xrpc_client_free(client);
}

static void test_verify_outputs_zeroed_on_failure(void) {
    wf_xrpc_client *client = test_client();
    int verified = 1;
    wf_commit commit;
    memset(&commit, 0xAA, sizeof(commit));
    wf_subscribe_commit c = {0};
    WF_CHECK(wf_sync_verify_commit(&c, client, &verified, &commit) == WF_ERR_INVALID_ARG);
    WF_CHECK(verified == 0);
    WF_CHECK(commit.sig_len == 0);
    wf_xrpc_client_free(client);
}

/* ── CAR parse + DID fail path ── */

static void test_verify_valid_car_unresolvable_did(void) {
    unsigned char *car_bytes = NULL;
    size_t car_len = 0;
    wf_cid commit_cid = {{0}, 0};

    if (!build_commit_car("did:plc:nonexistent00000000000000",
                          "3abcdefghijklmnop",
                          &car_bytes, &car_len, &commit_cid)) {
        /* Key generation not available (no libsecp256k1) — skip gracefully */
        return;
    }

    wf_xrpc_client *client = test_client();
    wf_subscribe_commit c;
    memset(&c, 0, sizeof(c));
    memcpy(c.did, "did:plc:nonexistent00000000000000", 33);
    c.commit_cid = commit_cid;
    c.blocks = car_bytes;
    c.blocks_len = car_len;

    int verified = 1;
    wf_commit parsed;
    memset(&parsed, 0, sizeof(parsed));
    wf_status status = wf_sync_verify_commit(&c, client, &verified, &parsed);

    /* CAR parsing succeeds, DID resolution fails (no network / no PLC record) */
    WF_CHECK(status != WF_OK);
    WF_CHECK(verified == 0);

    wf_xrpc_client_free(client);
    free(car_bytes);
}

/* ── safety ── */

static void test_verify_with_client_no_network(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1:0");
    if (!client) return;
    int verified = 0;
    wf_commit commit;
    wf_subscribe_commit c = {0};
    c.did[0] = 'd'; c.did[1] = '\0';
    c.blocks = (unsigned char*)"";
    c.blocks_len = 0;
    WF_CHECK(wf_sync_verify_commit(&c, client, &verified, &commit) == WF_ERR_INVALID_ARG);
    wf_xrpc_client_free(client);
}

int main(void) {
    test_verify_null_args();
    test_verify_empty_blocks();
    test_verify_outputs_zeroed_on_failure();
    test_verify_valid_car_unresolvable_did();
    test_verify_with_client_no_network();
    WF_TEST_SUMMARY();
}
