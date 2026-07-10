/*
 * test_draft_typed.c — offline unit tests for the app.bsky.draft typed parser
 * and wrapper argument validation. No network.
 */

#include "wolfram/draft_typed.h"
#include "wolfram/atproto_lex.h"
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
        WF_CHECK(wf_agent_get_drafts_typed(agent_dummy, 101, NULL, &out2) ==
                 WF_ERR_INVALID_ARG);

        wf_draft_list_free(&out);
        wf_draft_list_free(&out2);
    }

    /* ---- createDraft / updateDraft / deleteDraft output parsing ---- */
    {
        /* createDraft returns {id} */
        const char *create_json = "{\"id\":\"3k9create\"}";
        wf_draft_createDraft_result cr = {0};
        wf_status s = wf_draft_createDraft_parse(create_json,
                                                 strlen(create_json), &cr);
        WF_CHECK(s == WF_OK);
        WF_CHECK(cr.id && strcmp(cr.id, "3k9create") == 0);
        wf_draft_createDraft_result_free(&cr);
        WF_CHECK(cr.id == NULL);

        /* empty body for createDraft still decodes (id left NULL) */
        wf_draft_createDraft_result cr2 = {0};
        WF_CHECK(wf_draft_createDraft_parse("", 0, &cr2) == WF_OK);
        WF_CHECK(cr2.id == NULL);
        wf_draft_createDraft_result_free(&cr2);

        /* updateDraft / deleteDraft return no body -> ok=true */
        wf_draft_updateDraft_result ur = {0};
        WF_CHECK(wf_draft_updateDraft_parse("", 0, &ur) == WF_OK);
        WF_CHECK(ur.ok == true);
        wf_draft_updateDraft_result_free(&ur);

        wf_draft_deleteDraft_result dr = {0};
        WF_CHECK(wf_draft_deleteDraft_parse("{}", 2, &dr) == WF_OK);
        WF_CHECK(dr.ok == true);
        wf_draft_deleteDraft_result_free(&dr);

        /* malformed (non-empty) body fails to parse */
        WF_CHECK(wf_draft_deleteDraft_parse("{bad", 4, &dr) == WF_ERR_PARSE);

        /* round-trip: encode a deleteDraft input then parse it back */
        wf_lex_app_bsky_draft_delete_draft_main_input din = {0};
        din.id = "3k9del";
        char *djson = NULL;
        wf_status es = wf_lex_app_bsky_draft_delete_draft_main_input_encode_json(
            &din, &djson);
        WF_CHECK(es == WF_OK);
        WF_CHECK(djson != NULL);
        if (djson) {
            wf_draft_deleteDraft_result dr2 = {0};
            WF_CHECK(wf_draft_deleteDraft_parse(djson, strlen(djson), &dr2) ==
                     WF_OK);
            WF_CHECK(dr2.ok == true);
            wf_draft_deleteDraft_result_free(&dr2);
            wf_lex_app_bsky_draft_delete_draft_main_json_free(djson);
        }
    }

    /* ---- typed write-wrapper argument validation ---- */
    {
        wf_agent *agent_dummy = (wf_agent *)1;

        wf_draft_createDraft_result cr = {0};
        WF_CHECK(wf_agent_draft_createDraft_typed(NULL, "{}", &cr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_createDraft_typed(agent_dummy, NULL, &cr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_createDraft_typed(agent_dummy, "", &cr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_createDraft_typed(agent_dummy, "{", NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_draft_createDraft_result_free(&cr);

        wf_draft_updateDraft_result ur = {0};
        WF_CHECK(wf_agent_draft_updateDraft_typed(NULL, "id", "{}", &ur) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_updateDraft_typed(agent_dummy, NULL, "{}", &ur) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_updateDraft_typed(agent_dummy, "id", NULL, &ur) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_updateDraft_typed(agent_dummy, "id", "{}",
                                                  NULL) == WF_ERR_INVALID_ARG);
        wf_draft_updateDraft_result_free(&ur);

        wf_draft_deleteDraft_result dr = {0};
        WF_CHECK(wf_agent_draft_deleteDraft_typed(NULL, "id", &dr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_deleteDraft_typed(agent_dummy, NULL, &dr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_deleteDraft_typed(agent_dummy, "", &dr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_draft_deleteDraft_typed(agent_dummy, "id", NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_draft_deleteDraft_result_free(&dr);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
