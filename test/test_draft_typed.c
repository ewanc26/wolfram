/*
 * test_draft_typed.c — offline unit tests for the app.bsky.draft typed parser
 * and wrapper argument validation. No network.
 */

#include "wolfram/draft_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* getDrafts -> "drafts" array of {uri, value:<record>, createdAt} */
static const char *k_drafts_json =
    "{\"drafts\":["
    "{\"uri\":\"at://did:plc:abc/app.bsky.draft/3k2xyz\",\""
    "createdAt\":\"2026-01-01T00:00:00Z\","
    "\"value\":{\"text\":\"hello world\",\"langs\":[\"en\"],"
    "\"$type\":\"app.bsky.draft.defs#draft\"}},"
    "{\"uri\":\"at://did:plc:abc/app.bsky.draft/3k2zzz\","
    "\"createdAt\":\"2026-01-02T00:00:00Z\","
    "\"value\":{\"text\":\"bonjour\",\"langs\":[\"fr\",\"en\"]}}"
    "],\"cursor\":\"next\"}";

int main(void) {
    /* ---- parse getDrafts ---- */
    {
        wf_draft_list out = {0};
        wf_status s = wf_draft_parse_list(k_drafts_json, strlen(k_drafts_json),
                                          &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.count == 2);
        WF_CHECK(out.items != NULL);
        WF_CHECK(out.items[0].uri &&
                 strcmp(out.items[0].uri,
                        "at://did:plc:abc/app.bsky.draft/3k2xyz") == 0);
        WF_CHECK(out.items[0].created_at &&
                 strcmp(out.items[0].created_at, "2026-01-01T00:00:00Z") == 0);
        WF_CHECK(out.items[0].text &&
                 strcmp(out.items[0].text, "hello world") == 0);
        WF_CHECK(out.items[0].lang_count == 1);
        WF_CHECK(out.items[0].langs &&
                 strcmp(out.items[0].langs[0], "en") == 0);
        WF_CHECK(out.items[0].record != NULL);
        WF_CHECK(out.items[1].text &&
                 strcmp(out.items[1].text, "bonjour") == 0);
        WF_CHECK(out.items[1].lang_count == 2);
        WF_CHECK(out.items[1].langs &&
                 strcmp(out.items[1].langs[1], "en") == 0);
        WF_CHECK(out.cursor && strcmp(out.cursor, "next") == 0);
        wf_draft_list_free(&out);
        /* double-free / freed-list must be safe */
        wf_draft_list_free(&out);
    }

    /* ---- defs#draftView wire shape (id/draft variants) ---- */
    {
        const char *json =
            "{\"drafts\":[{\"id\":\"3k2xyz\","
            "\"createdAt\":\"2026-03-03T00:00:00Z\","
            "\"updatedAt\":\"2026-03-04T00:00:00Z\","
            "\"draft\":{\"posts\":[{\"text\":\"nested\"}],"
            "\"langs\":[\"de\"]}}]}";
        wf_draft_list out = {0};
        wf_status s = wf_draft_parse_list(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.count == 1);
        WF_CHECK(out.items[0].uri &&
                 strcmp(out.items[0].uri, "3k2xyz") == 0);
        WF_CHECK(out.items[0].text &&
                 strcmp(out.items[0].text, "nested") == 0);
        WF_CHECK(out.items[0].lang_count == 1);
        WF_CHECK(out.items[0].langs &&
                 strcmp(out.items[0].langs[0], "de") == 0);
        wf_draft_list_free(&out);
    }

    /* ---- input validation ---- */
    {
        wf_draft_list out = {0};
        WF_CHECK(wf_draft_parse_list(NULL, 0, &out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_draft_parse_list("not json{", 9, &out) == WF_ERR_PARSE);
        WF_CHECK(wf_draft_parse_list("{\"drafts\":\"x\"}", 15, &out) ==
                 WF_ERR_PARSE);
        WF_CHECK(wf_draft_parse_list("{}", 2, &out) == WF_ERR_PARSE);

        wf_agent *agent_dummy = (wf_agent *)1;
        char *uri = NULL;
        WF_CHECK(wf_agent_create_draft(NULL, "{}", &uri) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_draft(agent_dummy, NULL, &uri) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_draft(agent_dummy, "", &uri) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_update_draft(NULL, "id", "{}", &uri) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_update_draft(agent_dummy, NULL, "{}", &uri) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_update_draft(agent_dummy, "id", NULL, &uri) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_delete_draft(NULL, "id") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_delete_draft(agent_dummy, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_delete_draft(agent_dummy, "") == WF_ERR_INVALID_ARG);
        wf_draft_list out2 = {0};
        WF_CHECK(wf_agent_get_drafts_typed(NULL, 10, NULL, &out2) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_drafts_typed(agent_dummy, 10, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_draft_list_free(&out);
        wf_draft_list_free(&out2);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
