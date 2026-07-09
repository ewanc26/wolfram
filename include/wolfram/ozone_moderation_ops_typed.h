/*
 * ozone_moderation_ops_typed.h — owning typed parsers + agent wrappers for the
 * moderation *operations* endpoints of the tools.ozone.* namespaces:
 *   - tools.ozone.report.*    (report lifecycle: query/get/assign/activity/stats)
 *   - tools.ozone.queue.*     (moderation queue CRUD + routing + assignment)
 *   - tools.ozone.signature.* (threat-signature correlation / related accounts)
 *
 * This module is a sibling to wolfram/ozone_typed.h. Where ozone_typed.h
 * exposes wrappers that hand back the *generated* owning output structs
 * (wf_lex_tools_ozone_*_main_output), this module hands back small,
 * hand-written *ergonomic* owned structs following the labeler_typed /
 * ozone_typed subject-status ownership model:
 *   - scalar fields (ints/bools/strings, string arrays) are copied out;
 *   - every nested object/union/array-of-object (subject views, moderator
 *     member records, nested queues, activity unions, etc.) is left in an
 *     owned, detached `extra` cJSON subtree (NULL when absent);
 *   - each struct has a matching idempotent `_free` that is safe on a
 *     zeroed/already-freed value.
 *
 * NOTE (team.*): tools.ozone.team.{addMember,deleteMember,listMembers,
 * updateMember} and the tools.ozone.team.defs#member view are already covered
 * by wolfram/ozone_typed.h (wf_ozone_team_member / wf_ozone_parse_team_members
 * + the wf_ozone_team_* wrappers). To avoid duplicating types already present,
 * team.* is intentionally NOT re-declared here.
 *
 * ---- Transport / auth idiom (mirrors ozone_typed.c) ----
 * Every wrapper takes a wf_agent, calls wf_agent_sync_auth(agent) to copy the
 * session access JWT onto the agent's primary XRPC client, then invokes the
 * generated wf_lex_tools_ozone_<ns>_<op>_main_call(agent->client, ...) which
 * performs the XRPC round-trip against the configured ozone moderation-service
 * endpoint. The raw wf_response body is then decoded into the ergonomic owned
 * struct with the matching `_parse` helper and the response is freed. The
 * ozone service host + admin/service auth are resolved exactly as in the
 * existing ozone_typed wrappers; this module invents no new auth path.
 *
 * ---- Ownership summary ----
 *   - Every wf_ozone_ops_* struct is released with the matching
 *     wf_ozone_ops_*_free. All `_free` functions are idempotent and safe on a
 *     zeroed struct; on any parse error the out param is left fully reset.
 *   - Procedures whose lexicon defines no output body (queue.unassignModerator,
 *     report.refreshStats) return the raw wf_response, freed by the caller with
 *     wf_response_free. (report.unassignModerator instead returns the bare
 *     assignmentView through wf_ozone_ops_report_assignment_view.)
 */

#ifndef WOLFRAM_OZONE_MODERATION_OPS_TYPED_H
#define WOLFRAM_OZONE_MODERATION_OPS_TYPED_H

#include "wolfram/agent.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/xrpc.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* tools.ozone.report.* — owned view structs                          */
/* ================================================================== */

/* tools.ozone.report.defs#reportView. Nested subject/reporter/assignment/
 * queue/subjectStatus/actions objects remain in `extra`. */
typedef struct wf_ozone_ops_report_view {
    bool has_id;
    int64_t id;
    bool has_event_id;
    int64_t event_id;
    char *status;
    char *report_type;
    char *reported_by;
    char *comment;
    char *created_at;
    char *updated_at;
    char *queued_at;
    char *action_note;
    bool has_related_report_count;
    int64_t related_report_count;
    bool has_is_muted;
    bool is_muted;
    cJSON *extra;
} wf_ozone_ops_report_view;

typedef struct wf_ozone_ops_report_list {
    wf_ozone_ops_report_view *reports;
    size_t report_count;
    char *cursor;
} wf_ozone_ops_report_list;

/* tools.ozone.report.defs#reportActivityView. The activity union, meta,
 * moderator member and nested report view remain in `extra`. */
typedef struct wf_ozone_ops_report_activity_view {
    bool has_id;
    int64_t id;
    bool has_report_id;
    int64_t report_id;
    bool has_is_automated;
    bool is_automated;
    char *created_by;
    char *created_at;
    char *internal_note;
    char *public_note;
    cJSON *extra;
} wf_ozone_ops_report_activity_view;

typedef struct wf_ozone_ops_report_activity_list {
    wf_ozone_ops_report_activity_view *activities;
    size_t activity_count;
    char *cursor;
} wf_ozone_ops_report_activity_list;

/* tools.ozone.report.defs#assignmentView. The moderator member + nested queue
 * remain in `extra`. */
typedef struct wf_ozone_ops_report_assignment_view {
    bool has_id;
    int64_t id;
    char *did;
    bool has_report_id;
    int64_t report_id;
    char *start_at;
    char *end_at;
    cJSON *extra;
} wf_ozone_ops_report_assignment_view;

typedef struct wf_ozone_ops_report_assignment_list {
    wf_ozone_ops_report_assignment_view *assignments;
    size_t assignment_count;
    char *cursor;
} wf_ozone_ops_report_assignment_list;

/* tools.ozone.report.defs#liveStats. All counters are optional. */
typedef struct wf_ozone_ops_live_stats {
    bool has_pending_count;
    int64_t pending_count;
    bool has_actioned_count;
    int64_t actioned_count;
    bool has_escalated_count;
    int64_t escalated_count;
    bool has_inbound_count;
    int64_t inbound_count;
    bool has_action_rate;
    int64_t action_rate;
    bool has_avg_handling_time_sec;
    int64_t avg_handling_time_sec;
    char *last_updated;
    cJSON *extra;
} wf_ozone_ops_live_stats;

/* tools.ozone.report.defs#historicalStats. `date` is required. */
typedef struct wf_ozone_ops_historical_stats {
    char *date;
    char *computed_at;
    bool has_pending_count;
    int64_t pending_count;
    bool has_actioned_count;
    int64_t actioned_count;
    bool has_escalated_count;
    int64_t escalated_count;
    bool has_inbound_count;
    int64_t inbound_count;
    bool has_action_rate;
    int64_t action_rate;
    bool has_avg_handling_time_sec;
    int64_t avg_handling_time_sec;
    cJSON *extra;
} wf_ozone_ops_historical_stats;

typedef struct wf_ozone_ops_historical_stats_list {
    wf_ozone_ops_historical_stats *stats;
    size_t stats_count;
    char *cursor;
} wf_ozone_ops_historical_stats_list;

/* ================================================================== */
/* tools.ozone.queue.* — owned view structs                           */
/* ================================================================== */

/* tools.ozone.queue.defs#queueView. The nested #queueStats object remains in
 * `extra`. */
typedef struct wf_ozone_ops_queue_view {
    bool has_id;
    int64_t id;
    char *name;
    char **subject_types;
    size_t subject_type_count;
    char **report_types;
    size_t report_type_count;
    char *collection;
    char *description;
    char *created_by;
    char *created_at;
    char *updated_at;
    bool has_enabled;
    bool enabled;
    char *deleted_at;
    cJSON *extra;
} wf_ozone_ops_queue_view;

typedef struct wf_ozone_ops_queue_list {
    wf_ozone_ops_queue_view *queues;
    size_t queue_count;
    char *cursor;
} wf_ozone_ops_queue_list;

/* tools.ozone.queue.defs#assignmentView. The moderator member + nested queue
 * remain in `extra`. */
typedef struct wf_ozone_ops_queue_assignment_view {
    bool has_id;
    int64_t id;
    char *did;
    char *start_at;
    char *end_at;
    cJSON *extra;
} wf_ozone_ops_queue_assignment_view;

typedef struct wf_ozone_ops_queue_assignment_list {
    wf_ozone_ops_queue_assignment_view *assignments;
    size_t assignment_count;
    char *cursor;
} wf_ozone_ops_queue_assignment_list;

/* tools.ozone.queue.deleteQueue output. */
typedef struct wf_ozone_ops_delete_queue_result {
    bool has_deleted;
    bool deleted;
    bool has_reports_migrated;
    int64_t reports_migrated;
} wf_ozone_ops_delete_queue_result;

/* tools.ozone.queue.routeReports output. */
typedef struct wf_ozone_ops_route_reports_result {
    bool has_assigned;
    int64_t assigned;
    bool has_unmatched;
    int64_t unmatched;
} wf_ozone_ops_route_reports_result;

/* ================================================================== */
/* tools.ozone.signature.* — owned view structs                       */
/* ================================================================== */

/* tools.ozone.signature.defs#sigDetail. */
typedef struct wf_ozone_ops_sig_detail {
    char *property;
    char *value;
    cJSON *extra;
} wf_ozone_ops_sig_detail;

/* tools.ozone.signature.findCorrelation output ({ details }). */
typedef struct wf_ozone_ops_sig_detail_list {
    wf_ozone_ops_sig_detail *details;
    size_t detail_count;
} wf_ozone_ops_sig_detail_list;

/* A light view over com.atproto.admin.defs#accountView. Only the most common
 * scalar fields are copied; everything else (related records, invites, threat
 * signatures, etc.) remains in `extra`. */
typedef struct wf_ozone_ops_account {
    char *did;
    char *handle;
    char *email;
    char *indexed_at;
    cJSON *extra;
} wf_ozone_ops_account;

typedef struct wf_ozone_ops_account_list {
    wf_ozone_ops_account *accounts;
    size_t account_count;
    char *cursor;
} wf_ozone_ops_account_list;

/* tools.ozone.signature.findRelatedAccounts#relatedAccount. The nested
 * accountView is copied into `account`; its threat-signature similarities are
 * copied into `similarities`. */
typedef struct wf_ozone_ops_related_account {
    wf_ozone_ops_account account;
    wf_ozone_ops_sig_detail *similarities;
    size_t similarity_count;
    cJSON *extra;
} wf_ozone_ops_related_account;

typedef struct wf_ozone_ops_related_account_list {
    wf_ozone_ops_related_account *accounts;
    size_t account_count;
    char *cursor;
} wf_ozone_ops_related_account_list;

/* ================================================================== */
/* Parsers (offline, no network)                                      */
/*                                                                    */
/* Each parser decodes a representative endpoint response body into an */
/* owned struct. Return WF_ERR_INVALID_ARG on NULL json/out,          */
/* WF_ERR_PARSE on malformed JSON or a missing/invalid required shape, */
/* WF_ERR_ALLOC on allocation failure, WF_OK on success. On any error  */
/* `out` is left fully reset.                                          */
/* ================================================================== */

/* report.* */
wf_status wf_ozone_ops_parse_report_list(const char *json, size_t json_len,
                                         wf_ozone_ops_report_list *out);
/* Parses a bare tools.ozone.report.defs#reportView object (getReport). */
wf_status wf_ozone_ops_parse_report_view(const char *json, size_t json_len,
                                         wf_ozone_ops_report_view *out);
/* Parses a { "report": reportView } wrapper (getLatestReport, reassignQueue). */
wf_status wf_ozone_ops_parse_wrapped_report(const char *json, size_t json_len,
                                            wf_ozone_ops_report_view *out);
wf_status wf_ozone_ops_parse_report_activity_list(
    const char *json, size_t json_len, wf_ozone_ops_report_activity_list *out);
/* Parses a { "activity": reportActivityView } wrapper (createActivity). */
wf_status wf_ozone_ops_parse_wrapped_activity(
    const char *json, size_t json_len, wf_ozone_ops_report_activity_view *out);
wf_status wf_ozone_ops_parse_report_assignment_list(
    const char *json, size_t json_len, wf_ozone_ops_report_assignment_list *out);
/* Parses a bare report.defs#assignmentView object (assign/unassignModerator). */
wf_status wf_ozone_ops_parse_report_assignment_view(
    const char *json, size_t json_len, wf_ozone_ops_report_assignment_view *out);
/* Parses a { "stats": liveStats } wrapper (getLiveStats). */
wf_status wf_ozone_ops_parse_live_stats(const char *json, size_t json_len,
                                        wf_ozone_ops_live_stats *out);
wf_status wf_ozone_ops_parse_historical_stats_list(
    const char *json, size_t json_len, wf_ozone_ops_historical_stats_list *out);

/* queue.* */
wf_status wf_ozone_ops_parse_queue_list(const char *json, size_t json_len,
                                        wf_ozone_ops_queue_list *out);
/* Parses a { "queue": queueView } wrapper (createQueue, updateQueue). */
wf_status wf_ozone_ops_parse_wrapped_queue(const char *json, size_t json_len,
                                           wf_ozone_ops_queue_view *out);
wf_status wf_ozone_ops_parse_queue_assignment_list(
    const char *json, size_t json_len, wf_ozone_ops_queue_assignment_list *out);
/* Parses a bare queue.defs#assignmentView object (queue.assignModerator). */
wf_status wf_ozone_ops_parse_queue_assignment_view(
    const char *json, size_t json_len, wf_ozone_ops_queue_assignment_view *out);
wf_status wf_ozone_ops_parse_delete_queue_result(
    const char *json, size_t json_len, wf_ozone_ops_delete_queue_result *out);
wf_status wf_ozone_ops_parse_route_reports_result(
    const char *json, size_t json_len, wf_ozone_ops_route_reports_result *out);

/* signature.* */
wf_status wf_ozone_ops_parse_sig_detail_list(
    const char *json, size_t json_len, wf_ozone_ops_sig_detail_list *out);
wf_status wf_ozone_ops_parse_related_account_list(
    const char *json, size_t json_len, wf_ozone_ops_related_account_list *out);
wf_status wf_ozone_ops_parse_account_list(
    const char *json, size_t json_len, wf_ozone_ops_account_list *out);

/* ================================================================== */
/* Free functions (idempotent, safe on a zeroed struct)               */
/* ================================================================== */

void wf_ozone_ops_report_view_free(wf_ozone_ops_report_view *v);
void wf_ozone_ops_report_list_free(wf_ozone_ops_report_list *l);
void wf_ozone_ops_report_activity_view_free(wf_ozone_ops_report_activity_view *v);
void wf_ozone_ops_report_activity_list_free(wf_ozone_ops_report_activity_list *l);
void wf_ozone_ops_report_assignment_view_free(
    wf_ozone_ops_report_assignment_view *v);
void wf_ozone_ops_report_assignment_list_free(
    wf_ozone_ops_report_assignment_list *l);
void wf_ozone_ops_live_stats_free(wf_ozone_ops_live_stats *v);
void wf_ozone_ops_historical_stats_list_free(
    wf_ozone_ops_historical_stats_list *l);

void wf_ozone_ops_queue_view_free(wf_ozone_ops_queue_view *v);
void wf_ozone_ops_queue_list_free(wf_ozone_ops_queue_list *l);
void wf_ozone_ops_queue_assignment_view_free(
    wf_ozone_ops_queue_assignment_view *v);
void wf_ozone_ops_queue_assignment_list_free(
    wf_ozone_ops_queue_assignment_list *l);

void wf_ozone_ops_sig_detail_list_free(wf_ozone_ops_sig_detail_list *l);
void wf_ozone_ops_account_free(wf_ozone_ops_account *v);
void wf_ozone_ops_account_list_free(wf_ozone_ops_account_list *l);
void wf_ozone_ops_related_account_list_free(
    wf_ozone_ops_related_account_list *l);

/* ================================================================== */
/* Agent wrappers                                                     */
/*                                                                    */
/* All wrappers return WF_ERR_INVALID_ARG for NULL agent/agent->client/ */
/* required-input/out, sync auth, perform the XRPC call, decode the    */
/* body into the ergonomic owned struct, free the response, and set    */
/* *out on success. On failure *out is left reset.                     */
/* ================================================================== */

/* report.* */
wf_status wf_ozone_ops_report_query_reports(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_query_reports_main_params *params,
    wf_ozone_ops_report_list *out);
wf_status wf_ozone_ops_report_get_report(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_get_report_main_params *params,
    wf_ozone_ops_report_view *out);
wf_status wf_ozone_ops_report_get_latest_report(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_get_latest_report_main_params *params,
    wf_ozone_ops_report_view *out);
wf_status wf_ozone_ops_report_get_live_stats(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_get_live_stats_main_params *params,
    wf_ozone_ops_live_stats *out);
wf_status wf_ozone_ops_report_get_historical_stats(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_get_historical_stats_main_params *params,
    wf_ozone_ops_historical_stats_list *out);
wf_status wf_ozone_ops_report_get_assignments(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_get_assignments_main_params *params,
    wf_ozone_ops_report_assignment_list *out);
wf_status wf_ozone_ops_report_query_activities(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_query_activities_main_params *params,
    wf_ozone_ops_report_activity_list *out);
wf_status wf_ozone_ops_report_list_activities(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_list_activities_main_params *params,
    wf_ozone_ops_report_activity_list *out);
wf_status wf_ozone_ops_report_create_activity(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_create_activity_main_input *input,
    wf_ozone_ops_report_activity_view *out);
wf_status wf_ozone_ops_report_assign_moderator(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_assign_moderator_main_input *input,
    wf_ozone_ops_report_assignment_view *out);
wf_status wf_ozone_ops_report_unassign_moderator(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_unassign_moderator_main_input *input,
    wf_ozone_ops_report_assignment_view *out);
wf_status wf_ozone_ops_report_reassign_queue(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_reassign_queue_main_input *input,
    wf_ozone_ops_report_view *out);
/* refreshStats has an empty output body; the raw response is returned and
 * freed by the caller with wf_response_free. */
wf_status wf_ozone_ops_report_refresh_stats(
    wf_agent *agent,
    const wf_lex_tools_ozone_report_refresh_stats_main_input *input,
    wf_response *out);

/* queue.* */
wf_status wf_ozone_ops_queue_list_queues(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_list_queues_main_params *params,
    wf_ozone_ops_queue_list *out);
wf_status wf_ozone_ops_queue_get_assignments(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_get_assignments_main_params *params,
    wf_ozone_ops_queue_assignment_list *out);
wf_status wf_ozone_ops_queue_create_queue(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_create_queue_main_input *input,
    wf_ozone_ops_queue_view *out);
wf_status wf_ozone_ops_queue_update_queue(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_update_queue_main_input *input,
    wf_ozone_ops_queue_view *out);
wf_status wf_ozone_ops_queue_delete_queue(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_delete_queue_main_input *input,
    wf_ozone_ops_delete_queue_result *out);
wf_status wf_ozone_ops_queue_route_reports(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_route_reports_main_input *input,
    wf_ozone_ops_route_reports_result *out);
wf_status wf_ozone_ops_queue_assign_moderator(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_assign_moderator_main_input *input,
    wf_ozone_ops_queue_assignment_view *out);
/* unassignModerator has an empty output body; the raw response is returned and
 * freed by the caller with wf_response_free. */
wf_status wf_ozone_ops_queue_unassign_moderator(
    wf_agent *agent,
    const wf_lex_tools_ozone_queue_unassign_moderator_main_input *input,
    wf_response *out);

/* signature.* */
wf_status wf_ozone_ops_signature_find_correlation(
    wf_agent *agent,
    const wf_lex_tools_ozone_signature_find_correlation_main_params *params,
    wf_ozone_ops_sig_detail_list *out);
wf_status wf_ozone_ops_signature_find_related_accounts(
    wf_agent *agent,
    const wf_lex_tools_ozone_signature_find_related_accounts_main_params *params,
    wf_ozone_ops_related_account_list *out);
wf_status wf_ozone_ops_signature_search_accounts(
    wf_agent *agent,
    const wf_lex_tools_ozone_signature_search_accounts_main_params *params,
    wf_ozone_ops_account_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OZONE_MODERATION_OPS_TYPED_H */
