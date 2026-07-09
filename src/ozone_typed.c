/*
 * ozone_typed.c — owned typed parsers + agent convenience wrappers for the
 * tools.ozone.* namespaces. See include/wolfram/ozone_typed.h for the public
 * API, the authoritative wire format, and ownership rules.
 *
 * The agent wrappers reuse the generated owning lex wrappers in
 * wolfram/atproto_lex.h (call + decode + free) after syncing auth onto the
 * agent's primary XRPC client. The two endpoints whose lexicon is missing
 * from the local snapshot (getSuggestions, getLabelDefinitions) fall back to
 * the existing ozone.c transport helpers and return owned raw JSON.
 *
 * The hand-written ergonomic list parsers follow the labeler_typed ownership
 * model: static strdup/set_string/reset helpers, owned strings, a detached
 * `extra` cJSON subtree for open/unbounded fields, and full cleanup on the
 * first error.
 */

#include "wolfram/ozone_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/ozone.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* ---- local string/reset helpers ---- */

static char *wf_ozone_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_ozone_set_string(char **dst, const char *src) {
    char *copy = wf_ozone_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static wf_status wf_ozone_set_string_array(cJSON *arr, char ***out_items,
                                          size_t *out_count) {
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(arr);
    if (n == 0) {
        *out_items = NULL;
        *out_count = 0;
        return WF_OK;
    }
    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n; ++i) {
        cJSON *it = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsString(it) || !it->valuestring) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_ozone_set_string(&items[i], it->valuestring);
    }
    if (status == WF_OK) {
        *out_items = items;
        *out_count = n;
    } else {
        for (size_t i = 0; i < n; ++i) {
            free(items[i]);
        }
        free(items);
        *out_items = NULL;
        *out_count = 0;
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Ergonomic: subject statuses (tools.ozone.moderation.queryStatuses)    */
/* ------------------------------------------------------------------ */

static void wf_ozone_subject_status_reset(wf_ozone_subject_status *s) {
    if (!s) {
        return;
    }
    free(s->subject);
    free(s->review_state);
    free(s->created_at);
    free(s->updated_at);
    free(s->comment);
    free(s->subject_repo_handle);
    for (size_t i = 0; i < s->tag_count; ++i) {
        free(s->tags[i]);
    }
    free(s->tags);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_parse_subject_status(cJSON *obj,
                                              wf_ozone_subject_status *s) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *subject = cJSON_GetObjectItemCaseSensitive(obj, "subject");
    cJSON *review = cJSON_GetObjectItemCaseSensitive(obj, "reviewState");
    cJSON *created = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
    cJSON *updated = cJSON_GetObjectItemCaseSensitive(obj, "updatedAt");
    cJSON *comment = cJSON_GetObjectItemCaseSensitive(obj, "comment");
    cJSON *score = cJSON_GetObjectItemCaseSensitive(obj, "priorityScore");
    cJSON *taken = cJSON_GetObjectItemCaseSensitive(obj, "takendown");
    cJSON *appealed = cJSON_GetObjectItemCaseSensitive(obj, "appealed");
    cJSON *tags = cJSON_GetObjectItemCaseSensitive(obj, "tags");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "subjectRepoHandle");

    if (cJSON_IsNumber(id)) {
        s->has_id = true;
        s->id = (int64_t)id->valuedouble;
    }
    if (cJSON_IsString(subject) && subject->valuestring) {
        status = wf_ozone_set_string(&s->subject, subject->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(review) && review->valuestring) {
        status = wf_ozone_set_string(&s->review_state, review->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created) && created->valuestring) {
        status = wf_ozone_set_string(&s->created_at, created->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(updated) && updated->valuestring) {
        status = wf_ozone_set_string(&s->updated_at, updated->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(comment) && comment->valuestring) {
        s->has_comment = true;
        status = wf_ozone_set_string(&s->comment, comment->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(score)) {
        s->has_priority_score = true;
        s->priority_score = (int64_t)score->valuedouble;
    }
    if (status == WF_OK && cJSON_IsBool(taken)) {
        s->has_takendown = true;
        s->takendown = cJSON_IsTrue(taken);
    }
    if (status == WF_OK && cJSON_IsBool(appealed)) {
        s->has_appealed = true;
        s->appealed = cJSON_IsTrue(appealed);
    }
    if (status == WF_OK && tags != NULL) {
        status = wf_ozone_set_string_array(tags, &s->tags, &s->tag_count);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_ozone_set_string(&s->subject_repo_handle,
                                     handle->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "id");
        cJSON_DetachItemFromObject(obj, "subject");
        cJSON_DetachItemFromObject(obj, "reviewState");
        cJSON_DetachItemFromObject(obj, "createdAt");
        cJSON_DetachItemFromObject(obj, "updatedAt");
        cJSON_DetachItemFromObject(obj, "comment");
        cJSON_DetachItemFromObject(obj, "priorityScore");
        cJSON_DetachItemFromObject(obj, "takendown");
        cJSON_DetachItemFromObject(obj, "appealed");
        cJSON_DetachItemFromObject(obj, "tags");
        cJSON_DetachItemFromObject(obj, "subjectRepoHandle");
        s->extra = obj;
    } else {
        wf_ozone_subject_status_reset(s);
    }
    return status;
}

wf_status wf_ozone_parse_subject_statuses(const char *json, size_t json_len,
                                          wf_ozone_subject_status_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "subjectStatuses");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_ozone_subject_status *items = NULL;
    if (count > 0) {
        items = (wf_ozone_subject_status *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_ozone_parse_subject_status(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_ozone_subject_status_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_ozone_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->statuses = items;
        out->status_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_ozone_subject_status_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_subject_status_list_free(wf_ozone_subject_status_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->status_count; ++i) {
        wf_ozone_subject_status_reset(&l->statuses[i]);
    }
    free(l->statuses);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ------------------------------------------------------------------ */
/* Ergonomic: team members (tools.ozone.team.listMembers)               */
/* ------------------------------------------------------------------ */

static void wf_ozone_team_member_reset(wf_ozone_team_member *m) {
    if (!m) {
        return;
    }
    free(m->did);
    free(m->role);
    free(m->created_at);
    free(m->updated_at);
    free(m->last_updated_by);
    free(m->profile_handle);
    if (m->extra) {
        cJSON_Delete(m->extra);
    }
    memset(m, 0, sizeof(*m));
}

static wf_status wf_ozone_parse_team_member(cJSON *obj,
                                           wf_ozone_team_member *m) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *disabled = cJSON_GetObjectItemCaseSensitive(obj, "disabled");
    cJSON *role = cJSON_GetObjectItemCaseSensitive(obj, "role");
    cJSON *created = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
    cJSON *updated = cJSON_GetObjectItemCaseSensitive(obj, "updatedAt");
    cJSON *by = cJSON_GetObjectItemCaseSensitive(obj, "lastUpdatedBy");
    cJSON *profile = cJSON_GetObjectItemCaseSensitive(obj, "profile");

    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_ozone_set_string(&m->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsBool(disabled)) {
        m->has_disabled = true;
        m->disabled = cJSON_IsTrue(disabled);
    }
    if (status == WF_OK && cJSON_IsString(role) && role->valuestring) {
        status = wf_ozone_set_string(&m->role, role->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created) && created->valuestring) {
        status = wf_ozone_set_string(&m->created_at, created->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(updated) && updated->valuestring) {
        status = wf_ozone_set_string(&m->updated_at, updated->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(by) && by->valuestring) {
        status = wf_ozone_set_string(&m->last_updated_by, by->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(profile)) {
        cJSON *ph = cJSON_GetObjectItemCaseSensitive(profile, "handle");
        if (cJSON_IsString(ph) && ph->valuestring) {
            status = wf_ozone_set_string(&m->profile_handle, ph->valuestring);
        }
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "did");
        cJSON_DetachItemFromObject(obj, "disabled");
        cJSON_DetachItemFromObject(obj, "role");
        cJSON_DetachItemFromObject(obj, "createdAt");
        cJSON_DetachItemFromObject(obj, "updatedAt");
        cJSON_DetachItemFromObject(obj, "lastUpdatedBy");
        cJSON_DetachItemFromObject(obj, "profile");
        m->extra = obj;
    } else {
        wf_ozone_team_member_reset(m);
    }
    return status;
}

wf_status wf_ozone_parse_team_members(const char *json, size_t json_len,
                                      wf_ozone_team_member_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "members");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_ozone_team_member *items = NULL;
    if (count > 0) {
        items = (wf_ozone_team_member *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_ozone_parse_team_member(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_ozone_team_member_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_ozone_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->members = items;
        out->member_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_ozone_team_member_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_team_member_list_free(wf_ozone_team_member_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->member_count; ++i) {
        wf_ozone_team_member_reset(&l->members[i]);
    }
    free(l->members);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ------------------------------------------------------------------ */
/* Generated-decode wrapper definitions                                */
/* ------------------------------------------------------------------ */

#define WF_OZONE_DEF_Q(ns, op, genop)                                       \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,      \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {            \
        if (!agent || !agent->client || !params || !out) {                 \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        *out = NULL;                                                       \
        wf_lex_tools_ozone_##ns##_##genop##_main_output *dec = NULL;       \
        wf_agent_sync_auth(agent);                                         \
        wf_response res = {0};                                             \
        wf_status st = wf_lex_tools_ozone_##ns##_##genop##_main_call(      \
            agent->client, params, &res);                                  \
        if (st != WF_OK) {                                                 \
            wf_response_free(&res);                                        \
            return st;                                                     \
        }                                                                  \
        st = wf_lex_tools_ozone_##ns##_##genop##_main_output_decode_json(  \
            res.body, res.body_len, &dec);                                 \
        wf_response_free(&res);                                           \
        if (st == WF_OK) {                                                 \
            *out = dec;                                                    \
        }                                                                  \
        return st;                                                         \
    }                                                                      \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {           \
        if (!json || !out) {                                               \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        return wf_lex_tools_ozone_##ns##_##genop##_main_output_decode_json( \
            json, json_len, out);                                          \
    }

#define WF_OZONE_DEF_Q0(ns, op, genop)                                      \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {            \
        if (!agent || !agent->client || !out) {                            \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        *out = NULL;                                                       \
        wf_lex_tools_ozone_##ns##_##genop##_main_output *dec = NULL;       \
        wf_agent_sync_auth(agent);                                         \
        wf_response res = {0};                                             \
        wf_status st = wf_lex_tools_ozone_##ns##_##genop##_main_call(      \
            agent->client, &res);                                          \
        if (st != WF_OK) {                                                 \
            wf_response_free(&res);                                        \
            return st;                                                     \
        }                                                                  \
        st = wf_lex_tools_ozone_##ns##_##genop##_main_output_decode_json(  \
            res.body, res.body_len, &dec);                                 \
        wf_response_free(&res);                                           \
        if (st == WF_OK) {                                                 \
            *out = dec;                                                    \
        }                                                                  \
        return st;                                                         \
    }                                                                      \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {           \
        if (!json || !out) {                                               \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        return wf_lex_tools_ozone_##ns##_##genop##_main_output_decode_json( \
            json, json_len, out);                                          \
    }

#define WF_OZONE_DEF_P(ns, op, genop)                                       \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,        \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {            \
        if (!agent || !agent->client || !input || !out) {                  \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        *out = NULL;                                                       \
        wf_lex_tools_ozone_##ns##_##genop##_main_output *dec = NULL;       \
        wf_agent_sync_auth(agent);                                         \
        wf_response res = {0};                                             \
        wf_status st = wf_lex_tools_ozone_##ns##_##genop##_main_call(      \
            agent->client, input, &res);                                   \
        if (st != WF_OK) {                                                 \
            wf_response_free(&res);                                        \
            return st;                                                     \
        }                                                                  \
        st = wf_lex_tools_ozone_##ns##_##genop##_main_output_decode_json(  \
            res.body, res.body_len, &dec);                                 \
        wf_response_free(&res);                                           \
        if (st == WF_OK) {                                                 \
            *out = dec;                                                    \
        }                                                                  \
        return st;                                                         \
    }                                                                      \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {           \
        if (!json || !out) {                                               \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        return wf_lex_tools_ozone_##ns##_##genop##_main_output_decode_json( \
            json, json_len, out);                                          \
    }

#define WF_OZONE_DEF_PR(ns, op, genop)                                      \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,        \
        wf_response *out) {                                                \
        if (!agent || !agent->client || !input || !out) {                  \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        wf_agent_sync_auth(agent);                                         \
        return wf_lex_tools_ozone_##ns##_##genop##_main_call(              \
            agent->client, input, out);                                     \
    }

#define WF_OZONE_DEF_QR(ns, op, genop)                                      \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,      \
        wf_response *out) {                                                \
        if (!agent || !agent->client || !params || !out) {                 \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        wf_agent_sync_auth(agent);                                         \
        return wf_lex_tools_ozone_##ns##_##genop##_main_call(              \
            agent->client, params, out);                                    \
    }

#define WF_OZONE_ENDPOINTS                                                  \
    X(moderation, queryStatuses, query_statuses, Q)                         \
    X(moderation, getSubjects, get_subjects, Q)                             \
    X(moderation, queryEvents, query_events, Q)                             \
    X(moderation, getReporterStats, get_reporter_stats, Q)                  \
    X(moderation, getEvent, get_event, QR)                                 \
    X(moderation, emitEvent, emit_event, PR)                               \
    X(setting, listOptions, list_options, Q)                               \
    X(setting, upsertOption, upsert_option, PR)                            \
    X(setting, removeOptions, remove_options, P)                           \
    X(communication, listTemplates, list_templates, Q0)                     \
    X(communication, createTemplate, create_template, PR)                   \
    X(communication, updateTemplate, update_template, PR)                   \
    X(communication, deleteTemplate, delete_template, PR)                  \
    X(team, listMembers, list_members, Q)                                   \
    X(team, addMember, add_member, PR)                                      \
    X(team, updateMember, update_member, PR)                               \
    X(team, deleteMember, delete_member, PR)                              \
    X(server, getConfig, get_config, Q0)                                   \
    X(verification, listVerifications, list_verifications, Q)               \
    X(verification, grantVerifications, grant_verifications, P)             \
    X(verification, revokeVerifications, revoke_verifications, P)           \
    X(signature, searchAccounts, search_accounts, Q)                       \
    X(signature, findRelatedAccounts, find_related_accounts, Q)            \
    X(signature, findCorrelation, find_correlation, Q)                     \
    X(hosting, getAccountHistory, get_account_history, Q)                   \
    X(set, querySets, query_sets, Q)                                        \
    X(set, getValues, get_values, Q)                                        \
    X(set, addValues, add_values, PR)                                       \
    X(set, deleteValues, delete_values, PR)                                \
    X(set, upsertSet, upsert_set, PR)                                       \
    X(set, deleteSet, delete_set, P)                                        \
    X(queue, listQueues, list_queues, Q)                                    \
    X(queue, getAssignments, get_assignments, Q)                            \
    X(queue, routeReports, route_reports, P)                               \
    X(queue, assignModerator, assign_moderator, PR)                         \
    X(queue, unassignModerator, unassign_moderator, PR)                     \
    X(queue, createQueue, create_queue, P)                                  \
    X(queue, updateQueue, update_queue, P)                                  \
    X(queue, deleteQueue, delete_queue, P)                                  \
    X(report, queryReports, query_reports, Q)                               \
    X(report, getReport, get_report, QR)                                    \
    X(report, getLatestReport, get_latest_report, Q)                        \
    X(report, getLiveStats, get_live_stats, Q)                             \
    X(report, getHistoricalStats, get_historical_stats, Q)                  \
    X(report, getAssignments, get_assignments, Q)                           \
    X(report, assignModerator, assign_moderator, PR)                        \
    X(report, unassignModerator, unassign_moderator, PR)                    \
    X(report, reassignQueue, reassign_queue, P)                             \
    X(report, queryActivities, query_activities, Q)                         \
    X(report, listActivities, list_activities, Q)                           \
    X(report, createActivity, create_activity, P)                           \
    X(report, refreshStats, refresh_stats, P)                              \
    X(safelink, queryRules, query_rules, P)                                \
    X(safelink, addRule, add_rule, PR)                                      \
    X(safelink, updateRule, update_rule, PR)                               \
    X(safelink, removeRule, remove_rule, PR)                               \
    X(safelink, queryEvents, query_events, P)

#define X(ns, op, genop, kind) WF_OZONE_DEF_##kind(ns, op, genop)
WF_OZONE_ENDPOINTS
#undef X

/* ------------------------------------------------------------------ */
/* JSON-in/JSON-out wrappers for missing lexicon endpoints              */
/* ------------------------------------------------------------------ */

/* TODO: tools.ozone.moderation.getSuggestions is NOT in the local atproto
 * lexicon snapshot, so no generated params/decoder exist. Reuses the existing
 * ozone.c transport helper and returns the raw response body as owned JSON. */
wf_status wf_ozone_moderation_get_suggestions(
    wf_agent *agent, const char *const *ignore_subjects, size_t n,
    int64_t limit, const char *cursor, char **out_json) {
    if (!agent || !agent->client || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status st = wf_ozone_get_suggestions(agent->client, ignore_subjects, n,
                                            limit, cursor, &res);
    if (st != WF_OK) {
        wf_response_free(&res);
        return st;
    }
    size_t len = res.body_len;
    char *cpy = (char *)malloc(len ? len + 1 : 1);
    if (!cpy) {
        wf_response_free(&res);
        return WF_ERR_ALLOC;
    }
    memcpy(cpy, res.body ? res.body : "", len);
    cpy[len] = '\0';
    wf_response_free(&res);
    *out_json = cpy;
    return WF_OK;
}

/* TODO: tools.ozone.moderation.getLabelDefinitions is NOT in the local atproto
 * lexicon snapshot, so no generated params/decoder exist. Reuses the existing
 * ozone.c transport helper and returns the raw response body as owned JSON. */
wf_status wf_ozone_moderation_get_label_definitions(
    wf_agent *agent, const char *const *uris, size_t n, char **out_json) {
    if (!agent || !agent->client || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status st = wf_ozone_get_label_defs(agent->client, uris, n, &res);
    if (st != WF_OK) {
        wf_response_free(&res);
        return st;
    }
    size_t len = res.body_len;
    char *cpy = (char *)malloc(len ? len + 1 : 1);
    if (!cpy) {
        wf_response_free(&res);
        return WF_ERR_ALLOC;
    }
    memcpy(cpy, res.body ? res.body : "", len);
    cpy[len] = '\0';
    wf_response_free(&res);
    *out_json = cpy;
    return WF_OK;
}
