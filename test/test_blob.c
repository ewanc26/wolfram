#include "test.h"
#include "wolfram/auth_client.h"
#include "wolfram/xrpc.h"

int main(void) {
    wf_response res = {0};
    const unsigned char blob[] = {0x00, 0x01, 0x02, 0xff};

    wf_xrpc_client *tmp_client = wf_xrpc_client_new("https://example.com");
    WF_CHECK(tmp_client != NULL);
    if (tmp_client) {
        WF_CHECK(wf_xrpc_upload_blob(NULL, "com.atproto.repo.uploadBlob",
                                     blob, sizeof(blob), "application/octet-stream",
                                     &res) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_xrpc_upload_blob(tmp_client, NULL, blob, sizeof(blob),
                                     "application/octet-stream", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_xrpc_upload_blob(tmp_client, "com.atproto.repo.uploadBlob", NULL,
                                     sizeof(blob), "application/octet-stream",
                                     &res) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_xrpc_upload_blob(tmp_client, "com.atproto.repo.uploadBlob", blob,
                                     0, "application/octet-stream", &res)
                 == WF_ERR_INVALID_ARG);
        wf_xrpc_client_free(tmp_client);
    }

    wf_xrpc_client *client = wf_xrpc_client_new("https://example.com");
    WF_CHECK(client != NULL);
    if (client) {
        wf_status status = wf_xrpc_upload_blob(client,
                                               "com.atproto.repo.uploadBlob",
                                               blob, sizeof(blob),
                                               "application/octet-stream",
                                               &res);
        WF_CHECK(status == WF_OK || status == WF_ERR_NETWORK || status == WF_ERR_HTTP);
        WF_CHECK(status != WF_ERR_INVALID_ARG);
    }
    wf_xrpc_client_free(client);
    wf_xrpc_client_free(NULL);

    WF_CHECK(wf_auth_client_upload_blob(NULL, "com.atproto.repo.uploadBlob",
                                        blob, sizeof(blob),
                                        "application/octet-stream", &res)
             == WF_ERR_INVALID_ARG);

    WF_TEST_SUMMARY();
}
