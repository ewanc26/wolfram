/*
 * test_agent_pagination.c — offline unit tests for the agent pagination layer.
 *
 * No network injection hook exists in wolfram, so this test is fully offline
 * and deterministic:
 *   - wf_response_cursor is exercised against crafted JSON bodies.
 *   - the generic wf_agent_page iterator is exercised with a fake `call`
 *     closure that returns canned responses (no transport), verifying page
 *     counting, cursor advancement, exhaustion, max_pages, and early abort.
 *   - the typed paged wrappers are link/sanity checked via their documented
 *     invalid-argument contract (NULL inputs -> WF_ERR_INVALID_ARG).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"

/* --- helpers ----------------------------------------------------------- */

static wf_response make_response(const char *json) {
    wf_response r = {0};
    if (json) {
        r.body = strdup(json);
        r.body_len = strlen(json);
    }
    return r;
}

/* --- wf_response_cursor ------------------------------------------------ */

static void test_response_cursor(void) {
    /* Present, non-empty cursor. */
    wf_response r1 = make_response("{\"cursor\":\"abc123\",\"feed\":[]}");
    char *c = NULL;
    assert(wf_response_cursor(&r1, &c) == WF_OK);
    assert(c && strcmp(c, "abc123") == 0);
    free(c);
    wf_response_free(&r1);

    /* Absent cursor -> NULL out, still WF_OK. */
    wf_response r2 = make_response("{\"feed\":[],\"cursor\":null}");
    c = (char *)0x1; /* sentinel to detect reset */
    assert(wf_response_cursor(&r2, &c) == WF_OK);
    assert(c == NULL);
    wf_response_free(&r2);

    /* Cursor present but non-string -> treated as absent. */
    wf_response r3 = make_response("{\"cursor\":42}");
    c = (char *)0x1;
    assert(wf_response_cursor(&r3, &c) == WF_OK);
    assert(c == NULL);
    wf_response_free(&r3);

    /* Invalid JSON -> WF_ERR_PARSE, out left NULL. */
    wf_response r4 = make_response("{not json");
    c = (char *)0x1;
    assert(wf_response_cursor(&r4, &c) == WF_ERR_PARSE);
    assert(c == NULL);
    wf_response_free(&r4);

    /* Empty JSON object -> WF_OK, NULL cursor. */
    wf_response r5 = make_response("{}");
    c = (char *)0x1;
    assert(wf_response_cursor(&r5, &c) == WF_OK);
    assert(c == NULL);
    wf_response_free(&r5);

    /* Empty body is not valid JSON -> WF_ERR_PARSE. */
    wf_response r5b = make_response("");
    c = (char *)0x1;
    assert(wf_response_cursor(&r5b, &c) == WF_ERR_PARSE);
    assert(c == NULL);
    wf_response_free(&r5b);

    /* NULL response -> WF_ERR_INVALID_ARG. */
    c = (char *)0x1;
    assert(wf_response_cursor(NULL, &c) == WF_ERR_INVALID_ARG);

    /* NULL out param -> WF_ERR_INVALID_ARG. */
    wf_response r6 = make_response("{}");
    assert(wf_response_cursor(&r6, NULL) == WF_ERR_INVALID_ARG);
    wf_response_free(&r6);
}

/* --- generic wf_agent_page (fake transport) ---------------------------- */

typedef struct {
    int calls;     /* number of times fake_call invoked */
    int pages;     /* number of pages delivered to on_page */
    int abort_on;  /* if >0, on_page returns error on this page index (1-based) */
    int fail_on;   /* if >0, fake_call returns error on this call (1-based) */
} fake_state;

static wf_status fake_call(wf_agent *agent, int limit, const char *cursor,
                           wf_response *out, void *ud) {
    (void)agent;
    (void)limit;
    (void)cursor;
    fake_state *st = (fake_state *)ud;
    st->calls++;

    if (st->fail_on > 0 && st->calls == st->fail_on) {
        return WF_ERR_NETWORK;
    }

    /* First call: cursor "C1". Second and later: exhausted. */
    const char *body = (st->calls == 1)
        ? "{\"feed\":[],\"cursor\":\"C1\"}"
        : "{\"feed\":[]}";
    out->body = strdup(body);
    out->body_len = strlen(body);
    return WF_OK;
}

static wf_status count_on_page(wf_agent *agent, const char *cursor,
                               wf_response *resp, void *ud) {
    (void)agent;
    (void)cursor;
    (void)resp;
    fake_state *st = (fake_state *)ud;
    st->pages++;
    if (st->abort_on > 0 && st->pages == st->abort_on) {
        return WF_ERR_INVALID_ARG;
    }
    return WF_OK;
}

static void test_page_until_exhausted(void) {
    wf_agent *agent = wf_agent_new("https://example.invalid");
    assert(agent);

    fake_state st = {0};
    char *last = NULL;
    wf_status s = wf_agent_page(agent, fake_call, 10, 0, count_on_page, &st,
                                &last);
    assert(s == WF_OK);
    assert(st.calls == 2);
    assert(st.pages == 2);
    assert(last == NULL); /* exhausted */
    free(last);

    wf_agent_free(agent);
}

static void test_page_max_pages(void) {
    wf_agent *agent = wf_agent_new("https://example.invalid");
    assert(agent);

    fake_state st = {0};
    char *last = NULL;
    wf_status s = wf_agent_page(agent, fake_call, 10, 1, count_on_page, &st,
                                &last);
    assert(s == WF_OK);
    assert(st.calls == 1);
    assert(st.pages == 1);
    assert(last && strcmp(last, "C1") == 0); /* stopped with pending cursor */
    free(last);

    wf_agent_free(agent);
}

static void test_page_callback_abort(void) {
    wf_agent *agent = wf_agent_new("https://example.invalid");
    assert(agent);

    fake_state st = {0};
    st.abort_on = 1;
    char *last = NULL;
    wf_status s = wf_agent_page(agent, fake_call, 10, 0, count_on_page, &st,
                                &last);
    assert(s == WF_ERR_INVALID_ARG);
    assert(st.pages == 1);
    assert(st.calls == 1); /* no second fetch after abort */
    free(last);

    wf_agent_free(agent);
}

static void test_page_call_error(void) {
    wf_agent *agent = wf_agent_new("https://example.invalid");
    assert(agent);

    fake_state st = {0};
    st.fail_on = 1;
    char *last = NULL;
    wf_status s = wf_agent_page(agent, fake_call, 10, 0, count_on_page, &st,
                                &last);
    assert(s == WF_ERR_NETWORK);
    assert(st.calls == 1);
    free(last);

    wf_agent_free(agent);
}

static void test_page_invalid_args(void) {
    fake_state st = {0};
    assert(wf_agent_page(NULL, fake_call, 10, 0, count_on_page, &st, NULL)
           == WF_ERR_INVALID_ARG);
    wf_agent *agent = wf_agent_new("https://example.invalid");
    assert(agent);
    assert(wf_agent_page(agent, NULL, 10, 0, count_on_page, &st, NULL)
           == WF_ERR_INVALID_ARG);
    assert(wf_agent_page(agent, fake_call, 10, 0, NULL, &st, NULL)
           == WF_ERR_INVALID_ARG);
    wf_agent_free(agent);
}

/* --- typed paged wrappers: link + contract ----------------------------- */

static void test_typed_invalid_args(void) {
    /* NULL agent (or missing required arg) must yield WF_ERR_INVALID_ARG,
     * which also proves the symbols link against the library. */
    assert(wf_agent_get_timeline_paged(NULL, 10, 0, NULL, NULL, NULL)
           == WF_ERR_INVALID_ARG);
    assert(wf_agent_list_notifications_paged(NULL, 10, 0, NULL, NULL, NULL)
           == WF_ERR_INVALID_ARG);
    assert(wf_agent_get_author_feed_paged(NULL, "actor", 10, 0, NULL, NULL, NULL)
           == WF_ERR_INVALID_ARG);
    assert(wf_agent_list_records_paged(NULL, "coll", 10, 0, NULL, NULL, NULL)
           == WF_ERR_INVALID_ARG);
    /* Missing required string arg. */
    wf_agent *agent = wf_agent_new("https://example.invalid");
    assert(agent);
    assert(wf_agent_get_author_feed_paged(agent, NULL, 10, 0, NULL, NULL, NULL)
           == WF_ERR_INVALID_ARG);
    assert(wf_agent_list_records_paged(agent, NULL, 10, 0, NULL, NULL, NULL)
           == WF_ERR_INVALID_ARG);
    wf_agent_free(agent);
}

int main(void) {
    test_response_cursor();
    test_page_until_exhausted();
    test_page_max_pages();
    test_page_callback_abort();
    test_page_call_error();
    test_page_invalid_args();
    test_typed_invalid_args();
    printf("agent_pagination: all tests passed\n");
    return 0;
}
