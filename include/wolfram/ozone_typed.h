/*
 * ozone_typed.h — owned typed parsers + agent convenience wrappers for the
 * tools.ozone.* namespaces.
 *
 * This layer builds on the existing ozone transport layer (wolfram/ozone.h)
 * and the generated owning lex wrappers in wolfram/atproto_lex.h. For every
 * endpoint that has a generated owning decoder, this module provides:
 *   - wf_ozone_parse_<ns>_<op>  : decode a JSON body into the generated
 *                                 owning output struct (freed with the
 *                                 generated *_free).
 *   - wf_ozone_<ns>_<op>        : agent convenience wrapper that validates
 *                                 required args, syncs auth onto the agent's
 *                                 primary XRPC client, invokes the generated
 *                                 *_main_call, decodes the body, frees the
 *                                 response, and (on success) sets *out.
 *
 * Procedures (and a few queries) whose lexicon defines no owning output
 * decoder return the raw wf_response, freed by the caller with
 * wf_response_free. Endpoints whose lexicon is genuinely missing from the
 * local snapshot (tools.ozone.moderation.getSuggestions and
 * .getLabelDefinitions) are exposed as JSON-in/JSON-out wrappers returning an
 * owned char* body freed with free() (see the TODO on each).
 *
 * In addition, two hand-written ergonomic structs mirror the most-used list
 * responses following the labeler_typed ownership model (static
 * strdup/set_string/reset helpers, an owned `extra` cJSON subtree for open
 * fields, and a safe idempotent `_free`):
 *   - wf_ozone_subject_status / wf_ozone_subject_status_list
 *     (tools.ozone.moderation.queryStatuses subjectStatuses[]).
 *   - wf_ozone_team_member / wf_ozone_team_member_list
 *     (tools.ozone.team.listMembers members[]).
 *
 * Ownership summary:
 *   - (wf_lex_tools_ozone_*_main_output *) parse/wrapper results are freed
 *     with the matching generated *_main_output_free.
 *   - (wf_response *) results are freed with wf_response_free.
 *   - (char *) results (JSON-out wrappers) are freed with free().
 *   - The two hand-written list structs are freed with the matching
 *     wf_ozone_*_list_free.
 */

#ifndef WOLFRAM_OZONE_TYPED_H
#define WOLFRAM_OZONE_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Hand-written ergonomic structs                                     */
/* ------------------------------------------------------------------ */

/* A single moderation subject status (tools.ozone.moderation.defs#
 * subjectStatusView). Core fields are copied; any other fields remain in the
 * owned `extra` cJSON subtree (NULL when absent). */
typedef struct wf_ozone_subject_status {
    bool has_id;
    int64_t id;
    char *subject;            /* subject URI/DID */
    char *review_state;       /* reviewState */
    char *created_at;
    char *updated_at;
    bool has_comment;
    char *comment;
    bool has_priority_score;
    int64_t priority_score;
    bool has_takendown;
    bool takendown;
    bool has_appealed;
    bool appealed;
    char **tags;
    size_t tag_count;
    char *subject_repo_handle;
    cJSON *extra;             /* owned detached subtree of unknown fields */
} wf_ozone_subject_status;

typedef struct wf_ozone_subject_status_list {
    wf_ozone_subject_status *statuses;
    size_t status_count;
    char *cursor;
} wf_ozone_subject_status_list;

/* A single team member (tools.ozone.team.defs#member). Core fields are copied;
 * the nested `profile` (and any other fields) remain in the owned `extra`
 * cJSON subtree (NULL when absent). The member `handle` is *not* a top-level
 * field; when a `profile` object is present its `handle` is copied into
 * `profile_handle`. */
typedef struct wf_ozone_team_member {
    char *did;
    bool has_disabled;
    bool disabled;
    char *role;
    char *created_at;
    char *updated_at;
    char *last_updated_by;
    char *profile_handle;     /* copied from profile.handle when present */
    cJSON *extra;             /* owned detached subtree of unknown fields */
} wf_ozone_team_member;

typedef struct wf_ozone_team_member_list {
    wf_ozone_team_member *members;
    size_t member_count;
    char *cursor;
} wf_ozone_team_member_list;

/* ---- Ergonomic parsers (own their outputs) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset. */
wf_status wf_ozone_parse_subject_statuses(const char *json, size_t json_len,
                                          wf_ozone_subject_status_list *out);
wf_status wf_ozone_parse_team_members(const char *json, size_t json_len,
                                      wf_ozone_team_member_list *out);

/* ---- Ergonomic free functions (safe on a reset/zeroed struct) ---- */
void wf_ozone_subject_status_list_free(wf_ozone_subject_status_list *l);
void wf_ozone_team_member_list_free(wf_ozone_team_member_list *l);

/* ------------------------------------------------------------------ */
/* Generated-decode wrappers + parsers                                 */
/* (declarations generated from the endpoint table below)              */
/* ------------------------------------------------------------------ */

/* Query with params, owning output decoder. */
#define WF_OZONE_DECL_Q(ns, op, genop)                                     \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,     \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);            \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);

/* Query with no params, owning output decoder. */
#define WF_OZONE_DECL_Q0(ns, op, genop)                                    \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);            \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);

/* Procedure with input, owning output decoder. */
#define WF_OZONE_DECL_P(ns, op, genop)                                     \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,       \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);            \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);

/* Procedure with input, raw wf_response output. */
#define WF_OZONE_DECL_PR(ns, op, genop)                                    \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,       \
        wf_response *out);

/* Query with params, raw wf_response output. */
#define WF_OZONE_DECL_QR(ns, op, genop)                                    \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,     \
        wf_response *out);

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

#define X(ns, op, genop, kind) WF_OZONE_DECL_##kind(ns, op, genop)
WF_OZONE_ENDPOINTS
#undef X

/* JSON-in/JSON-out wrappers for endpoints whose lexicon is missing from the
 * local snapshot. `out_json` is an owned NUL-terminated string freed with
 * free(); on error it is left NULL. */
/* TODO: tools.ozone.moderation.getSuggestions is NOT in the local atproto
 * lexicon snapshot, so no generated params/decoder exist. This wrapper reuses
 * the existing ozone.c transport helper and returns the raw response body. */
wf_status wf_ozone_moderation_get_suggestions(
    wf_agent *agent, const char *const *ignore_subjects, size_t n,
    int64_t limit, const char *cursor, char **out_json);

/* TODO: tools.ozone.moderation.getLabelDefinitions is NOT in the local atproto
 * lexicon snapshot, so no generated params/decoder exist. This wrapper reuses
 * the existing ozone.c transport helper and returns the raw response body. */
wf_status wf_ozone_moderation_get_label_definitions(
    wf_agent *agent, const char *const *uris, size_t n, char **out_json);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OZONE_TYPED_H */
