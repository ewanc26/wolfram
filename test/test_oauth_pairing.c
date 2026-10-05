/*
 * test_oauth_pairing.c -- oauth_pairing.h against the shared vectors in
 * test/vectors/oauth_pairing.json: pure parsers, request body, and the whole
 * begin/poll driver against an in-process handler (no network). Also asserts
 * the bearer token never reaches stderr, where wf_log writes.
 */

#include "wolfram/oauth_pairing.h"

#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef WF_TEST_VECTOR_DIR
#define WF_TEST_VECTOR_DIR "test/vectors"
#endif

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n)
        buf[n] = '\0';
    else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static const char *vstr(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

/* The reply text of a vector: "body_text" verbatim, else "body" serialised. */
static char *reply_text(const cJSON *vec) {
    const char *t = vstr(vec, "body_text");
    if (t) return strdup(t);
    return cJSON_PrintUnformatted(
        cJSON_GetObjectItemCaseSensitive(vec, "body"));
}

#define VCHECK(vec, cond)                                                      \
    do {                                                                       \
        int ok_ = (cond);                                                      \
        WF_CHECK(ok_);                                                         \
        if (!ok_) fprintf(stderr, "  vector: %s\n", vstr(vec, "name"));        \
    } while (0)

static void run_build(const cJSON *vec) {
    char out[256];
    wf_status st =
        wf_oauth_pair_build_begin(vstr(vec, "handle"), out, sizeof out);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(vec, "invalid")))
        VCHECK(vec, st == WF_ERR_INVALID_ARG);
    else
        VCHECK(vec, st == WF_OK && strcmp(out, vstr(vec, "body")) == 0);
}

static void run_begin(const cJSON *vec) {
    char *text = reply_text(vec);
    const cJSON *expect = cJSON_GetObjectItemCaseSensitive(vec, "expect");
    wf_oauth_pair_begin r;
    wf_status st = wf_oauth_pair_parse_begin(text, strlen(text), &r);
    if (cJSON_IsString(expect)) {
        VCHECK(vec, st == WF_ERR_PARSE);
        VCHECK(vec, r.pair_code[0] == '\0' && r.pair_url[0] == '\0');
    } else {
        VCHECK(vec, st == WF_OK);
        VCHECK(vec, strcmp(r.pair_code, vstr(expect, "pair_code")) == 0);
        VCHECK(vec, strcmp(r.pair_url, vstr(expect, "pair_url")) == 0);
        VCHECK(vec, r.expires_at == (int64_t)cJSON_GetObjectItemCaseSensitive(
                                        expect, "expires_at")
                                        ->valuedouble);
    }
    free(text);
}

static void run_poll(const cJSON *vec) {
    char *text = reply_text(vec);
    const cJSON *expect = cJSON_GetObjectItemCaseSensitive(vec, "expect");
    wf_oauth_pair_poll r;
    wf_status st = wf_oauth_pair_parse_poll(text, strlen(text), &r);
    if (cJSON_IsString(expect)) {
        VCHECK(vec, st == WF_ERR_PARSE);
        VCHECK(vec, r.token[0] == '\0' && r.did[0] == '\0');
    } else {
        const char *state = vstr(expect, "state");
        VCHECK(vec, st == WF_OK);
        if (!strcmp(state, "pending")) {
            VCHECK(vec, r.state == WF_OAUTH_POLL_PENDING);
        } else if (!strcmp(state, "error")) {
            VCHECK(vec, r.state == WF_OAUTH_POLL_ERROR);
            VCHECK(vec, strcmp(r.message, vstr(expect, "message")) == 0);
        } else {
            VCHECK(vec, r.state == WF_OAUTH_POLL_COMPLETE);
            VCHECK(vec, !strcmp(r.token, vstr(expect, "token")));
            VCHECK(vec, !strcmp(r.handle, vstr(expect, "handle")));
            VCHECK(vec, !strcmp(r.did, vstr(expect, "did")));
            VCHECK(vec, !strcmp(r.service, vstr(expect, "service")));
        }
    }
    free(text);
}

/* ---- driver ---------------------------------------------------------- */

typedef struct script {
    const cJSON *polls; /* array of {http, body} */
    int polls_made;
    int slept;
    int coded;
    int cancel_after; /* cancel once this many polls were made (-1: never) */
    int64_t now;
    char seen_code[WF_OAUTH_PAIR_CODE_MAX];
} script;

static wf_status handler(void *ud, const char *method, const char *url,
                         const char *ctype, const char *body, size_t blen,
                         const wf_http_header *h, size_t hc, wf_response *out) {
    script *s = ud;
    (void)ctype;
    (void)body;
    (void)blen;
    (void)h;
    (void)hc;
    const char *json;
    char *owned = NULL;
    long http = 200;
    if (strcmp(method, "POST") == 0) {
        json = "{\"pair_code\":\"CODE42\",\"pair_url\":\"https://"
               "auth.example.com/pair/CODE42\","
               "\"expires_at\":1000}";
    } else {
        const char *q = strstr(url, "code=");
        if (q) snprintf(s->seen_code, sizeof s->seen_code, "%s", q + 5);
        int n = cJSON_GetArraySize(s->polls);
        const cJSON *step = cJSON_GetArrayItem(
            s->polls, s->polls_made < n ? s->polls_made : n - 1);
        s->polls_made++;
        http =
            (long)cJSON_GetObjectItemCaseSensitive(step, "http")->valuedouble;
        owned = cJSON_PrintUnformatted(
            cJSON_GetObjectItemCaseSensitive(step, "body"));
        json = owned;
    }
    out->status = http;
    out->body = strdup(json);
    out->body_len = strlen(json);
    free(owned);
    return http >= 200 && http < 300 ? WF_OK : WF_ERR_HTTP;
}

static void on_code(const wf_oauth_pair_begin *b, void *ud) {
    script *s = ud;
    s->coded = strcmp(b->pair_code, "CODE42") == 0;
}
static int do_cancel(void *ud) {
    script *s = ud;
    return s->cancel_after >= 0 && s->polls_made >= s->cancel_after;
}
static void do_sleep(unsigned ms, void *ud) {
    script *s = ud;
    WF_CHECK(ms == WF_OAUTH_PAIR_POLL_INTERVAL_MS);
    s->slept++;
    s->now += 2; /* two seconds pass per sleep */
}
static int64_t do_now(void *ud) {
    return ((script *)ud)->now;
}

static wf_status drive(script *s, wf_oauth_pair_poll *out) {
    wf_xrpc_client *c = wf_xrpc_client_new("https://auth.example.com");
    wf_xrpc_set_handler(c, handler, s);
    wf_oauth_pair_hooks hk = {on_code, do_cancel, do_sleep, do_now, s};
    wf_status st = wf_oauth_pair_run(c, "a.example", &hk, out);
    wf_xrpc_client_free(c);
    return st;
}

static void run_driver(const cJSON *vec) {
    const cJSON *expect = cJSON_GetObjectItemCaseSensitive(vec, "expect");
    script s = {.polls = cJSON_GetObjectItemCaseSensitive(vec, "polls"),
                .cancel_after = -1,
                .now = 0};
    wf_oauth_pair_poll out;
    wf_status st = drive(&s, &out);
    const char *want = vstr(expect, "status");
    wf_status want_st = !strcmp(want, "ok")     ? WF_OK
                        : !strcmp(want, "auth") ? WF_ERR_AUTH
                                                : WF_ERR_PARSE;
    VCHECK(vec, st == want_st);
    VCHECK(vec, s.coded == 1);
    VCHECK(vec, s.polls_made ==
                    (int)cJSON_GetObjectItemCaseSensitive(expect, "polls_made")
                        ->valuedouble);
    VCHECK(vec, strcmp(s.seen_code, "CODE42") == 0);
    if (vstr(expect, "message"))
        VCHECK(vec, strcmp(out.message, vstr(expect, "message")) == 0);
    /* Sleeps happen between polls only: never after the last one. */
    VCHECK(vec, s.slept == s.polls_made - 1);
    wf_oauth_pair_poll_wipe(&out);
}

static void check_driver_edges(void) {
    cJSON *pending =
        cJSON_Parse("[{\"http\":200,\"body\":{\"status\":\"pending\"}}]");
    script s = {.polls = pending, .cancel_after = 2, .now = 0};
    wf_oauth_pair_poll out;
    WF_CHECK(drive(&s, &out) == WF_ERR_STATE); /* cancel hook stops it */
    WF_CHECK(s.polls_made == 2);

    script t = {.polls = pending, .cancel_after = -1, .now = 0};
    WF_CHECK(drive(&t, &out) ==
             WF_ERR_TIMEOUT); /* expires_at=1000, +2s/sleep */
    WF_CHECK(t.polls_made > 100 && t.polls_made < 600);
    cJSON_Delete(pending);
}

/* Run the driver to a COMPLETE reply with stderr captured, at debug log level,
 * and require the token string never to appear. */
static void check_token_not_logged(const cJSON *driver) {
    const cJSON *vec = NULL, *v;
    cJSON_ArrayForEach(v, driver) if (strstr(vstr(v, "name"), "5xx")) vec = v;
    WF_CHECK(vec != NULL);
    if (!vec) return;
    setenv("WOLFRAM_LOG_LEVEL", "debug", 1);
    char tmpl[] = "/tmp/wf_pair_stderrXXXXXX";
    int fd = mkstemp(tmpl);
    int saved = dup(2);
    fflush(stderr);
    dup2(fd, 2);
    script s = {.polls = cJSON_GetObjectItemCaseSensitive(vec, "polls"),
                .cancel_after = -1};
    wf_oauth_pair_poll out;
    wf_status st = drive(&s, &out);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    close(fd);
    char *captured = slurp(tmpl);
    unlink(tmpl);
    WF_CHECK(st == WF_OK);
    WF_CHECK(strcmp(out.token, "SECRETTOKEN-0123") == 0);
    WF_CHECK(captured != NULL);
    if (captured) {
        WF_CHECK(strstr(captured, "SECRETTOKEN") == NULL);
        WF_CHECK(strstr(captured, "CODE42") == NULL);
    }
    free(captured);
    wf_oauth_pair_poll_wipe(&out);
    /* Wiped means wiped. */
    WF_CHECK(out.token[0] == '\0' && out.did[0] == '\0' && out.state == 0);
}

static void check_args(void) {
    wf_oauth_pair_begin b;
    wf_oauth_pair_poll p;
    char buf[8];
    WF_CHECK(wf_oauth_pair_build_begin("a.b", buf, sizeof buf) ==
             WF_ERR_INVALID_ARG); /* too small */
    WF_CHECK(wf_oauth_pair_build_begin(NULL, buf, sizeof buf) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_oauth_pair_parse_begin(NULL, 0, &b) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_oauth_pair_begin_request(NULL, "a.b", &b) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_oauth_pair_poll_once(NULL, "x", &p) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_oauth_pair_run(NULL, "a.b", NULL, &p) == WF_ERR_INVALID_ARG);
    wf_oauth_pair_poll_wipe(NULL);
    WF_CHECK(strcmp(WF_OAUTH_PAIR_BEGIN_NSID, "uk.ewancroft.oauth.begin") == 0);
    WF_CHECK(strcmp(WF_OAUTH_PAIR_POLL_NSID, "uk.ewancroft.oauth.poll") == 0);
}

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/oauth_pairing.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    WF_CHECK(text != NULL);
    if (text) {
        cJSON *root = cJSON_Parse(text);
        WF_CHECK(root != NULL);
        const cJSON *v;
        cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(
                                  root, "build_begin")) run_build(v);
        cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "begin"))
            run_begin(v);
        cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "poll"))
            run_poll(v);
        cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "driver"))
            run_driver(v);
        check_token_not_logged(
            cJSON_GetObjectItemCaseSensitive(root, "driver"));
        cJSON_Delete(root);
        free(text);
    }
    check_driver_edges();
    check_args();
    WF_TEST_SUMMARY();
}
