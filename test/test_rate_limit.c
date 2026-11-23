/*
 * test_rate_limit.c -- RateLimit header parsing and the bounded retry
 * helpers. No network: the client readback case uses the XRPC test seam.
 */

#include <stdlib.h>
#include <string.h>

#include "wolfram/rate_limit.h"
#include "wolfram/retry.h"
#include "wolfram/xrpc.h"
#include "test.h"

struct budget_handler_ctx {
    wf_rate_limit budget;
    int called;
};

static wf_status budget_handler(void *userdata, const char *method,
                                const char *url, const char *content_type,
                                const char *body, size_t body_len,
                                const wf_http_header *headers,
                                size_t header_count, wf_response *out) {
    struct budget_handler_ctx *ctx = (struct budget_handler_ctx *)userdata;
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;
    ctx->called++;
    out->status = 200;
    out->body = strdup("{}");
    out->body_len = out->body ? 2 : 0;
    out->rate_limit = ctx->budget;
    return WF_OK;
}

static void test_parser(void) {
    wf_rate_limit b;

    memset(&b, 0, sizeof(b));
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Limit", "100") == 1);
    WF_CHECK((b.present & WF_RATE_LIMIT_HAS_LIMIT) != 0);
    WF_CHECK(b.limit == 100);

    WF_CHECK(wf_rate_limit_parse_header(&b, "ratelimit-remaining", " 42 ") ==
             1);
    WF_CHECK((b.present & WF_RATE_LIMIT_HAS_REMAINING) != 0);
    WF_CHECK(b.remaining == 42);

    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Reset", "18") == 1);
    WF_CHECK((b.present & WF_RATE_LIMIT_HAS_RESET) != 0);
    WF_CHECK(b.reset == 18);

    /* Current structured policy. */
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Policy",
                                        "\"default\";q=100;w=60") == 1);
    WF_CHECK((b.present & WF_RATE_LIMIT_HAS_POLICY) != 0);
    WF_CHECK(b.policy_quota == 100 && b.policy_window == 60);

    /* Legacy bare quota. */
    memset(&b, 0, sizeof(b));
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Policy", "200;w=900") ==
             1);
    WF_CHECK(b.policy_quota == 200 && b.policy_window == 900);

    /* Only the first policy item is reported. */
    memset(&b, 0, sizeof(b));
    WF_CHECK(wf_rate_limit_parse_header(
                 &b, "RateLimit-Policy",
                 "\"burst\";q=50;w=60,\"daily\";q=1000;w=3600") == 1);
    WF_CHECK(b.policy_quota == 50 && b.policy_window == 60);

    /* Unknown parameters (qu, pk) are skipped, not treated as malformed. */
    memset(&b, 0, sizeof(b));
    WF_CHECK(wf_rate_limit_parse_header(
                 &b, "RateLimit-Policy",
                 "\"peruser\";q=65535;qu=\"content-bytes\";w=10;pk=:abc==:") ==
             1);
    WF_CHECK(b.policy_quota == 65535 && b.policy_window == 10);

    /* Malformed values leave the field untouched and report no parse. */
    memset(&b, 0, sizeof(b));
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Limit", "12x") == 0);
    WF_CHECK((b.present & WF_RATE_LIMIT_HAS_LIMIT) == 0);
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Limit", "-1") == 0);
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Limit", "") == 0);
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Limit",
                                        "999999999999999999999999999") == 0);
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Policy", "q=nope") == 0);
    WF_CHECK((b.present & WF_RATE_LIMIT_HAS_POLICY) == 0);
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Limit", "5") == 1);
    WF_CHECK(wf_rate_limit_parse_header(&b, "RateLimit-Limit", "oops") == 0);
    WF_CHECK(b.limit == 5);
    WF_CHECK(wf_rate_limit_parse_header(&b, "X-Other", "5") == 0);
    WF_CHECK(wf_rate_limit_parse_header(NULL, "RateLimit-Limit", "5") == 0);
}

static void test_retry(void) {
    wf_retry_policy p = {4, 100, 1000, 0};
    wf_retry_policy j = {5, 100, 1000, 25};

    WF_CHECK(wf_retry_should_retry(WF_FAIL_NETWORK, 1, &p) == 1);
    WF_CHECK(wf_retry_should_retry(WF_FAIL_RATE_LIMIT, 3, &p) == 1);
    WF_CHECK(wf_retry_should_retry(WF_FAIL_RATE_LIMIT, 4, &p) == 0);
    WF_CHECK(wf_retry_should_retry(WF_FAIL_BAD_CREDENTIALS, 1, &p) == 0);
    WF_CHECK(wf_retry_should_retry(WF_FAIL_TLS, 1, &p) == 0);
    WF_CHECK(wf_retry_should_retry(WF_FAIL_NONE, 1, &p) == 0);
    WF_CHECK(wf_retry_should_retry(WF_FAIL_NETWORK, 1, NULL) == 0);
    WF_CHECK(wf_retry_should_retry(WF_FAIL_NETWORK, 1,
                                   &(wf_retry_policy){1, 100, 1000, 0}) == 0);

    /* A 429 is the existing rate-limit failure kind, so it retries. */
    WF_CHECK(wf_failure_classify(WF_ERR_HTTP, 429, NULL) == WF_FAIL_RATE_LIMIT);
    WF_CHECK(wf_retry_should_retry(wf_failure_classify(WF_ERR_HTTP, 429, NULL),
                                   1, &p) == 1);

    WF_CHECK(wf_retry_delay_ms(&p, 1, 0, 0) == 100);
    WF_CHECK(wf_retry_delay_ms(&p, 2, 0, 0) == 200);
    WF_CHECK(wf_retry_delay_ms(&p, 3, 0, 0) == 400);
    WF_CHECK(wf_retry_delay_ms(&p, 4, 0, 0) == 800);
    WF_CHECK(wf_retry_delay_ms(&p, 5, 0, 0) == 1000); /* capped */

    /* The server's Reset window is a floor, even beyond the cap. */
    WF_CHECK(wf_retry_delay_ms(&p, 1, 1500, 0) == 1500);

    /* Jitter is deterministic, only adds, and stays under the cap. */
    WF_CHECK(wf_retry_delay_ms(&j, 1, 0, 7) == 107);
    WF_CHECK(wf_retry_delay_ms(&j, 1, 0, 7) == wf_retry_delay_ms(&j, 1, 0, 7));
    WF_CHECK(wf_retry_delay_ms(&j, 1, 0, 8) == 108);
    WF_CHECK(wf_retry_delay_ms(&j, 5, 0, 7) == 1000);
    WF_CHECK(wf_retry_delay_ms(NULL, 1, 0, 0) == 0);
    WF_CHECK(wf_retry_delay_ms(&(wf_retry_policy){4, 0, 1000, 0}, 1, 0, 0) ==
             0);
}

static void test_async_readback_is_safe(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("https://example.test");
    struct budget_handler_ctx ctx;
    wf_xrpc_pending *pending = NULL;
    wf_response res;

    WF_CHECK(client != NULL);
    memset(&ctx, 0, sizeof(ctx));
    memset(&res, 0, sizeof(res));
    ctx.budget.present = WF_RATE_LIMIT_HAS_REMAINING;
    ctx.budget.remaining = 3;
    wf_xrpc_set_handler(client, budget_handler, &ctx);

    WF_CHECK(wf_xrpc_query_async(client, "app.bsky.feed.getTimeline", NULL,
                                 &pending) == WF_OK);
    /* The worker must not write back to a client freed before the handle. */
    wf_xrpc_client_free(client);

    WF_CHECK(wf_xrpc_pending_wait(pending) == WF_OK);
    WF_CHECK(wf_xrpc_pending_result(pending, &res) == WF_OK);
    WF_CHECK(res.rate_limit.remaining == 3);
    wf_response_free(&res);
    wf_xrpc_pending_free(pending);
}

static void test_client_readback(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("https://example.test");
    struct budget_handler_ctx ctx;
    wf_response res;
    wf_rate_limit got;

    WF_CHECK(client != NULL);
    memset(&ctx, 0, sizeof(ctx));
    memset(&res, 0, sizeof(res));
    ctx.budget.present = WF_RATE_LIMIT_HAS_LIMIT | WF_RATE_LIMIT_HAS_REMAINING |
                         WF_RATE_LIMIT_HAS_RESET | WF_RATE_LIMIT_HAS_POLICY;
    ctx.budget.limit = 100;
    ctx.budget.remaining = 7;
    ctx.budget.reset = 30;
    ctx.budget.policy_quota = 100;
    ctx.budget.policy_window = 60;

    wf_xrpc_set_handler(client, budget_handler, &ctx);
    WF_CHECK(wf_xrpc_query(client, "app.bsky.feed.getTimeline", NULL, &res) ==
             WF_OK);
    WF_CHECK(ctx.called == 1);
    WF_CHECK(res.rate_limit.remaining == 7);

    got = wf_xrpc_client_get_rate_limit(client);
    WF_CHECK(got.present == ctx.budget.present);
    WF_CHECK(got.limit == 100 && got.remaining == 7 && got.reset == 30);
    WF_CHECK(got.policy_quota == 100 && got.policy_window == 60);
    wf_response_free(&res);

    /* A response without RateLimit headers clears the readback. */
    memset(&ctx.budget, 0, sizeof(ctx.budget));
    WF_CHECK(wf_xrpc_query(client, "app.bsky.feed.getTimeline", NULL, &res) ==
             WF_OK);
    got = wf_xrpc_client_get_rate_limit(client);
    WF_CHECK(got.present == 0);
    wf_response_free(&res);

    memset(&got, 0xff, sizeof(got));
    got = wf_xrpc_client_get_rate_limit(NULL);
    WF_CHECK(got.present == 0 && got.limit == 0 && got.remaining == 0);

    wf_xrpc_set_handler(client, NULL, NULL);
    wf_xrpc_client_free(client);
}

int main(void) {
    test_parser();
    test_retry();
    test_client_readback();
    test_async_readback_is_safe();
    WF_TEST_SUMMARY();
}
