/*
 * test_ageassurance_typed.c — offline tests for the app.bsky.ageassurance
 * typed parsers (begin / getConfig / getState) and agent wrapper input
 * validation. No network access; representative JSON bodies are hardcoded.
 */

#include "wolfram/ageassurance_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

/* begin output is a defs#state object. */
static const char *k_begin_json =
    "{\"lastInitiatedAt\":\"2026-07-08T10:00:00.000Z\","
    "\"status\":\"assured\",\"access\":\"full\"}";

/* getConfig output is a defs#config object with a per-region array. */
static const char *k_config_json =
    "{\"regions\":["
    "{\"countryCode\":\"US\",\"minAccessAge\":13,"
    "\"rules\":[{\"access\":\"safe\"}]},"
    "{\"countryCode\":\"GB\",\"regionCode\":\"ENG\",\"minAccessAge\":16,"
    "\"additionalVerificationMethods\":[\"device\"],"
    "\"rules\":[{\"access\":\"full\"}]}"
    "]}";

/* getState output is { state: defs#state, metadata: defs#stateMetadata }. */
static const char *k_state_json =
    "{\"state\":{\"status\":\"pending\",\"access\":\"none\"},"
    "\"metadata\":{\"accountCreatedAt\":\"2024-01-02T03:04:05.000Z\"}}";

int main(void) {
    /* --- invalid / NULL inputs --- */
    wf_ageassurance_begin b = {0};
    wf_ageassurance_config c = {0};
    wf_ageassurance_state s = {0};
    WF_CHECK(wf_ageassurance_parse_begin(NULL, 0, &b) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_ageassurance_parse_begin(k_begin_json, strlen(k_begin_json),
                                         NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_ageassurance_parse_config(NULL, 0, &c) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_ageassurance_parse_config(k_config_json, strlen(k_config_json),
                                          NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_ageassurance_parse_state(NULL, 0, &s) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_ageassurance_parse_state(k_state_json, strlen(k_state_json),
                                         NULL) == WF_ERR_INVALID_ARG);

    /* --- malformed JSON --- */
    WF_CHECK(wf_ageassurance_parse_begin("{not json", 9, &b) == WF_ERR_PARSE);
    WF_CHECK(wf_ageassurance_parse_config("[]", 2, &c) == WF_ERR_PARSE);
    WF_CHECK(wf_ageassurance_parse_state("{\"state\":1}", 11, &s) ==
             WF_ERR_PARSE);

    /* --- begin (defs#state) --- */
    WF_CHECK(wf_ageassurance_parse_begin(k_begin_json, strlen(k_begin_json),
                                         &b) == WF_OK);
    WF_CHECK(b.status && strcmp(b.status, "assured") == 0);
    WF_CHECK(b.access && strcmp(b.access, "full") == 0);
    WF_CHECK(b.last_initiated_at &&
             strcmp(b.last_initiated_at, "2026-07-08T10:00:00.000Z") == 0);
    wf_ageassurance_begin_free(&b);
    WF_CHECK(b.status == NULL && b.access == NULL &&
             b.last_initiated_at == NULL);

    /* --- getConfig (defs#config) --- */
    WF_CHECK(wf_ageassurance_parse_config(k_config_json, strlen(k_config_json),
                                          &c) == WF_OK);
    WF_CHECK(c.regions != NULL && cJSON_IsArray(c.regions));
    WF_CHECK(cJSON_GetArraySize(c.regions) == 2);
    wf_ageassurance_config_free(&c);
    WF_CHECK(c.regions == NULL);

    /* --- getState ({ state, metadata }) --- */
    WF_CHECK(wf_ageassurance_parse_state(k_state_json, strlen(k_state_json),
                                         &s) == WF_OK);
    WF_CHECK(s.state.status && strcmp(s.state.status, "pending") == 0);
    WF_CHECK(s.state.access && strcmp(s.state.access, "none") == 0);
    WF_CHECK(s.metadata.account_created_at &&
             strcmp(s.metadata.account_created_at,
                    "2024-01-02T03:04:05.000Z") == 0);
    wf_ageassurance_state_free(&s);
    WF_CHECK(s.state.status == NULL && s.metadata.account_created_at == NULL);

    /* --- begin with absent optional field still parses --- */
    wf_ageassurance_begin b2 = {0};
    WF_CHECK(wf_ageassurance_parse_begin("{\"status\":\"unknown\","
                                         "\"access\":\"unknown\"}",
                                         strlen("{\"status\":\"unknown\","
                                                "\"access\":\"unknown\"}"),
                                         &b2) == WF_OK);
    WF_CHECK(b2.status && strcmp(b2.status, "unknown") == 0);
    WF_CHECK(b2.last_initiated_at == NULL);
    wf_ageassurance_begin_free(&b2);

    WF_TEST_SUMMARY();
}
