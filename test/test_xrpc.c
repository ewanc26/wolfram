/**
 * test_xrpc.c — unit tests for the parts of xrpc.c that don't need
 * the network: client construction, base URL normalisation, and auth
 * header handling. Live-network tests belong somewhere separate
 * (they're slow, flaky, and not what "unit test" should mean here).
 */

#include "wolfram/xrpc.h"
#include "test.h"

int main(void) {
    /* Rejects empty/NULL base URLs. */
    WF_CHECK(wf_xrpc_client_new(NULL) == NULL);
    WF_CHECK(wf_xrpc_client_new("") == NULL);

    /* Accepts a normal URL and doesn't crash on free. */
    wf_xrpc_client *client = wf_xrpc_client_new("https://eurosky.social");
    WF_CHECK(client != NULL);

    /* Setting and clearing auth shouldn't crash either. */
    wf_xrpc_client_set_auth(client, "fake.jwt.token");
    wf_xrpc_client_set_auth(client, NULL);

    wf_xrpc_client_free(client);
    wf_xrpc_client_free(NULL); /* must be safe */

    /* A trailing-slash base URL should still produce a client. */
    wf_xrpc_client *trailing = wf_xrpc_client_new("https://eurosky.social/");
    WF_CHECK(trailing != NULL);

    /* Bad arguments to query/procedure are rejected without a client. */
    wf_response res = {0};
    WF_CHECK(wf_xrpc_query(NULL, "com.atproto.repo.describeRepo", NULL, &res) == WF_ERR_INVALID_ARG);

    wf_xrpc_param param = {"did", "did:plc:test"};
    WF_CHECK(wf_xrpc_query_params(NULL, "com.atproto.sync.getRepo",
                                  &param, 1, &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_xrpc_query_params(trailing, "com.atproto.sync.getRepo",
                                  NULL, 1, &res) == WF_ERR_INVALID_ARG);
    wf_xrpc_client_free(trailing);

    WF_TEST_SUMMARY();
}
