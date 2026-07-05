#include "wolfram/sync.h"
#include "test.h"

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

    WF_TEST_SUMMARY();
}
