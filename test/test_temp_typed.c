/*
 * test_temp_typed.c — offline unit tests for the com.atproto.temp typed
 * parsers and agent-wrapper argument validation. No network.
 */

#include "wolfram/temp_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* checkHandleAvailability: available case (resultAvailable union). */
static const char *k_handle_available_json =
    "{\"handle\":\"alice.example.com\","
     "\"result\":{\"$type\":\"com.atproto.temp.checkHandleAvailability#resultAvailable\"}}";

/* checkHandleAvailability: unavailable case (resultUnavailable + suggestions). */
static const char *k_handle_unavailable_json =
    "{\"handle\":\"taken.example.com\","
     "\"result\":{\"$type\":\"com.atproto.temp.checkHandleAvailability#resultUnavailable\","
     "\"suggestions\":[{\"handle\":\"taken-1\",\"method\":\"m1\"},"
     "{\"handle\":\"taken-2\",\"method\":\"m2\"}]}}";

/* checkSignupQueue. */
static const char *k_signup_json =
    "{\"activated\":true,\"placeInQueue\":42,\"estimatedTimeMs\":9000}";

/* fetchLabels. */
static const char *k_labels_json =
    "{\"labels\":[{\"uri\":\"at://did:plc:x\",\"val\":\"spam\"},"
     "{\"uri\":\"at://did:plc:y\",\"val\":\"nudge\"}]}";

/* dereferenceScope. */
static const char *k_scope_json =
    "{\"scope\":\"ref:xyz\"}";

int main(void) {
    /* ---- checkHandleAvailability: available ---- */
    {
        wf_temp_check_handle_availability out = {0};
        wf_status s = wf_temp_check_handle_availability_parse(
            k_handle_available_json, strlen(k_handle_available_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.available == 1);
        WF_CHECK(out.handle && strcmp(out.handle, "alice.example.com") == 0);
        WF_CHECK(out.suggestion_count == 0);
        WF_CHECK(out.raw_result != NULL);
        wf_temp_check_handle_availability_free(&out);
    }

    /* ---- checkHandleAvailability: unavailable with suggestions ---- */
    {
        wf_temp_check_handle_availability out = {0};
        wf_status s = wf_temp_check_handle_availability_parse(
            k_handle_unavailable_json, strlen(k_handle_unavailable_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.available == 0);
        WF_CHECK(out.suggestion_count == 2);
        WF_CHECK(out.suggestions &&
                 strcmp(out.suggestions[0].handle, "taken-1") == 0);
        WF_CHECK(out.suggestions &&
                 strcmp(out.suggestions[0].method, "m1") == 0);
        WF_CHECK(out.suggestions &&
                 strcmp(out.suggestions[1].handle, "taken-2") == 0);
        wf_temp_check_handle_availability_free(&out);
    }

    /* ---- checkSignupQueue ---- */
    {
        wf_temp_check_signup_queue out = {0};
        wf_status s = wf_temp_check_signup_queue_parse(
            k_signup_json, strlen(k_signup_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.activated == 1);
        WF_CHECK(out.has_place_in_queue == 1);
        WF_CHECK(out.place_in_queue == 42);
        WF_CHECK(out.has_estimated_time_ms == 1);
        WF_CHECK(out.estimated_time_ms == 9000);
        wf_temp_check_signup_queue_free(&out);
    }

    /* ---- checkSignupQueue: missing required activated => error ---- */
    {
        wf_temp_check_signup_queue out = {0};
        wf_status s = wf_temp_check_signup_queue_parse(
            "{\"placeInQueue\":1}", strlen("{\"placeInQueue\":1}"), &out);
        WF_CHECK(s == WF_ERR_PARSE);
        wf_temp_check_signup_queue_free(&out);
    }

    /* ---- fetchLabels ---- */
    {
        wf_temp_fetch_labels out = {0};
        wf_status s = wf_temp_fetch_labels_parse(
            k_labels_json, strlen(k_labels_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.labels && cJSON_IsArray(out.labels));
        WF_CHECK(out.labels &&
                 cJSON_GetArraySize(out.labels) == 2);
        wf_temp_fetch_labels_free(&out);
    }

    /* ---- fetchLabels: missing labels array => error ---- */
    {
        wf_temp_fetch_labels out = {0};
        wf_status s = wf_temp_fetch_labels_parse(
            "{\"foo\":1}", strlen("{\"foo\":1}"), &out);
        WF_CHECK(s == WF_ERR_PARSE);
        wf_temp_fetch_labels_free(&out);
    }

    /* ---- dereferenceScope ---- */
    {
        wf_temp_dereference_scope out = {0};
        wf_status s = wf_temp_dereference_scope_parse(
            k_scope_json, strlen(k_scope_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.scope && strcmp(out.scope, "ref:xyz") == 0);
        wf_temp_dereference_scope_free(&out);
    }

    /* ---- dereferenceScope: missing scope => error ---- */
    {
        wf_temp_dereference_scope out = {0};
        wf_status s = wf_temp_dereference_scope_parse(
            "{}", strlen("{}"), &out);
        WF_CHECK(s == WF_ERR_PARSE);
        wf_temp_dereference_scope_free(&out);
    }

    /* ---- NULL/invalid parser inputs => WF_ERR_INVALID_ARG ---- */
    {
        wf_temp_check_handle_availability a = {0};
        WF_CHECK(wf_temp_check_handle_availability_parse(NULL, 0, &a) ==
                 WF_ERR_INVALID_ARG);
        wf_temp_check_signup_queue b = {0};
        WF_CHECK(wf_temp_check_signup_queue_parse(NULL, 0, &b) ==
                 WF_ERR_INVALID_ARG);
        wf_temp_fetch_labels c = {0};
        WF_CHECK(wf_temp_fetch_labels_parse(NULL, 0, &c) ==
                 WF_ERR_INVALID_ARG);
        wf_temp_dereference_scope d = {0};
        WF_CHECK(wf_temp_dereference_scope_parse(NULL, 0, &d) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- Agent wrapper argument validation (no network) ---- */
    {
        int avail = 0;
        WF_CHECK(wf_agent_check_handle_availability(NULL, "h", &avail) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_check_handle_availability(NULL, "", &avail) ==
                 WF_ERR_INVALID_ARG);

        int act = 0, closed = 0;
        char *place = NULL;
        WF_CHECK(wf_agent_check_signup_queue(NULL, &act, &closed, &place) ==
                 WF_ERR_INVALID_ARG);

        cJSON *labels = NULL;
        WF_CHECK(wf_agent_fetch_labels(NULL, NULL, 0, &labels) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_request_phone_verification(NULL, "123", NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_phone_verification(NULL, "", NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_add_reserved_handle(NULL, "h") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_add_reserved_handle(NULL, "") ==
                 WF_ERR_INVALID_ARG);

        char *did = NULL;
        WF_CHECK(wf_agent_dereference_scope(NULL, "s", &did) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_dereference_scope(NULL, "", &did) ==
                 WF_ERR_INVALID_ARG);
    }

    WF_TEST_SUMMARY();
}
