/**
 * test_xrpc.c — unit tests for the parts of xrpc.c that don't need
 * the network: client construction, base URL normalisation, and auth
 * header handling. Live-network tests belong somewhere separate
 * (they're slow, flaky, and not what "unit test" should mean here).
 */

#include <stdlib.h>
#include <string.h>

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

    /* XRPC error-envelope decoding from a non-OK response body. */
    {
        const char *env = "{\"error\":\"RateLimitExceeded\","
                          "\"message\":\"rate limited, retry later\"}";
        wf_response r = {.status = 429, .body = (char *)env,
                         .body_len = strlen(env)};
        char *err = NULL, *msg = NULL;
        WF_CHECK(wf_xrpc_error(&r, &err, &msg) == WF_OK);
        WF_CHECK(err && strcmp(err, "RateLimitExceeded") == 0);
        WF_CHECK(msg && strcmp(msg, "rate limited, retry later") == 0);
        free(err);
        free(msg);
    }

    /* A non-envelope body yields not-found (no `error` field). */
    {
        const char *plain = "{\"did\":\"did:plc:abc\",\"handle\":\"a.b\"}";
        wf_response r = {.status = 400, .body = (char *)plain,
                         .body_len = strlen(plain)};
        char *err = NULL, *msg = NULL;
        WF_CHECK(wf_xrpc_error(&r, &err, &msg) == WF_ERR_NOT_FOUND);
        WF_CHECK(err == NULL && msg == NULL);
    }

    /* An envelope with only `error` (no `message`) still decodes. */
    {
        const char *only_err = "{\"error\":\"InvalidToken\"}";
        wf_response r = {.status = 401, .body = (char *)only_err,
                         .body_len = strlen(only_err)};
        char *err = NULL;
        WF_CHECK(wf_xrpc_error(&r, &err, NULL) == WF_OK);
        WF_CHECK(err && strcmp(err, "InvalidToken") == 0);
        free(err);
    }

    WF_TEST_SUMMARY();
}

