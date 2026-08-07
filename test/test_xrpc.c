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

/* Stand-in application RNG. Never called here — no handshake happens in a
 * unit test — it only needs a valid wf_tls_rng_fn address to install. */
static int wf_test_tls_rng(void *userdata, unsigned char *output, size_t len) {
    (void)userdata;
    memset(output, 0x5A, len);
    return 0;
}

/* Test seam handler: fails every request with the configured HTTP status and
 * body, so the client's error capture can be exercised without a network. */
struct wf_test_err_ctx {
    int status;
    const char *body;
};

static wf_status wf_test_error_handler(void *userdata, const char *method,
                                       const char *url,
                                       const char *content_type,
                                       const char *body, size_t body_len,
                                       const wf_http_header *headers,
                                       size_t header_count, wf_response *out) {
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;
    const struct wf_test_err_ctx *ctx =
        (const struct wf_test_err_ctx *)userdata;
    out->status = ctx->status;
    if (ctx->body) {
        out->body = strdup(ctx->body);
        out->body_len = strlen(ctx->body);
    }
    return (ctx->status >= 200 && ctx->status < 300) ? WF_OK : WF_ERR_HTTP;
}

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
    WF_CHECK(wf_xrpc_query(NULL, "com.atproto.repo.describeRepo", NULL, &res) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_param param = {"did", "did:plc:test"};
    WF_CHECK(wf_xrpc_query_params(NULL, "com.atproto.sync.getRepo", &param, 1,
                                  &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_xrpc_query_params(trailing, "com.atproto.sync.getRepo", NULL, 1,
                                  &res) == WF_ERR_INVALID_ARG);
    wf_xrpc_client_free(trailing);

    /* XRPC error-envelope decoding from a non-OK response body. */
    {
        const char *env = "{\"error\":\"RateLimitExceeded\","
                          "\"message\":\"rate limited, retry later\"}";
        wf_response r = {
            .status = 429, .body = (char *)env, .body_len = strlen(env)};
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
        wf_response r = {
            .status = 400, .body = (char *)plain, .body_len = strlen(plain)};
        char *err = NULL, *msg = NULL;
        WF_CHECK(wf_xrpc_error(&r, &err, &msg) == WF_ERR_NOT_FOUND);
        WF_CHECK(err == NULL && msg == NULL);
    }

    /* An envelope with only `error` (no `message`) still decodes. */
    {
        const char *only_err = "{\"error\":\"InvalidToken\"}";
        wf_response r = {.status = 401,
                         .body = (char *)only_err,
                         .body_len = strlen(only_err)};
        char *err = NULL;
        WF_CHECK(wf_xrpc_error(&r, &err, NULL) == WF_OK);
        WF_CHECK(err && strcmp(err, "InvalidToken") == 0);
        free(err);
    }

    /* The error envelope of a non-2xx response is recorded on the client and
     * exposed via wf_xrpc_last_error; a later successful request clears it. */
    {
        wf_xrpc_client *c = wf_xrpc_client_new("https://eurosky.social");
        WF_CHECK(c != NULL);
        WF_CHECK(wf_xrpc_last_error(c) == NULL);

        struct wf_test_err_ctx ctx = {.status = 400,
                                      .body =
                                          "{\"error\":\"InvalidRecord\","
                                          "\"message\":\"text is too long\"}"};
        wf_xrpc_set_handler(c, wf_test_error_handler, &ctx);

        wf_response res = {0};
        WF_CHECK(wf_xrpc_query(c, "com.atproto.repo.createRecord", NULL,
                               &res) == WF_ERR_HTTP);
        wf_response_free(&res);
        const char *le = wf_xrpc_last_error(c);
        WF_CHECK(le && strcmp(le, "text is too long") == 0);

        /* A non-envelope failure body yields no message. */
        struct wf_test_err_ctx plain = {.status = 400,
                                        .body = "{\"did\":\"x\"}"};
        wf_xrpc_set_handler(c, wf_test_error_handler, &plain);
        WF_CHECK(wf_xrpc_query(c, "com.atproto.repo.describeRepo", NULL,
                               &res) == WF_ERR_HTTP);
        wf_response_free(&res);
        WF_CHECK(wf_xrpc_last_error(c) == NULL);

        /* Success clears the recorded error. */
        struct wf_test_err_ctx ok = {.status = 200, .body = "{\"ok\":true}"};
        wf_xrpc_set_handler(c, wf_test_error_handler, &ok);
        WF_CHECK(wf_xrpc_query(c, "com.atproto.server.describeServer", NULL,
                               &res) == WF_OK);
        wf_response_free(&res);
        WF_CHECK(wf_xrpc_last_error(c) == NULL);

        wf_xrpc_set_handler(c, NULL, NULL);
        wf_xrpc_client_free(c);
    }

    /*
     * Application TLS RNG. Whether one can be installed depends on the linked
     * libcurl's backend, which differs between a desktop build (usually
     * OpenSSL) and the Wii U one (mbedTLS), so the expected outcome is taken
     * from wf_xrpc_tls_rng_supported() rather than hardcoded. What must hold
     * everywhere is that the two cases stay distinguishable: a build that
     * cannot honour the RNG has to say so, not silently accept one it will
     * never call.
     */
    {
        wf_xrpc_client *c = wf_xrpc_client_new("https://example.com");
        WF_CHECK(c != NULL);

        WF_CHECK(wf_xrpc_client_set_tls_rng(NULL, wf_test_tls_rng, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_status installed = wf_xrpc_client_set_tls_rng(c, wf_test_tls_rng, c);
        if (wf_xrpc_tls_rng_supported()) {
            WF_CHECK(installed == WF_OK);
        } else {
            WF_CHECK(installed == WF_ERR_UNSUPPORTED);
        }

        /* Clearing restores libcurl's own RNG and is valid everywhere,
         * including on builds that cannot install one. */
        WF_CHECK(wf_xrpc_client_set_tls_rng(c, NULL, NULL) == WF_OK);

        wf_xrpc_client_free(c);
    }

    WF_TEST_SUMMARY();
}
