/*
 * test_ozone_typed.c — offline tests for the tools.ozone.* typed wrappers.
 *
 * Hardcodes representative response bodies (built with cJSON), asserts the
 * hand-written ergonomic parsers and the generated-decode path populate their
 * owned structs correctly, then frees them. Agent wrappers require live auth
 * and are exercised only for NULL-argument validation.
 */

#include "wolfram/ozone_typed.h"
#include "wolfram/atproto_lex.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a tools.ozone.moderation.queryStatuses body via cJSON. */
static char *build_query_statuses_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "subjectStatuses", arr);

    cJSON *s = cJSON_CreateObject();
    cJSON_AddNumberToObject(s, "id", 7);
    cJSON_AddStringToObject(s, "subject",
                            "at://did:plc:abc/app.bsky.feed.post/xyz");
    cJSON_AddStringToObject(s, "reviewState", "reviewOpen");
    cJSON_AddStringToObject(s, "createdAt", "2026-01-01T00:00:00.000Z");
    cJSON_AddStringToObject(s, "updatedAt", "2026-01-02T00:00:00.000Z");
    cJSON_AddBoolToObject(s, "takendown", 0);
    cJSON_AddItemToArray(arr, s);

    cJSON_AddStringToObject(root, "cursor", "cursor-1");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Build a tools.ozone.team.listMembers body via cJSON. */
static char *build_team_members_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "members", arr);

    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "did", "did:plc:team000000000000000000");
    cJSON_AddStringToObject(m, "role",
                            "tools.ozone.team.defs#moderator");
    cJSON_AddBoolToObject(m, "disabled", 0);
    cJSON_AddStringToObject(m, "createdAt", "2026-01-01T00:00:00.000Z");

    cJSON *profile = cJSON_CreateObject();
    cJSON_AddStringToObject(profile, "handle", "mod.example.com");
    cJSON_AddItemToObject(m, "profile", profile);

    cJSON_AddItemToArray(arr, m);
    cJSON_AddStringToObject(root, "cursor", "team-cursor-1");

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int main(void) {
    /* ---- hand-written ergonomic: subject statuses ---- */
    {
        char *json = build_query_statuses_json();
        WF_CHECK(json != NULL);
        wf_ozone_subject_status_list list = {0};
        wf_status st = wf_ozone_parse_subject_statuses(json, strlen(json), &list);
        free(json);

        WF_CHECK(st == WF_OK);
        WF_CHECK(list.status_count == 1);
        WF_CHECK(list.statuses != NULL);
        if (list.statuses) {
            WF_CHECK(list.statuses[0].has_id);
            WF_CHECK(list.statuses[0].id == 7);
            WF_CHECK(list.statuses[0].subject != NULL);
            WF_CHECK(strcmp(list.statuses[0].subject,
                            "at://did:plc:abc/app.bsky.feed.post/xyz") == 0);
            WF_CHECK(list.statuses[0].review_state != NULL);
            WF_CHECK(strcmp(list.statuses[0].review_state, "reviewOpen") == 0);
            WF_CHECK(list.statuses[0].has_takendown);
            WF_CHECK(list.statuses[0].takendown == false);
            WF_CHECK(list.statuses[0].extra != NULL);
        }
        WF_CHECK(list.cursor != NULL);
        WF_CHECK(strcmp(list.cursor, "cursor-1") == 0);
        wf_ozone_subject_status_list_free(&list);
    }

    /* ---- hand-written ergonomic: team members ---- */
    {
        char *json = build_team_members_json();
        WF_CHECK(json != NULL);
        wf_ozone_team_member_list list = {0};
        wf_status st = wf_ozone_parse_team_members(json, strlen(json), &list);
        free(json);

        WF_CHECK(st == WF_OK);
        WF_CHECK(list.member_count == 1);
        WF_CHECK(list.members != NULL);
        if (list.members) {
            WF_CHECK(list.members[0].did != NULL);
            WF_CHECK(strcmp(list.members[0].did,
                            "did:plc:team000000000000000000") == 0);
            WF_CHECK(list.members[0].role != NULL);
            WF_CHECK(strcmp(list.members[0].role,
                            "tools.ozone.team.defs#moderator") == 0);
            WF_CHECK(list.members[0].has_disabled);
            WF_CHECK(list.members[0].disabled == false);
            WF_CHECK(list.members[0].profile_handle != NULL);
            WF_CHECK(strcmp(list.members[0].profile_handle,
                            "mod.example.com") == 0);
            WF_CHECK(list.members[0].extra != NULL);
        }
        WF_CHECK(list.cursor != NULL);
        WF_CHECK(strcmp(list.cursor, "team-cursor-1") == 0);
        wf_ozone_team_member_list_free(&list);
    }

    /* ---- generated-decode path: queryStatuses ---- */
    {
        char *json = build_query_statuses_json();
        wf_lex_tools_ozone_moderation_query_statuses_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_moderation_queryStatuses(json, strlen(json), &out);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->subject_statuses.count == 1);
            WF_CHECK(out->subject_statuses.items != NULL);
            if (out->subject_statuses.items) {
                WF_CHECK(out->subject_statuses.items[0]->id == 7);
                WF_CHECK(out->subject_statuses.items[0]->review_state != NULL);
            }
            WF_CHECK(out->has_cursor);
            wf_lex_tools_ozone_moderation_query_statuses_main_output_free(out);
        }
    }

    /* ---- generated-decode path: team listMembers ---- */
    {
        /* Minimal body: the generated decoder requires a complete nested
         * `profile` (profileViewDetailed) when present, so omit it here and
         * exercise the hand-written profile handle extraction separately. */
        const char *json =
            "{\"members\":[{\"did\":\"did:plc:team000000000000000000\","
            "\"role\":\"tools.ozone.team.defs#moderator\",\"disabled\":false,"
            "\"createdAt\":\"2026-01-01T00:00:00.000Z\"}],"
            "\"cursor\":\"team-cursor-1\"}";
        wf_lex_tools_ozone_team_list_members_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_team_listMembers(json, strlen(json), &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->members.count == 1);
            WF_CHECK(out->members.items != NULL);
            if (out->members.items) {
                WF_CHECK(out->members.items[0]->did != NULL);
                WF_CHECK(out->members.items[0]->role != NULL);
            }
            wf_lex_tools_ozone_team_list_members_main_output_free(out);
        }
    }

    /* ---- agent wrappers: NULL-argument validation ---- */
    {
        wf_agent *agent = wf_agent_new("https://mod.example.com");
        WF_CHECK(agent != NULL);

        wf_lex_tools_ozone_moderation_query_statuses_main_output *out = NULL;
        WF_CHECK(wf_ozone_moderation_queryStatuses(
                     NULL, NULL, &out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_moderation_queryStatuses(
                     agent, NULL, &out) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_team_list_members_main_output *tout = NULL;
        WF_CHECK(wf_ozone_team_listMembers(
                     agent, NULL, &tout) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_communication_list_templates_main_output *c = NULL;
        WF_CHECK(wf_ozone_communication_listTemplates(
                     NULL, &c) == WF_ERR_INVALID_ARG);

        wf_response r = {0};
        WF_CHECK(wf_ozone_moderation_emitEvent(
                     NULL, NULL, &r) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_moderation_emitEvent(
                     agent, NULL, &r) == WF_ERR_INVALID_ARG);

        char *js = NULL;
        WF_CHECK(wf_ozone_moderation_get_suggestions(
                     NULL, NULL, 0, 0, NULL, &js) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_moderation_get_label_definitions(
                     NULL, NULL, 0, &js) == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
