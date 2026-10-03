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
    (void)session;
}

static void test_session_new_empty_url(void) {
    wf_session *session = wf_session_new("");
    assert(session == NULL);
    (void)session;
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
    (void)data;

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

static void free_data(wf_session_data *d) {
    free(d->access_jwt);
    free(d->refresh_jwt);
    free(d->handle);
    free(d->did);
    free(d->pds_url);
}

static void test_session_data_json_roundtrip(void) {
    wf_session_data in = {0};
    in.access_jwt = "acc";
    in.refresh_jwt = "ref";
    in.handle = "alice.example.com";
    in.did = "did:plc:abc";
    in.pds_url = "https://pds.example.com";

    char *json = NULL;
    assert(wf_session_data_to_json(&in, &json) == WF_OK);
    assert(json && !strchr(json, '\n'));

    wf_session_data out;
    assert(wf_session_data_from_json(json, strlen(json), &out) == WF_OK);
    assert(strcmp(out.access_jwt, "acc") == 0);
    assert(strcmp(out.refresh_jwt, "ref") == 0);
    assert(strcmp(out.handle, "alice.example.com") == 0);
    assert(strcmp(out.did, "did:plc:abc") == 0);
    assert(strcmp(out.pds_url, "https://pds.example.com") == 0);
    free_data(&out);
    free(json);

    in.pds_url = NULL;
    assert(wf_session_data_to_json(&in, &json) == WF_OK);
    assert(wf_session_data_from_json(json, strlen(json), &out) == WF_OK);
    assert(out.pds_url == NULL);
    free_data(&out);
    free(json);
}

static void test_session_data_json_rejects_bad_input(void) {
    wf_session_data out;
    char *json = NULL;
    wf_session_data in = {0};

    assert(wf_session_data_to_json(&in, &json) == WF_ERR_INVALID_ARG);
    assert(wf_session_data_to_json(NULL, &json) == WF_ERR_INVALID_ARG);
    assert(wf_session_data_from_json(NULL, 0, &out) == WF_ERR_INVALID_ARG);
    assert(wf_session_data_from_json("not json", 8, &out) == WF_ERR_PARSE);
    assert(out.access_jwt == NULL);
    const char *missing =
        "{\"accessJwt\":\"a\",\"handle\":\"h\",\"did\":\"d\"}";
    assert(wf_session_data_from_json(missing, strlen(missing), &out) ==
           WF_ERR_PARSE);
    const char *badpds =
        "{\"accessJwt\":\"a\",\"refreshJwt\":\"r\",\"handle\":\"h\","
        "\"did\":\"d\",\"pdsUrl\":5}";
    assert(wf_session_data_from_json(badpds, strlen(badpds), &out) ==
           WF_ERR_PARSE);
    assert(out.pds_url == NULL);
}

int main(void) {
    test_session_create_free();
    test_session_free_null();
    test_session_new_null_url();
    test_session_new_empty_url();
    test_session_login_null_args();
    test_session_data_json_roundtrip();
    test_session_data_json_rejects_bad_input();
    test_session_refresh_no_session();
    test_session_resume_invalid_data();
    test_session_get_no_session();
    test_session_delete_no_session();
    test_session_has_session_null();

    printf("session tests: all passed\n");
    return 0;
}
