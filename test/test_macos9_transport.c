/*
 * test_macos9_transport.c — offline tests for the Classic Mac OS 9 XRPC
 * transport.
 *
 * The transport cannot be exercised over a real network in CI: it needs macTLS,
 * an Open Transport endpoint and a PowerPC Mac OS 9. What it does contain is a
 * substantial amount of pure, testable logic that is easy to get subtly wrong
 * and that the rest of Wolfram's test suite never touches, because every other
 * transport is either curl-based or talks to a real PDS:
 *
 *   1. the bounded JSON scanner behind wf_xrpc_error(), which replaced the C99
 *      cJSON dependency this transport could not keep under C89;
 *   2. request building (URL assembly, percent-encoding, header rendering,
 *      base-URL re-rooting) observed through the wf_xrpc_set_handler() seam,
 *      which bypasses the network entirely;
 *   3. the refresh+retry path, driven entirely by the same seam;
 *   4. argument validation and response/ownership rules.
 *
 * Requires WOLFRAM_BUILD_MACOS9_TRANSPORT, since the logic under test lives in
 * the wolfram-macos9-transport library, which is only built when the Classic
 * Mac OS 9 transport is enabled.
 */

#include "macos9_mactls_stub.h"
#include "wolfram/macos9_tls.h"
#include "wolfram/xrpc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Give `r` an owned body, as a real transport would. */
static void set_body(wf_response *r, const char *body) {
    r->body_len = strlen(body);
    r->body = (char *)malloc(r->body_len + 1);
    assert(r->body != NULL);
    memcpy(r->body, body, r->body_len + 1);
}

/* ── 1. XRPC error envelope decoding ────────────────────────────────── */

static void test_error_envelope_basic(void) {
    wf_response r;
    char *err = NULL;
    char *msg = NULL;

    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"ExpiredToken\",\"message\":\"Token expired\"}");
    assert(wf_xrpc_error(&r, &err, &msg) == WF_OK);
    assert(err != NULL && strcmp(err, "ExpiredToken") == 0);
    assert(msg != NULL && strcmp(msg, "Token expired") == 0);
    free(err);
    free(msg);
    wf_response_free(&r);

    /* `message` is optional: a missing one leaves the out pointer NULL rather
     * than failing the call. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"NotFound\"}");
    err = NULL;
    msg = NULL;
    assert(wf_xrpc_error(&r, &err, &msg) == WF_OK);
    assert(err != NULL && strcmp(err, "NotFound") == 0);
    assert(msg == NULL);
    free(err);
    wf_response_free(&r);

    /* Members out of order, with insignificant whitespace. */
    memset(&r, 0, sizeof(r));
    set_body(&r,
             "{ \"message\" : \"nope\" , \"error\" : \"RateLimitExceeded\" }");
    assert(wf_xrpc_error(&r, &err, &msg) == WF_OK);
    assert(err != NULL && strcmp(err, "RateLimitExceeded") == 0);
    assert(msg != NULL && strcmp(msg, "nope") == 0);
    free(err);
    free(msg);
    wf_response_free(&r);

    /* Asking for one field must not leak the other. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"E\",\"message\":\"M\"}");
    err = NULL;
    assert(wf_xrpc_error(&r, &err, NULL) == WF_OK);
    assert(err != NULL && strcmp(err, "E") == 0);
    free(err);
    wf_response_free(&r);

    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"E\",\"message\":\"M\"}");
    msg = NULL;
    assert(wf_xrpc_error(&r, NULL, &msg) == WF_OK);
    assert(msg != NULL && strcmp(msg, "M") == 0);
    free(msg);
    wf_response_free(&r);

    /* Neither field requested is not an error. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"E\"}");
    assert(wf_xrpc_error(&r, NULL, NULL) == WF_OK);
    wf_response_free(&r);
}

static void test_error_envelope_escapes(void) {
    wf_response r;
    char *err = NULL;
    char *msg = NULL;

    memset(&r, 0, sizeof(r));
    set_body(
        &r, "{\"error\":\"Bad\",\"message\":\"line1\\nline2\\ttab\\\\slash\"}");
    assert(wf_xrpc_error(&r, &err, &msg) == WF_OK);
    assert(err != NULL && strcmp(err, "Bad") == 0);
    assert(msg != NULL);
    assert(strchr(msg, '\n') != NULL);
    assert(strchr(msg, '\t') != NULL);
    assert(strstr(msg, "\\slash") != NULL);
    free(err);
    free(msg);
    wf_response_free(&r);

    /* An escaped quote must not terminate the value early. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"E\",\"message\":\"say \\\"hi\\\" now\"}");
    msg = NULL;
    assert(wf_xrpc_error(&r, NULL, &msg) == WF_OK);
    assert(msg != NULL && strcmp(msg, "say \"hi\" now") == 0);
    free(msg);
    wf_response_free(&r);
}

static void test_error_envelope_rejects(void) {
    wf_response r;
    char *err = NULL;
    char *msg = NULL;

    /* Not a JSON object. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "\"just a string\"");
    assert(wf_xrpc_error(&r, &err, &msg) == WF_ERR_NOT_FOUND);
    assert(err == NULL && msg == NULL);
    wf_response_free(&r);

    /* Object with no `error` member. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"other\":\"thing\"}");
    assert(wf_xrpc_error(&r, &err, &msg) == WF_ERR_NOT_FOUND);
    assert(err == NULL && msg == NULL);
    wf_response_free(&r);

    /* `error` present but not a string: reported as no envelope rather than
     * coercing some other type into an error code. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":42}");
    assert(wf_xrpc_error(&r, &err, &msg) == WF_ERR_NOT_FOUND);
    assert(err == NULL);
    wf_response_free(&r);

    /* A nested `error` must not be mistaken for a top-level one. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"outer\":{\"error\":\"Nested\"}}");
    assert(wf_xrpc_error(&r, &err, &msg) == WF_ERR_NOT_FOUND);
    assert(err == NULL);
    wf_response_free(&r);

    /* Malformed: truncated object. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"Trunc");
    assert(wf_xrpc_error(&r, &err, &msg) != WF_OK);
    assert(err == NULL && msg == NULL);
    wf_response_free(&r);

    /* Malformed: unterminated string inside the value. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "{\"error\":\"oops\n\"}");
    assert(wf_xrpc_error(&r, &err, &msg) != WF_OK);
    assert(err == NULL);
    wf_response_free(&r);

    /* Empty body. */
    memset(&r, 0, sizeof(r));
    set_body(&r, "");
    assert(wf_xrpc_error(&r, &err, &msg) != WF_OK);
    wf_response_free(&r);

    /* No response at all. */
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_error(&r, &err, &msg) != WF_OK);
    assert(wf_xrpc_error(NULL, &err, &msg) == WF_ERR_INVALID_ARG);
}

/* ── 2. Request building through the handler seam ───────────────────── */

typedef struct {
    char url[1024];
    char method[16];
    char content_type[64];
    char body[2048];
    size_t body_len;
    char headers[1024];
    unsigned calls;
    unsigned status;
    const char *canned_body;
} capture;

/* Record the request and answer with a canned status/body. */
static wf_status capture_reply(void *ud, const char *method, const char *url,
                               const char *content_type, const char *body,
                               size_t body_len, const wf_http_header *headers,
                               size_t header_count, wf_response *out) {
    capture *c = (capture *)ud;
    size_t i;
    size_t n;

    c->calls++;
    snprintf(c->url, sizeof(c->url), "%s", url);
    snprintf(c->method, sizeof(c->method), "%s", method);
    snprintf(c->content_type, sizeof(c->content_type), "%s",
             content_type ? content_type : "");
    c->body[0] = '\0';
    c->body_len = body != NULL ? body_len : 0;
    if (body != NULL && body_len < sizeof(c->body)) {
        memcpy(c->body, body, body_len);
        c->body[body_len] = '\0';
    }
    c->headers[0] = '\0';
    for (i = 0; i < header_count; i++) {
        n = strlen(c->headers);
        snprintf(c->headers + n, sizeof(c->headers) - n, "%s%s: %s",
                 n == 0 ? "" : "\n", headers[i].name,
                 headers[i].value ? headers[i].value : "");
    }

    out->status = (int)c->status;
    set_body(out, c->canned_body ? c->canned_body : "{\"ok\":true}");
    return (c->status >= 200 && c->status < 300) ? WF_OK : WF_ERR_HTTP;
}

/* Value of the request's Authorization header, or NULL if absent. */
static const char *auth_header(const capture *c) {
    if (strncmp(c->headers, "Authorization:", 14) == 0) return c->headers + 15;
    if (strstr(c->headers, "\nAuthorization:") != NULL)
        return strstr(c->headers, "\nAuthorization:") + 16;
    return NULL;
}

static void test_request_url_building(void) {
    wf_xrpc_client *c;
    capture cap;
    wf_response r;
    wf_xrpc_param params[2];

    c = wf_xrpc_client_new("https://pds.example/");
    assert(c != NULL);
    memset(&cap, 0, sizeof(cap));
    cap.status = 200;
    wf_xrpc_set_handler(c, capture_reply, &cap);

    /* A trailing slash on the base URL is normalised away so paths do not
     * double up. */
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    assert(strcmp(cap.method, "GET") == 0);
    assert(strcmp(cap.url,
                  "https://pds.example/xrpc/app.bsky.feed.getTimeline") == 0);
    wf_response_free(&r);

    /* Query parameters are percent-encoded, in input order. */
    params[0].name = "actor";
    params[0].value = "alice.bsky.social";
    params[1].name = "limit";
    params[1].value = "20";
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query_params(c, "app.bsky.actor.getProfile", params, 2,
                                &r) == WF_OK);
    assert(strcmp(cap.url, "https://pds.example/xrpc/app.bsky.actor.getProfile"
                           "?actor=alice.bsky.social&limit=20") == 0);
    wf_response_free(&r);

    /* Characters that must be escaped in a value. */
    params[0].name = "q";
    params[0].value = "a b&c=d/e?f";
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query_params(c, "app.bsky.feed.searchPosts", params, 1,
                                &r) == WF_OK);
    assert(strstr(cap.url, "?q=a%20b%26c%3Dd%2Fe%3Ff") != NULL);
    wf_response_free(&r);

    /* Parameter names are escaped on the same terms as values. */
    params[0].name = "a b&c";
    params[0].value = "v";
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query_params(c, "app.bsky.feed.searchPosts", params, 1,
                                &r) == WF_OK);
    assert(strstr(cap.url, "?a%20b%26c=v") != NULL);
    wf_response_free(&r);

    /* A NULL value is rejected rather than encoded as the string "NULL". */
    params[0].name = "actor";
    params[0].value = NULL;
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query_params(c, "app.bsky.actor.getProfile", params, 1,
                                &r) == WF_ERR_INVALID_ARG);
    wf_response_free(&r);

    /* A POST carries its JSON body and content type. */
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_procedure(c, "com.atproto.repo.createRecord",
                             "{\"text\":\"hi\"}", &r) == WF_OK);
    assert(strcmp(cap.method, "POST") == 0);
    assert(strcmp(cap.content_type, "application/json") == 0);
    assert(strcmp(cap.body, "{\"text\":\"hi\"}") == 0);
    wf_response_free(&r);

    /* A blob upload posts binary with the caller's content type, not JSON. */
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_upload_blob(c, "com.atproto.repo.uploadBlob", "\xFF\xD8jpeg",
                               6, "image/jpeg", &r) == WF_OK);
    assert(strcmp(cap.content_type, "image/jpeg") == 0);
    assert(cap.body_len == 6);
    wf_response_free(&r);

    /* Re-pointing the client re-roots every subsequent URL. */
    assert(wf_xrpc_client_set_base_url(c, "https://other.example") == WF_OK);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    assert(strcmp(cap.url,
                  "https://other.example/xrpc/app.bsky.feed.getTimeline") == 0);
    wf_response_free(&r);

    /* A base URL that already has a path prefix keeps it. */
    assert(wf_xrpc_client_set_base_url(c, "https://host.example/atproto") ==
           WF_OK);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    assert(strcmp(cap.url, "https://host.example/atproto/xrpc/"
                           "app.bsky.feed.getTimeline") == 0);
    wf_response_free(&r);

    wf_xrpc_client_free(c);
}

static void test_auth_header(void) {
    wf_xrpc_client *c;
    capture cap;
    wf_response r;
    const char *value;

    c = wf_xrpc_client_new("https://pds.example");
    assert(c != NULL);
    memset(&cap, 0, sizeof(cap));
    cap.status = 200;
    wf_xrpc_set_handler(c, capture_reply, &cap);

    /* Unauthenticated by default. */
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    assert(auth_header(&cap) == NULL);
    wf_response_free(&r);

    /* A bearer token goes out with the Bearer scheme, and nothing else
     * changes about the request. */
    wf_xrpc_client_set_auth(c, "token-abc");
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    value = auth_header(&cap);
    assert(value != NULL && strcmp(value, "Bearer token-abc") == 0);
    wf_response_free(&r);

    /* Clearing the token clears the header. */
    wf_xrpc_client_set_auth(c, NULL);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    assert(auth_header(&cap) == NULL);
    wf_response_free(&r);

    /* The token is copied, so a later change to the caller's buffer cannot
     * rewrite a token already installed on the client. */
    {
        char token[32];
        snprintf(token, sizeof(token), "first");
        wf_xrpc_client_set_auth(c, token);
        snprintf(token, sizeof(token), "second");
        memset(&r, 0, sizeof(r));
        assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) ==
               WF_OK);
        value = auth_header(&cap);
        assert(value != NULL && strcmp(value, "Bearer first") == 0);
        wf_response_free(&r);
    }

    wf_xrpc_client_free(c);
}

static void test_base_url_getter(void) {
    wf_xrpc_client *c;
    char *first;
    char *second;

    c = wf_xrpc_client_new("https://pds.example///");
    assert(c != NULL);
    first = wf_xrpc_get_base_url(c);
    second = wf_xrpc_get_base_url(c);
    assert(first != NULL && second != NULL);
    /* Normalised: trailing slashes stripped. */
    assert(strcmp(first, "https://pds.example") == 0);
    /* Each call returns an independent copy the caller owns. */
    assert(first != second);
    free(first);
    free(second);
    assert(wf_xrpc_get_base_url(NULL) == NULL);

    wf_xrpc_client_free(c);
}

/* ── 3. Refresh and retry ───────────────────────────────────────────── */

typedef struct {
    wf_xrpc_client *client;
    unsigned refreshes;
    wf_status result;
    const char *new_token;
} refresh_state;

/* Stand-in for an OAuth refresh: installs a new token on the client, which is
 * exactly what the transport's documented contract requires of it. */
static wf_status do_refresh(void *ud) {
    refresh_state *s = (refresh_state *)ud;
    s->refreshes++;
    if (s->result == WF_OK) wf_xrpc_client_set_auth(s->client, s->new_token);
    return s->result;
}

/* Answer 401 ExpiredToken until a token other than "stale" is presented. */
static wf_status stale_then_ok(void *ud, const char *method, const char *url,
                               const char *content_type, const char *body,
                               size_t body_len, const wf_http_header *headers,
                               size_t header_count, wf_response *out) {
    capture *c = (capture *)ud;
    const char *value;
    size_t i;

    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;

    c->calls++;
    for (i = 0; i < header_count; i++) {
        if (headers[i].name == NULL ||
            strcmp(headers[i].name, "Authorization") != 0)
            continue;
        value = headers[i].value;
        if (value != NULL && strstr(value, "fresh") != NULL) {
            out->status = 200;
            set_body(out, "{\"ok\":true}");
            return WF_OK;
        }
    }
    out->status = 401;
    set_body(out, "{\"error\":\"ExpiredToken\",\"message\":\"token expired\"}");
    return WF_ERR_HTTP;
}

static void test_refresh_and_retry(void) {
    wf_xrpc_client *c;
    refresh_state state;
    capture cap;
    wf_response r;

    c = wf_xrpc_client_new("https://pds.example");
    assert(c != NULL);
    memset(&cap, 0, sizeof(cap));
    memset(&state, 0, sizeof(state));
    state.client = c;
    state.result = WF_OK;
    state.new_token = "fresh";

    wf_xrpc_set_handler(c, stale_then_ok, &cap);
    wf_xrpc_client_set_auth(c, "stale");
    wf_xrpc_client_set_refresh_handler(c, do_refresh, &state);

    /* One logical request, one refresh, two wire attempts. */
    assert(cap.calls == 0);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    assert(state.refreshes == 1);
    assert(cap.calls == 2);
    assert(r.status == 200);
    wf_response_free(&r);
    wf_response_free(&r);

    /* The next request already carries the refreshed token, so it must not
     * trigger another refresh: the retry is one-shot per request, not a loop,
     * and costs no extra wire attempt. */
    assert(cap.calls == 2);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    assert(state.refreshes == 1);
    assert(cap.calls == 3);
    wf_response_free(&r);

    /* A failing refresh surfaces the original 401 rather than looping, and
     * does not re-issue the request a second time. The token is reset first,
     * because the successful refresh above left a fresh one installed that the
     * stub would happily accept. */
    assert(cap.calls == 3);
    state.result = WF_ERR_AUTH;
    state.refreshes = 0;
    wf_xrpc_client_set_auth(c, "stale");
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) ==
           WF_ERR_HTTP);
    assert(r.status == 401);
    assert(state.refreshes == 1);
    wf_response_free(&r);

    /* The server's message survives a failed refresh for the UI to show. */
    assert(wf_xrpc_last_error(c) != NULL);
    assert(strcmp(wf_xrpc_last_error(c), "token expired") == 0);

    /* Removing the refresh handler disables the retry path entirely. */
    state.result = WF_OK;
    state.refreshes = 0;
    wf_xrpc_client_set_auth(c, "stale");
    wf_xrpc_client_set_refresh_handler(c, NULL, NULL);
    assert(cap.calls == 4);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) ==
           WF_ERR_HTTP);
    assert(state.refreshes == 0);
    assert(cap.calls == 5);
    wf_response_free(&r);

    wf_xrpc_client_free(c);
}

/* A 401 with no recognisable envelope must not trigger a refresh: the token
 * may be fine and the endpoint may simply be refusing for another reason. */
static void test_no_refresh_without_envelope(void) {
    wf_xrpc_client *c;
    refresh_state state;
    wf_response r;
    capture cap;

    c = wf_xrpc_client_new("https://pds.example");
    assert(c != NULL);
    memset(&state, 0, sizeof(state));
    state.client = c;
    state.result = WF_OK;
    state.new_token = "fresh";
    memset(&cap, 0, sizeof(cap));
    cap.status = 401;
    cap.canned_body = "<html>nope</html>";
    wf_xrpc_set_handler(c, capture_reply, &cap);
    wf_xrpc_client_set_refresh_handler(c, do_refresh, &state);

    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) ==
           WF_ERR_HTTP);
    assert(state.refreshes == 0);
    assert(cap.calls == 1);
    /* No envelope means no message to report, so last error stays empty. */
    assert(wf_xrpc_last_error(c) == NULL);
    wf_response_free(&r);

    wf_xrpc_client_free(c);
}

static void test_last_error_from_envelope(void) {
    wf_xrpc_client *c;
    capture cap;
    wf_response r;

    c = wf_xrpc_client_new("https://pds.example");
    assert(c != NULL);
    assert(wf_xrpc_last_error(c) == NULL);

    memset(&cap, 0, sizeof(cap));
    cap.status = 400;
    cap.canned_body =
        "{\"error\":\"InvalidRequest\",\"message\":\"bad handle\"}";
    wf_xrpc_set_handler(c, capture_reply, &cap);

    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.actor.getProfile", NULL, &r) ==
           WF_ERR_HTTP);
    assert(wf_xrpc_last_error(c) != NULL);
    assert(strcmp(wf_xrpc_last_error(c), "bad handle") == 0);
    wf_response_free(&r);

    /* An envelope with no message falls back to the machine-readable code. */
    cap.canned_body = "{\"error\":\"InvalidRequest\"}";
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.actor.getProfile", NULL, &r) ==
           WF_ERR_HTTP);
    assert(wf_xrpc_last_error(c) != NULL);
    assert(strcmp(wf_xrpc_last_error(c), "InvalidRequest") == 0);
    wf_response_free(&r);

    /* A later success clears it, so a stale message cannot be attributed to a
     * request that did not fail. */
    cap.status = 200;
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.actor.getProfile", NULL, &r) == WF_OK);
    assert(wf_xrpc_last_error(c) == NULL);
    wf_response_free(&r);

    wf_xrpc_client_free(c);
}

/* ── 4. Argument validation and ownership ───────────────────────────── */

static void test_argument_validation(void) {
    wf_xrpc_client *c;
    capture cap;
    wf_response r;
    unsigned before;

    c = wf_xrpc_client_new("https://pds.example");
    assert(c != NULL);
    memset(&cap, 0, sizeof(cap));
    cap.status = 200;
    wf_xrpc_set_handler(c, capture_reply, &cap);

    assert(wf_xrpc_client_new(NULL) == NULL);
    assert(wf_xrpc_client_new("") == NULL);
    assert(wf_xrpc_client_set_base_url(c, NULL) == WF_ERR_INVALID_ARG);
    assert(wf_xrpc_client_set_base_url(c, "") == WF_ERR_INVALID_ARG);
    assert(wf_xrpc_client_set_base_url(NULL, "https://x") ==
           WF_ERR_INVALID_ARG);

    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(NULL, "app.bsky.feed.getTimeline", NULL, &r) ==
           WF_ERR_INVALID_ARG);
    assert(wf_xrpc_query(c, NULL, NULL, &r) == WF_ERR_INVALID_ARG);
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, NULL) ==
           WF_ERR_INVALID_ARG);
    assert(wf_http_get(c, NULL, &r) == WF_ERR_INVALID_ARG);
    assert(wf_http_post(c, NULL, NULL, NULL, NULL, 0, &r) ==
           WF_ERR_INVALID_ARG);
    assert(wf_xrpc_upload_blob(c, "com.atproto.repo.uploadBlob", NULL, 4,
                               "image/jpeg", &r) == WF_ERR_INVALID_ARG);
    assert(wf_xrpc_upload_blob(c, "com.atproto.repo.uploadBlob", "data", 4,
                               NULL, &r) == WF_ERR_INVALID_ARG);

    /* https-only refuses a non-https URL before any I/O, so no handler call
     * happens at all. */
    before = cap.calls;
    wf_xrpc_client_set_https_only(c, 1);
    assert(wf_http_get(c, "http://insecure.example/x", &r) ==
           WF_ERR_INVALID_ARG);
    assert(cap.calls == before);
    assert(wf_http_get(c, "https://secure.example/x", &r) == WF_OK);
    assert(cap.calls == before + 1);
    wf_response_free(&r);

    /* The public (unauthenticated) getter is https-only unconditionally. */
    assert(wf_http_get_public(c, "http://insecure.example/avatar", 0, &r) ==
           WF_ERR_INVALID_ARG);
    assert(cap.calls == before + 1);

    /* An authenticated client must not leak its bearer token through the
     * public getter. */
    wf_xrpc_client_set_auth(c, "secret-token");
    memset(&r, 0, sizeof(r));
    assert(wf_http_get_public(c, "https://cdn.example/avatar.jpg", 0, &r) ==
           WF_OK);
    assert(auth_header(&cap) == NULL);
    wf_response_free(&r);

    /* Response caps are accepted, including the reset and overflow values. */
    wf_xrpc_client_set_max_response_bytes(c, 4096);
    wf_xrpc_client_set_max_response_bytes(c, 0);
    wf_xrpc_client_set_max_response_bytes(c, (size_t)-1);
    wf_xrpc_client_set_max_response_bytes(NULL, 4096);
    /* A 1-byte cap cannot hold even a minimal JSON body. */
    wf_xrpc_client_set_max_response_bytes(c, 1);
    memset(&r, 0, sizeof(r));
    assert(wf_http_get(c, "https://secure.example/x", &r) != WF_OK);
    /* Per-call caps override the client default. */
    memset(&r, 0, sizeof(r));
    assert(wf_http_get_limited(c, "https://secure.example/x", 1, &r) != WF_OK);

    /* wf_response_free is safe on NULL and on a zeroed struct, and freeing
     * twice through a cleared struct leaks nothing and crashes nothing. */
    wf_response_free(NULL);
    memset(&r, 0, sizeof(r));
    wf_response_free(&r);
    wf_response_free(&r);

    wf_xrpc_client_free(NULL);
    wf_xrpc_client_free(c);
}

static void test_yield_hook_is_optional(void) {
    /* Installing and removing the cooperative-yield hook must be safe even
     * though nothing on this host ever drives a real connection: a request
     * simply works without one, and a null hook with a non-null client is
     * equally harmless. */
    capture cap;
    wf_xrpc_client *c;
    wf_response r;

    wf_macos9_set_yield_callback(NULL, NULL);

    c = wf_xrpc_client_new("https://pds.example");
    assert(c != NULL);
    memset(&cap, 0, sizeof(cap));
    cap.status = 200;
    wf_xrpc_set_handler(c, capture_reply, &cap);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    wf_response_free(&r);

    /* Removing the hook again, and installing a null hook with a live client,
     * are both no-ops rather than errors. */
    wf_macos9_set_yield_callback(NULL, NULL);
    memset(&r, 0, sizeof(r));
    assert(wf_xrpc_query(c, "app.bsky.feed.getTimeline", NULL, &r) == WF_OK);
    wf_response_free(&r);

    /* The per-connection API tolerates NULL, so a caller cannot be made to
     * guard every call site. Its documented failure convention is a negated
     * wf_status, not the status itself, so check both halves of that. */
    assert(wf_macos9_send(NULL, "x", 1) == -(long)WF_ERR_INVALID_ARG);
    assert(wf_macos9_recv(NULL, &r, 1) == -(long)WF_ERR_INVALID_ARG);
    assert(wf_macos9_send(NULL, NULL, 0) == -(long)WF_ERR_INVALID_ARG);
    assert(wf_macos9_recv(NULL, &r, 0) == -(long)WF_ERR_INVALID_ARG);
    assert(wf_macos9_last_error(NULL) == NULL);
    wf_macos9_close(NULL);

    wf_xrpc_client_free(c);
}

int main(void) {
    /* Every test below drives the transport through wf_xrpc_set_handler(),
     * which returns before any connection is attempted. The macTLS stubs count
     * entry-point calls, so this assert turns "we did not accidentally touch
     * the network" into a checked property rather than an assumption: if a
     * future change routes a handler request through the real path, the tests
     * fail here instead of silently passing. */
    macos9_stub_reset();

    test_error_envelope_basic();
    test_error_envelope_escapes();
    test_error_envelope_rejects();

    test_request_url_building();
    test_auth_header();
    test_base_url_getter();

    test_refresh_and_retry();
    test_no_refresh_without_envelope();
    test_last_error_from_envelope();

    test_argument_validation();
    test_yield_hook_is_optional();

    assert(macos9_stub_total_calls() == 0);

    printf("test_macos9_transport: all checks passed\n");
    return 0;
}
