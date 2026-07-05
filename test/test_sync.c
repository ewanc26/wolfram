#include "wolfram/sync.h"
#include "test.h"

#include <stdlib.h>

int main(void) {
    wf_car output = {0};
    WF_CHECK(wf_sync_get_repo(NULL, "did:plc:test", NULL, &output) ==
             WF_ERR_INVALID_ARG);

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

    /* The byte-oriented verifier accepts a sparse CAR and borrows unchanged
     * blocks from the current repository, like atproto SyncStorage. */
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
