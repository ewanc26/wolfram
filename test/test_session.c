/**
 * test_session.c — unit tests for the session module.
 *
 * Tests the lifecycle and argument validation that can be done
 * without a live PDS. Network-dependent tests (login, refresh, get,
 * delete) are left as integration tests.
 */

#include "wolfram/session.h"
#include "test.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void test_session_create_free(void) {
    wf_session *session = wf_session_new("https://eurosky.social");
    assert(session != NULL);
    assert(wf_session_has_session(session) == 0);
    assert(session->has_session == 0);
    wf_session_free(session);
}

static void test_session_free_null(void) {
    /* Should not crash */
    wf_session_free(NULL);
}

static void test_session_new_null_url(void) {
    wf_session *session = wf_session_new(NULL);
    assert(session == NULL);
}

static void test_session_new_empty_url(void) {
    wf_session *session = wf_session_new("");
    assert(session == NULL);
}

static void test_session_login_null_args(void) {
    wf_session *session = wf_session_new("https://eurosky.social");
    assert(session != NULL);

    assert(wf_session_login(session, NULL, "pass") == WF_ERR_INVALID_ARG);
    assert(wf_session_login(session, "user", NULL) == WF_ERR_INVALID_ARG);
    assert(wf_session_login(NULL, "user", "pass") == WF_ERR_INVALID_ARG);
    assert(wf_session_has_session(session) == 0);

    wf_session_free(session);
}

static void test_session_refresh_no_session(void) {
    wf_session *session = wf_session_new("https://eurosky.social");
    assert(session != NULL);

    assert(wf_session_refresh(session) == WF_ERR_INVALID_ARG);
    assert(wf_session_refresh(NULL) == WF_ERR_INVALID_ARG);

    wf_session_free(session);
}

static void test_session_resume_invalid_data(void) {
    wf_session *session = wf_session_new("https://eurosky.social");
    wf_session_data data = {0};
    assert(session != NULL);

    assert(wf_session_resume(NULL, &data) == WF_ERR_INVALID_ARG);
    assert(wf_session_resume(session, NULL) == WF_ERR_INVALID_ARG);
    assert(wf_session_resume(session, &data) == WF_ERR_INVALID_ARG);
    assert(wf_session_has_session(session) == 0);
    wf_session_free(session);
}

static void test_session_get_no_session(void) {
    wf_session *session = wf_session_new("https://eurosky.social");
    assert(session != NULL);

    assert(wf_session_get(session) == WF_ERR_INVALID_ARG);
    assert(wf_session_get(NULL) == WF_ERR_INVALID_ARG);

    wf_session_free(session);
}

static void test_session_delete_no_session(void) {
    wf_session *session = wf_session_new("https://eurosky.social");
    assert(session != NULL);

    assert(wf_session_delete(session) == WF_ERR_INVALID_ARG);
    assert(wf_session_delete(NULL) == WF_ERR_INVALID_ARG);

    wf_session_free(session);
}

static void test_session_has_session_null(void) {
    assert(wf_session_has_session(NULL) == 0);
}

int main(void) {
    test_session_create_free();
    test_session_free_null();
    test_session_new_null_url();
    test_session_new_empty_url();
    test_session_login_null_args();
    test_session_refresh_no_session();
    test_session_resume_invalid_data();
    test_session_get_no_session();
    test_session_delete_no_session();
    test_session_has_session_null();

    printf("session tests: all passed\n");
    return 0;
}
