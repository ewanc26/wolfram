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

struct wf_test_request_ctx {
    const char *expected_url;
    const unsigned char *expected_body;
    size_t expected_body_len;
    int called;
};

static wf_status
wf_test_request_handler(void *userdata, const char *method, const char *url,
                        const char *content_type, const char *body,
                        size_t body_len, const wf_http_header *headers,
                        size_t header_count, wf_response *out) {
    (void)headers;
    (void)header_count;
    struct wf_test_request_ctx *ctx = userdata;
    WF_CHECK(strcmp(method, "POST") == 0);
    WF_CHECK(strcmp(url, ctx->expected_url) == 0);
    WF_CHECK(strcmp(content_type, "application/octet-stream") == 0);
    WF_CHECK(body_len == ctx->expected_body_len);
    WF_CHECK(memcmp(body, ctx->expected_body, body_len) == 0);
    ctx->called++;
    out->status = 200;
    return WF_OK;
}

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

/* Test seam handler: captures the request headers for one call. */
struct wf_test_header_capture_ctx {
    wf_http_header headers[8];
    size_t count;
    int called;
};

static wf_status wf_test_header_capture_handler(
    void *userdata, const char *method, const char *url,
    const char *content_type, const char *body, size_t body_len,
    const wf_http_header *headers, size_t header_count, wf_response *out) {
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    struct wf_test_header_capture_ctx *ctx = userdata;
    for (size_t i = 0; i < ctx->count; i++) {
        free((void *)ctx->headers[i].name);
        free((void *)ctx->headers[i].value);
    }
    ctx->count = 0;
    ctx->called++;
    /* The seam frees its header array when the handler returns, so the values
     * must be copied out now rather than borrowed. */
    for (size_t i = 0; i < header_count && ctx->count < 8; i++) {
        ctx->headers[ctx->count].name =
            headers[i].name ? strdup(headers[i].name) : NULL;
        ctx->headers[ctx->count].value =
            headers[i].value ? strdup(headers[i].value) : NULL;
        ctx->count++;
    }
    out->status = 200;
    return WF_OK;
}

static const char *wf_test_find_header(const wf_http_header *headers,
                                       size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (headers[i].name && strcmp(headers[i].name, name) == 0) {
            return headers[i].value;
        }
    }
    return NULL;
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

    /* Binary procedures retain and percent-encode their query parameters. */
    {
        wf_xrpc_client *c = wf_xrpc_client_new("https://video.example");
        const unsigned char body[] = {0x00, 0x7f, 0xff};
        struct wf_test_request_ctx ctx = {
            .expected_url = "https://video.example/xrpc/app.bsky.video."
                            "uploadPart?jobId=job%2Fone&partNumber=2",
            .expected_body = body,
            .expected_body_len = sizeof(body),
        };
        wf_xrpc_set_handler(c, wf_test_request_handler, &ctx);
        const wf_xrpc_param params[] = {
            {"jobId", "job/one"},
            {"partNumber", "2"},
        };
        WF_CHECK(wf_xrpc_upload_blob_params(
                     c, "app.bsky.video.uploadPart", params, 2, body,
                     sizeof(body), "application/octet-stream", &res) == WF_OK);
        WF_CHECK(ctx.called == 1);
        wf_response_free(&res);
        wf_xrpc_client_free(c);
    }

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

    /* An authenticated request rejected with 401/ExpiredToken/InvalidToken
     * and no refresh path surfaces as WF_ERR_AUTH, not WF_ERR_HTTP, so
     * callers can fail fast on dead credentials. */
    {
        wf_xrpc_client *c = wf_xrpc_client_new("https://jetstream.example");
        WF_CHECK(c != NULL);
        wf_xrpc_client_set_auth(c, "archive-token");

        struct wf_test_err_ctx ctx = {.status = 401,
                                      .body = "{\"error\":\"InvalidToken\"}"};
        wf_xrpc_set_handler(c, wf_test_error_handler, &ctx);
        wf_response res = {0};
        WF_CHECK(wf_xrpc_query(c, "network.bsky.jetstream.getBlock", NULL,
                               &res) == WF_ERR_AUTH);
        wf_response_free(&res);
        const char *le = wf_xrpc_last_error(c);
        WF_CHECK(le != NULL); /* the rejection message is recorded */

        /* ExpiredToken via a 200-less envelope on a non-401 status maps the
         * same way: the envelope, not the HTTP status, drives the decision. */
        struct wf_test_err_ctx expired = {
            .status = 400, .body = "{\"error\":\"ExpiredToken\"}"};
        wf_xrpc_set_handler(c, wf_test_error_handler, &expired);
        WF_CHECK(wf_xrpc_query(c, "network.bsky.jetstream.getBlock", NULL,
                               &res) == WF_ERR_AUTH);
        wf_response_free(&res);

        /* An unauthenticated request rejected with 401 stays WF_ERR_HTTP:
         * there were no credentials to be rejected. */
        wf_xrpc_client_set_auth(c, NULL);
        struct wf_test_err_ctx unauth = {
            .status = 401, .body = "{\"error\":\"ExpiredToken\"}"};
        wf_xrpc_set_handler(c, wf_test_error_handler, &unauth);
        WF_CHECK(wf_xrpc_query(c, "network.bsky.jetstream.getBlock", NULL,
                               &res) == WF_ERR_HTTP);
        wf_response_free(&res);

        /* A different 4xx stays WF_ERR_HTTP. */
        wf_xrpc_client_set_auth(c, "archive-token");
        struct wf_test_err_ctx other = {
            .status = 400, .body = "{\"error\":\"InvalidRequest\"}"};
        wf_xrpc_set_handler(c, wf_test_error_handler, &other);
        WF_CHECK(wf_xrpc_query(c, "network.bsky.jetstream.getBlock", NULL,
                               &res) == WF_ERR_HTTP);
        wf_response_free(&res);

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

    /* A client-level atproto-proxy header is sent on every request and can be
     * cleared again. */
    {
        wf_xrpc_client *c = wf_xrpc_client_new("https://bsky.social");
        WF_CHECK(c != NULL);
        const char *chat_proxy = "did:web:api.bsky.chat#bsky_chat";
        WF_CHECK(wf_xrpc_client_set_proxy(c, chat_proxy) == WF_OK);
        WF_CHECK(wf_xrpc_client_set_proxy(NULL, chat_proxy) ==
                 WF_ERR_INVALID_ARG);

        struct wf_test_header_capture_ctx ctx = {0};
        wf_xrpc_set_handler(c, wf_test_header_capture_handler, &ctx);
        wf_response res = {0};
        WF_CHECK(wf_xrpc_query(c, "chat.bsky.convo.getConvo", NULL, &res) ==
                 WF_OK);
        wf_response_free(&res);
        WF_CHECK(ctx.called == 1);
        const char *proxy =
            wf_test_find_header(ctx.headers, ctx.count, "atproto-proxy");
        WF_CHECK(proxy != NULL && strcmp(proxy, chat_proxy) == 0);
        WF_CHECK(wf_test_find_header(ctx.headers, ctx.count, "Authorization") ==
                 NULL);

        /* A bearer token rides alongside the proxy header. */
        wf_xrpc_client_set_auth(c, "jwt");
        ctx.called = 0;
        WF_CHECK(wf_xrpc_query(c, "chat.bsky.convo.getConvo", NULL, &res) ==
                 WF_OK);
        wf_response_free(&res);
        WF_CHECK(
            wf_test_find_header(ctx.headers, ctx.count, "Authorization") &&
            strcmp(wf_test_find_header(ctx.headers, ctx.count, "Authorization"),
                   "Bearer jwt") == 0);
        WF_CHECK(
            strcmp(wf_test_find_header(ctx.headers, ctx.count, "atproto-proxy"),
                   chat_proxy) == 0);

        /* A procedure carries both headers plus Content-Type. */
        ctx.called = 0;
        WF_CHECK(wf_xrpc_procedure(c, "chat.bsky.convo.sendMessage",
                                   "{\"convoId\":\"c\"}", &res) == WF_OK);
        wf_response_free(&res);
        WF_CHECK(
            strcmp(wf_test_find_header(ctx.headers, ctx.count, "atproto-proxy"),
                   chat_proxy) == 0);
        WF_CHECK(
            strcmp(wf_test_find_header(ctx.headers, ctx.count, "Content-Type"),
                   "application/json") == 0);

        /* Clearing the proxy removes the header from subsequent requests. */
        WF_CHECK(wf_xrpc_client_set_proxy(c, NULL) == WF_OK);
        ctx.called = 0;
        WF_CHECK(wf_xrpc_query(c, "chat.bsky.convo.getConvo", NULL, &res) ==
                 WF_OK);
        wf_response_free(&res);
        WF_CHECK(wf_test_find_header(ctx.headers, ctx.count, "atproto-proxy") ==
                 NULL);

        wf_xrpc_set_handler(c, NULL, NULL);
        wf_xrpc_client_free(c);
    }

    WF_TEST_SUMMARY();
}
