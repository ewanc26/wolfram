/*
 * test_moderation_report_typed.c — offline unit tests for the
 * com.atproto.moderation.createReport typed parser and the agent wrapper's
 * argument validation. No network.
 */

#include "wolfram/moderation_report_typed.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

/* A representative createReport output record: a moderation report with a
 * uri, cid, and an arbitrary `value` subtree. */
static const char *k_report_json =
    "{"
    "\"uri\":\"at://did:plc:reporter/com.atproto.moderation.createReport/abc123\","
    "\"cid\":\"bafyreigh2xyz\","
    "\"value\":{"
    "\"id\":42,"
    "\"reasonType\":\"com.atproto.moderation.defs#reasonSpam\","
    "\"subject\":{\"uri\":\"at://did:plc:spam/app.bsky.feed.post/xyz\","
    "\"cid\":\"bafyreiabc\"},"
    "\"reportedBy\":\"did:plc:reporter\","
    "\"createdAt\":\"2024-01-01T00:00:00Z\""
    "}"
    "}";

int main(void) {
    /* ---- happy path: parse a full report record ---- */
    {
        wf_moderation_report_record r = {0};
        wf_status s = wf_moderation_report_record_parse(k_report_json,
                                                 strlen(k_report_json), &r);
        WF_CHECK(s == WF_OK);
        WF_CHECK(r.uri != NULL);
        WF_CHECK(r.cid != NULL);
        WF_CHECK(r.value != NULL);
        if (r.uri) {
            WF_CHECK(strcmp(r.uri,
                            "at://did:plc:reporter/com.atproto.moderation."
                            "createReport/abc123") == 0);
        }
        if (r.cid) {
            WF_CHECK(strcmp(r.cid, "bafyreigh2xyz") == 0);
        }
        if (r.value) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(r.value, "id");
            WF_CHECK(cJSON_IsNumber(id) && id->valueint == 42);
            cJSON *rt =
                cJSON_GetObjectItemCaseSensitive(r.value, "reasonType");
            WF_CHECK(cJSON_IsString(rt) && rt->valuestring &&
                     strcmp(rt->valuestring,
                            "com.atproto.moderation.defs#reasonSpam") == 0);
        }
        wf_moderation_report_record_free(&r);
        /* double-free is safe */
        wf_moderation_report_record_free(&r);
    }

    /* ---- NULL arguments rejected ---- */
    {
        wf_moderation_report_record r = {0};
        WF_CHECK(wf_moderation_report_record_parse(NULL, 0, &r) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_moderation_report_record_parse(k_report_json,
                                            strlen(k_report_json),
                                            NULL) == WF_ERR_INVALID_ARG);
    }

    /* ---- malformed JSON rejected ---- */
    {
        wf_moderation_report_record r = {0};
        WF_CHECK(wf_moderation_report_record_parse("{not json", 8, &r) ==
                 WF_ERR_PARSE);
        WF_CHECK(r.uri == NULL && r.cid == NULL && r.value == NULL);
    }

    /* ---- wrapper argument validation (returns before any network use) ---- */
    {
        wf_moderation_report_record out = {0};

        /* NULL agent */
        WF_CHECK(wf_agent_report(NULL, "at://x", NULL,
                                 "com.atproto.moderation.defs#reasonSpam",
                                 NULL, &out) == WF_ERR_INVALID_ARG);

        /* empty subject_uri */
        WF_CHECK(wf_agent_report((wf_agent *)1, "", NULL,
                                 "com.atproto.moderation.defs#reasonSpam",
                                 NULL, &out) == WF_ERR_INVALID_ARG);

        /* NULL reason_type */
        WF_CHECK(wf_agent_report((wf_agent *)1, "at://x", NULL, NULL, NULL,
                                 &out) == WF_ERR_INVALID_ARG);

        /* empty reason_type */
        WF_CHECK(wf_agent_report((wf_agent *)1, "at://x", NULL, "",
                                 NULL, &out) == WF_ERR_INVALID_ARG);

        /* NULL out */
        WF_CHECK(wf_agent_report((wf_agent *)1, "at://x", NULL,
                                 "com.atproto.moderation.defs#reasonSpam",
                                 NULL, NULL) == WF_ERR_INVALID_ARG);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
