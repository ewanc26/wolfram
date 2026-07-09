/*
 * test_ozone_admin_typed.c — offline tests for the tools.ozone.moderation.*
 * admin typed wrappers (the endpoints not covered by test_ozone_typed.c).
 *
 * Builds representative response bodies with cJSON, asserts the generated-decode
 * parse path populates the owned structs correctly, then frees them. Agent
 * wrappers require live service auth and are exercised only for NULL-argument
 * validation (which is checked before any network I/O).
 */

#include "wolfram/ozone_admin_typed.h"
#include "wolfram/atproto_lex.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a tools.ozone.moderation.getRecords body via cJSON. The `records`
 * array is decoded by the generated decoder as raw wf_lex_json (cJSON*). */
static char *build_get_records_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "records", arr);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "did", "did:plc:repo000000000000000000");
    cJSON_AddStringToObject(r, "handle", "repo.example.com");
    cJSON_AddItemToArray(arr, r);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Build a tools.ozone.moderation.getRepos body via cJSON. */
static char *build_get_repos_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "repos", arr);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "did", "did:plc:repo000000000000000000");
    cJSON_AddStringToObject(r, "handle", "repo.example.com");
    cJSON_AddItemToArray(arr, r);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Build a tools.ozone.moderation.getAccountTimeline body via cJSON. */
static char *build_get_account_timeline_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "timeline", arr);

    cJSON *day = cJSON_CreateObject();
    cJSON_AddStringToObject(day, "day", "2026-01-01");
    cJSON *sum = cJSON_CreateArray();
    cJSON_AddItemToObject(day, "summary", sum);
    cJSON *s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "eventSubjectType", "account");
    cJSON_AddStringToObject(s, "eventType", "tools.ozone.moderation.defs#modEventTakedown");
    cJSON_AddNumberToObject(s, "count", 3);
    cJSON_AddItemToArray(sum, s);
    cJSON_AddItemToArray(arr, day);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Build a tools.ozone.moderation.searchRepos body via cJSON. */
static char *build_search_repos_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cursor", "repo-cursor-1");
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "repos", arr);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "did", "did:plc:srch000000000000000000");
    cJSON_AddStringToObject(r, "handle", "srch.example.com");
    /* repo_view requires relatedRecords (array), indexedAt (string) and
     * moderation (object); the latter two have only optional contents. */
    cJSON_AddItemToObject(r, "relatedRecords", cJSON_CreateArray());
    cJSON_AddStringToObject(r, "indexedAt", "2026-01-01T00:00:00.000Z");
    cJSON_AddItemToObject(r, "moderation", cJSON_CreateObject());
    cJSON_AddItemToArray(arr, r);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Build a tools.ozone.moderation.listScheduledActions body via cJSON. */
static char *build_list_scheduled_actions_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cursor", "sched-cursor-1");
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "actions", arr);

    cJSON *a = cJSON_CreateObject();
    cJSON_AddNumberToObject(a, "id", 11);
    cJSON_AddStringToObject(a, "action", "tools.ozone.moderation.defs#modEventTakedown");
    cJSON_AddStringToObject(a, "did", "did:plc:sched000000000000000000");
    cJSON_AddStringToObject(a, "createdBy", "did:plc:mod000000000000000000");
    cJSON_AddStringToObject(a, "createdAt", "2026-01-01T00:00:00.000Z");
    cJSON_AddStringToObject(a, "status", "pending");
    cJSON_AddItemToArray(arr, a);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int main(void) {
    /* ---- generated-decode path: getRecords ---- */
    {
        char *json = build_get_records_json();
        WF_CHECK(json != NULL);
        wf_lex_tools_ozone_moderation_get_records_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_moderation_getRecords(json, strlen(json), &out);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->records.count == 1);
            WF_CHECK(out->records.items != NULL);
            if (out->records.items) {
                WF_CHECK(out->records.items[0].data != NULL);
            }
            wf_lex_tools_ozone_moderation_get_records_main_output_free(out);
        }
    }

    /* ---- generated-decode path: getRepos ---- */
    {
        char *json = build_get_repos_json();
        WF_CHECK(json != NULL);
        wf_lex_tools_ozone_moderation_get_repos_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_moderation_getRepos(json, strlen(json), &out);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->repos.count == 1);
            WF_CHECK(out->repos.items != NULL);
            if (out->repos.items) {
                WF_CHECK(out->repos.items[0].data != NULL);
            }
            wf_lex_tools_ozone_moderation_get_repos_main_output_free(out);
        }
    }

    /* ---- generated-decode path: getAccountTimeline ---- */
    {
        char *json = build_get_account_timeline_json();
        WF_CHECK(json != NULL);
        wf_lex_tools_ozone_moderation_get_account_timeline_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_moderation_getAccountTimeline(json, strlen(json), &out);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->timeline.count == 1);
            WF_CHECK(out->timeline.items != NULL);
            if (out->timeline.items) {
                WF_CHECK(strcmp(out->timeline.items[0]->day, "2026-01-01") == 0);
                WF_CHECK(out->timeline.items[0]->summary.count == 1);
                if (out->timeline.items[0]->summary.items) {
                    WF_CHECK(out->timeline.items[0]->summary.items[0]->count == 3);
                }
            }
            wf_lex_tools_ozone_moderation_get_account_timeline_main_output_free(out);
        }
    }

    /* ---- generated-decode path: searchRepos ---- */
    {
        char *json = build_search_repos_json();
        WF_CHECK(json != NULL);
        wf_lex_tools_ozone_moderation_search_repos_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_moderation_searchRepos(json, strlen(json), &out);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->has_cursor);
            WF_CHECK(out->repos.count == 1);
            WF_CHECK(out->repos.items != NULL);
            if (out->repos.items) {
                WF_CHECK(out->repos.items[0]->did != NULL);
            }
            wf_lex_tools_ozone_moderation_search_repos_main_output_free(out);
        }
    }

    /* ---- generated-decode path: listScheduledActions ---- */
    {
        char *json = build_list_scheduled_actions_json();
        WF_CHECK(json != NULL);
        wf_lex_tools_ozone_moderation_list_scheduled_actions_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_moderation_listScheduledActions(json, strlen(json), &out);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->has_cursor);
            WF_CHECK(out->actions.count == 1);
            WF_CHECK(out->actions.items != NULL);
            if (out->actions.items) {
                WF_CHECK(out->actions.items[0]->id == 11);
                WF_CHECK(out->actions.items[0]->did != NULL);
                WF_CHECK(strcmp(out->actions.items[0]->status, "pending") == 0);
            }
            wf_lex_tools_ozone_moderation_list_scheduled_actions_main_output_free(out);
        }
    }

    /* ---- parse NULL-argument validation ---- */
    {
        wf_lex_tools_ozone_moderation_get_records_main_output *out = NULL;
        WF_CHECK(wf_ozone_parse_moderation_getRecords(NULL, 0, &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_parse_moderation_getRecords("{}", 2, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_search_repos_main_output *so = NULL;
        WF_CHECK(wf_ozone_parse_moderation_searchRepos(NULL, 0, &so) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_list_scheduled_actions_main_output *lo = NULL;
        WF_CHECK(wf_ozone_parse_moderation_listScheduledActions(NULL, 0, &lo) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- agent wrappers: NULL-argument validation (offline) ---- */
    {
        wf_agent *agent = wf_agent_new("https://mod.example.com");
        WF_CHECK(agent != NULL);

        wf_lex_tools_ozone_moderation_get_records_main_output *o = NULL;
        WF_CHECK(wf_ozone_moderation_getRecords(
                     NULL, NULL, &o) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_moderation_getRecords(
                     agent, NULL, &o) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_get_repos_main_params rp = {0};
        WF_CHECK(wf_ozone_moderation_getRepos(
                     agent, &rp, NULL) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_search_repos_main_params sp = {0};
        WF_CHECK(wf_ozone_moderation_searchRepos(
                     agent, &sp, NULL) == WF_ERR_INVALID_ARG);

        wf_response r = {0};
        wf_lex_tools_ozone_moderation_get_record_main_params grp = {0};
        WF_CHECK(wf_ozone_moderation_getRecord(
                     NULL, &grp, &r) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_moderation_getRecord(
                     agent, NULL, &r) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_get_repo_main_params grp2 = {0};
        WF_CHECK(wf_ozone_moderation_getRepo(
                     agent, &grp2, NULL) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_list_scheduled_actions_main_output *lo = NULL;
        WF_CHECK(wf_ozone_moderation_listScheduledActions(
                     agent, NULL, &lo) == WF_ERR_INVALID_ARG);

        WF_CHECK(wf_ozone_moderation_cancelScheduledActions(
                     agent, NULL, &r) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_schedule_action_main_input si = {0};
        WF_CHECK(wf_ozone_moderation_scheduleAction(
                     NULL, &si, &r) == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
