/*
 * ozone.h — moderation-service / labeler helper.
 *
 * A small, transport-backed convenience layer over the local atproto
 * `tools/ozone/*` lexicons. It reuses the low-level XRPC client
 * (wolfram/xrpc.h) rather than performing any network I/O itself, so all
 * real socket work stays isolated in xrpc.c per the transport-first rule.
 *
 * The module exposes:
 *   - wf_labeler_build_service_record : an in-memory builder for the
 *     app.bsky.labeler.service record (no network).
 *   - wf_ozone_report_subject         : file a moderation report
 *     (com.atproto.moderation.createReport).
 *   - wf_ozone_query_statuses         : tools.ozone.moderation.queryStatuses.
 *   - wf_ozone_get_label_defs         : tools.ozone.moderation.getLabelDefinitions.
 *   - wf_ozone_emit_event             : tools.ozone.moderation.emitEvent.
 *   - wf_ozone_query_events /
 *     wf_ozone_list_events            : tools.ozone.moderation.queryEvents.
 *   - wf_ozone_get_event              : tools.ozone.moderation.getEvent.
 *   - wf_ozone_get_reporter_stats     : tools.ozone.moderation.getReporterStats.
 *   - wf_ozone_scan_verdicts          : tools.ozone.moderation.scanVerdicts
 *     (JSON-in/JSON-out; lexicon not in the local snapshot — see TODO).
 *   - wf_ozone_get_subjects           : tools.ozone.moderation.getSubjects.
 *   - wf_ozone_get_suggestions        : tools.ozone.moderation.getSuggestions.
 *   - wf_ozone_get_tag /
 *     wf_ozone_query_tags             : tag lookup (JSON-out; lexicon not in
 *     the local snapshot — see TODO).
 *   - Communication templates         : tools.ozone.communication.*.
 *   - Set values                      : tools.ozone.set.*.
 *
 * Where a generated lexicon type exists in atproto_lex.h it is reused for the
 * input and (when the lexicon defines one) for an owning output decoder. The
 * transport wrappers return a wf_response* carrying the raw server reply
 * (freed with wf_response_free) so that HTTP error bodies are preserved; the
 * matching `*_parse` helper decodes a successful wf_response body into an
 * owning typed value freed with the generated `*_free`. Endpoints whose
 * lexicon is not generated are exposed as JSON-in/JSON-out wrappers with a
 * borrowed input and an owned output string freed with free().
 *
 * Ownership summary:
 *   - (wf_response *) outputs are freed with wf_response_free.
 *   - (char *) outputs are freed with free().
 *   - generated `*_output` parse results are freed with the matching
 *     generated `*_output_free`.
 *   - wf_ozone_emit_event_input_serialize output is freed with
 *     wf_ozone_emit_event_input_free.
 * Nothing is freed on the caller's behalf.
 */

#ifndef WOLFRAM_OZONE_H
#define WOLFRAM_OZONE_H

#include "wolfram/xrpc.h"
#include "wolfram/atproto_lex.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NSIDs used by this module. */
#define WF_OZONE_QUERY_STATUSES_NSID       "tools.ozone.moderation.queryStatuses"
#define WF_OZONE_GET_LABEL_DEFS_NSID       "tools.ozone.moderation.getLabelDefinitions"
#define WF_OZONE_GET_SUGGESTIONS_NSID      "tools.ozone.moderation.getSuggestions"
#define WF_ATPROTO_CREATE_REPORT_NSID      "com.atproto.moderation.createReport"

#define WF_OZONE_EMIT_EVENT_NSID           "tools.ozone.moderation.emitEvent"
#define WF_OZONE_QUERY_EVENTS_NSID         "tools.ozone.moderation.queryEvents"
#define WF_OZONE_GET_EVENT_NSID            "tools.ozone.moderation.getEvent"
#define WF_OZONE_GET_REPORTER_STATS_NSID   "tools.ozone.moderation.getReporterStats"
#define WF_OZONE_SCAN_VERDICTS_NSID        "tools.ozone.moderation.scanVerdicts"
#define WF_OZONE_GET_SUBJECTS_NSID         "tools.ozone.moderation.getSubjects"
#define WF_OZONE_GET_TAG_NSID              "tools.ozone.moderation.getTag"
#define WF_OZONE_QUERY_TAGS_NSID           "tools.ozone.moderation.queryTags"

#define WF_OZONE_COMM_LIST_TEMPLATES_NSID  "tools.ozone.communication.listTemplates"
#define WF_OZONE_COMM_CREATE_TEMPLATE_NSID "tools.ozone.communication.createTemplate"
#define WF_OZONE_COMM_UPDATE_TEMPLATE_NSID "tools.ozone.communication.updateTemplate"
#define WF_OZONE_COMM_DELETE_TEMPLATE_NSID "tools.ozone.communication.deleteTemplate"

#define WF_OZONE_SET_ADD_VALUES_NSID       "tools.ozone.set.addValues"
#define WF_OZONE_SET_DELETE_VALUES_NSID    "tools.ozone.set.deleteValues"
#define WF_OZONE_SET_GET_VALUES_NSID       "tools.ozone.set.getValues"
#define WF_OZONE_SET_UPSERT_SET_NSID       "tools.ozone.set.upsertSet"

/* tools.ozone.moderation — additional query/procedure endpoints */
#define WF_OZONE_GET_ACCOUNT_TIMELINE_NSID    "tools.ozone.moderation.getAccountTimeline"
#define WF_OZONE_GET_RECORDS_NSID             "tools.ozone.moderation.getRecords"
#define WF_OZONE_GET_REPO_NSID                "tools.ozone.moderation.getRepo"
#define WF_OZONE_GET_REPOS_NSID               "tools.ozone.moderation.getRepos"
#define WF_OZONE_SEARCH_REPOS_NSID            "tools.ozone.moderation.searchRepos"
#define WF_OZONE_CANCEL_SCHEDULED_ACTIONS_NSID "tools.ozone.moderation.cancelScheduledActions"
#define WF_OZONE_SCHEDULE_ACTION_NSID         "tools.ozone.moderation.scheduleAction"
#define WF_OZONE_LIST_SCHEDULED_ACTIONS_NSID  "tools.ozone.moderation.listScheduledActions"

/* tools.ozone.queue.* */
#define WF_OZONE_QUEUE_ASSIGN_MODERATOR_NSID    "tools.ozone.queue.assignModerator"
#define WF_OZONE_QUEUE_CREATE_QUEUE_NSID        "tools.ozone.queue.createQueue"
#define WF_OZONE_QUEUE_DELETE_QUEUE_NSID        "tools.ozone.queue.deleteQueue"
#define WF_OZONE_QUEUE_GET_ASSIGNMENTS_NSID     "tools.ozone.queue.getAssignments"
#define WF_OZONE_QUEUE_LIST_QUEUES_NSID         "tools.ozone.queue.listQueues"
#define WF_OZONE_QUEUE_ROUTE_REPORTS_NSID       "tools.ozone.queue.routeReports"
#define WF_OZONE_QUEUE_UNASSIGN_MODERATOR_NSID  "tools.ozone.queue.unassignModerator"
#define WF_OZONE_QUEUE_UPDATE_QUEUE_NSID        "tools.ozone.queue.updateQueue"

/* tools.ozone.report.* */
#define WF_OZONE_REPORT_ASSIGN_MODERATOR_NSID       "tools.ozone.report.assignModerator"
#define WF_OZONE_REPORT_CREATE_ACTIVITY_NSID         "tools.ozone.report.createActivity"
#define WF_OZONE_REPORT_GET_ASSIGNMENTS_NSID         "tools.ozone.report.getAssignments"
#define WF_OZONE_REPORT_GET_HISTORICAL_STATS_NSID    "tools.ozone.report.getHistoricalStats"
#define WF_OZONE_REPORT_GET_LATEST_REPORT_NSID       "tools.ozone.report.getLatestReport"
#define WF_OZONE_REPORT_GET_LIVE_STATS_NSID          "tools.ozone.report.getLiveStats"
#define WF_OZONE_REPORT_GET_REPORT_NSID              "tools.ozone.report.getReport"
#define WF_OZONE_REPORT_LIST_ACTIVITIES_NSID         "tools.ozone.report.listActivities"
#define WF_OZONE_REPORT_QUERY_ACTIVITIES_NSID        "tools.ozone.report.queryActivities"
#define WF_OZONE_REPORT_QUERY_REPORTS_NSID           "tools.ozone.report.queryReports"
#define WF_OZONE_REPORT_REASSIGN_QUEUE_NSID          "tools.ozone.report.reassignQueue"
#define WF_OZONE_REPORT_REFRESH_STATS_NSID           "tools.ozone.report.refreshStats"
#define WF_OZONE_REPORT_UNASSIGN_MODERATOR_NSID      "tools.ozone.report.unassignModerator"

/* tools.ozone.team.* */
#define WF_OZONE_TEAM_ADD_MEMBER_NSID       "tools.ozone.team.addMember"
#define WF_OZONE_TEAM_DELETE_MEMBER_NSID    "tools.ozone.team.deleteMember"
#define WF_OZONE_TEAM_LIST_MEMBERS_NSID     "tools.ozone.team.listMembers"
#define WF_OZONE_TEAM_UPDATE_MEMBER_NSID    "tools.ozone.team.updateMember"

/* tools.ozone.verification.* */
#define WF_OZONE_VERIFICATION_GRANT_NSID      "tools.ozone.verification.grantVerifications"
#define WF_OZONE_VERIFICATION_LIST_NSID       "tools.ozone.verification.listVerifications"
#define WF_OZONE_VERIFICATION_REVOKE_NSID     "tools.ozone.verification.revokeVerifications"

/* tools.ozone.signature.* */
#define WF_OZONE_SIGNATURE_FIND_CORRELATION_NSID       "tools.ozone.signature.findCorrelation"
#define WF_OZONE_SIGNATURE_FIND_RELATED_ACCOUNTS_NSID  "tools.ozone.signature.findRelatedAccounts"
#define WF_OZONE_SIGNATURE_SEARCH_ACCOUNTS_NSID        "tools.ozone.signature.searchAccounts"

/* tools.ozone.setting.* */
#define WF_OZONE_SETTING_LIST_OPTIONS_NSID     "tools.ozone.setting.listOptions"
#define WF_OZONE_SETTING_REMOVE_OPTIONS_NSID   "tools.ozone.setting.removeOptions"
#define WF_OZONE_SETTING_UPSERT_OPTION_NSID    "tools.ozone.setting.upsertOption"

/* tools.ozone.hosting.* */
#define WF_OZONE_HOSTING_GET_ACCOUNT_HISTORY_NSID "tools.ozone.hosting.getAccountHistory"

/* tools.ozone.server.* */
#define WF_OZONE_SERVER_GET_CONFIG_NSID       "tools.ozone.server.getConfig"

/* tools.ozone.safelink.* */
#define WF_OZONE_SAFELINK_ADD_RULE_NSID       "tools.ozone.safelink.addRule"
#define WF_OZONE_SAFELINK_QUERY_EVENTS_NSID   "tools.ozone.safelink.queryEvents"
#define WF_OZONE_SAFELINK_QUERY_RULES_NSID    "tools.ozone.safelink.queryRules"
#define WF_OZONE_SAFELINK_REMOVE_RULE_NSID    "tools.ozone.safelink.removeRule"
#define WF_OZONE_SAFELINK_UPDATE_RULE_NSID    "tools.ozone.safelink.updateRule"

/*
 * Build an `app.bsky.labeler.service` record value as a JSON string, in
 * memory. This never touches the network.
 *
 * `labeler_did` is the DID of the account that will run the labeler and is
 * required. `creator` (optional, may be NULL) is recorded on the service
 * record; `created_at` (optional, may be NULL) is an RFC 3339 timestamp —
 * when NULL the builder fills in the current UTC time.
 *
 * On success returns a NUL-terminated JSON string owned by the caller; free
 * it with free(). Returns NULL on allocation failure or when `labeler_did` is
 * NULL.
 */
char *wf_labeler_build_service_record(const char *labeler_did,
                                       const char *creator,
                                       const char *created_at);

/*
 * File a moderation report about a repo or a specific record.
 *
 * `repo_did` identifies the subject account. When both `collection` and
 * `rkey` are non-NULL the subject is a
 * `com.atproto.repo.strongRef` (at://did/collection/rkey); otherwise the
 * subject is a `com.atproto.admin.defs#repoRef` (the whole account).
 *
 * `reason_type` is a com.atproto.moderation.defs#reasonType string (e.g.
 * "com.atproto.moderation.defs#reasonViolation"); `reason` is free-form
 * context (may be NULL).
 *
 * On WF_OK, *out is populated with the created report and must be released
 * with wf_response_free. Returns WF_ERR_INVALID_ARG for NULL client/did/
 * reason_type, or a transport error from wf_xrpc_procedure.
 *
 * NOTE: record subjects are built without a `cid`. Production callers that
 * need a verified record reference should extend the request JSON with the
 * record CID before sending.
 */
wf_status wf_ozone_report_subject(wf_xrpc_client *client,
                                   const char *repo_did,
                                   const char *collection,
                                   const char *rkey,
                                   const char *reason_type,
                                   const char *reason,
                                   wf_response *out);

/*
 * Query moderation subject statuses via tools.ozone.moderation.queryStatuses.
 *
 * `subjects` is an array of `n` atproto URIs (or DIDs) to fetch statuses for;
 * pass NULL/`n == 0` to query the default queue. Each subject is sent as a
 * repeated `subject` query parameter.
 *
 * On WF_OK, *out is populated and must be released with wf_response_free.
 * Returns WF_ERR_INVALID_ARG for NULL client/out, or a transport error.
 */
wf_status wf_ozone_query_statuses(wf_xrpc_client *client,
                                   const char **subjects,
                                   size_t n,
                                   wf_response *out);

/*
 * Fetch label value definitions via tools.ozone.moderation.getLabelDefinitions
 * (falling back to getSuggestions when definitions are unavailable). `uris` is
 * an array of `n` labeler service record URIs; each is sent as a repeated
 * `uris` query parameter.
 *
 * On WF_OK, *out is populated and must be released with wf_response_free.
 * Returns WF_ERR_INVALID_ARG for NULL client/out, or a transport error.
 */
wf_status wf_ozone_get_label_defs(wf_xrpc_client *client,
                                   const char **uris,
                                   size_t n,
                                   wf_response *out);

/* ------------------------------------------------------------------ */
/* Emit / query moderation events                                     */
/* ------------------------------------------------------------------ */

/*
 * Emit a moderation event via tools.ozone.moderation.emitEvent.
 *
 * `input` is the borrowed generated input struct
 * (wf_lex_tools_ozone_moderation_emit_event_main_input); `event` and
 * `subject` inside it are borrowed wf_lex_json views. On WF_OK, *out is
 * populated with the raw server response and must be released with
 * wf_response_free. The lexicon defines no output decoder, so the response
 * body is returned verbatim. Returns WF_ERR_INVALID_ARG for NULL
 * client/input/out, or a transport error.
 */
wf_status wf_ozone_emit_event(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_emit_event_main_input *input,
    wf_response *out);

/*
 * Pure, network-free serializer for an emitEvent input. Builds the request
 * JSON from the borrowed generated input struct. Used by offline tests to
 * validate that a constructed input round-trips through the lexicon encoder.
 *
 * On success returns an owned NUL-terminated JSON string; free it with
 * wf_ozone_emit_event_input_free. Returns NULL on NULL input or encode
 * failure.
 */
char *wf_ozone_emit_event_input_serialize(
    const wf_lex_tools_ozone_moderation_emit_event_main_input *input);

/* Free JSON returned by wf_ozone_emit_event_input_serialize. Safe on NULL. */
void wf_ozone_emit_event_input_free(char *json);

/*
 * Query moderation events via tools.ozone.moderation.queryEvents.
 *
 * `params` is the borrowed generated params struct
 * (wf_lex_tools_ozone_moderation_query_events_main_params). On WF_OK, *out is
 * populated with the raw server response freed with wf_response_free. Decode
 * a successful reply with wf_ozone_query_events_parse. Returns
 * WF_ERR_INVALID_ARG for NULL client/params/out, or a transport error.
 */
wf_status wf_ozone_query_events(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_query_events_main_params *params,
    wf_response *out);

/*
 * Decode a successful wf_ozone_query_events (or wf_ozone_list_events) reply
 * into an owning typed value. On WF_OK, *out must be released with
 * wf_lex_tools_ozone_moderation_query_events_main_output_free. Returns
 * WF_ERR_INVALID_ARG for NULL resp/out, or a parse error.
 */
wf_status wf_ozone_query_events_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_query_events_main_output **out);

/*
 * List moderation events (tools.ozone.moderation.queryEvents with no
 * filters). Convenience wrapper over wf_ozone_query_events for the common
 * "fetch the most recent events" case.
 *
 * `limit` (0 = server default) and `cursor` (may be NULL) page the results.
 * On WF_OK, *out is populated with the raw server response freed with
 * wf_response_free. Decode with wf_ozone_query_events_parse.
 */
wf_status wf_ozone_list_events(
    wf_xrpc_client *client,
    int64_t limit,
    const char *cursor,
    wf_response *out);

/*
 * Fetch a single moderation event by id via tools.ozone.moderation.getEvent.
 *
 * `id` is the numeric event id. On WF_OK, *out is populated with the raw
 * server response and must be released with wf_response_free. The lexicon
 * defines no output decoder, so the body is returned verbatim.
 */
wf_status wf_ozone_get_event(wf_xrpc_client *client,
                              int64_t id,
                              wf_response *out);

/*
 * Fetch reporter statistics via tools.ozone.moderation.getReporterStats.
 *
 * `dids` is an array of `n` reporter DIDs. On WF_OK, *out is populated with
 * the raw server response freed with wf_response_free. Decode a successful
 * reply with wf_ozone_get_reporter_stats_parse. Returns WF_ERR_INVALID_ARG for
 * NULL client/dids/out, or a transport error.
 */
wf_status wf_ozone_get_reporter_stats(
    wf_xrpc_client *client,
    const char **dids,
    size_t n,
    wf_response *out);

/*
 * Decode a successful wf_ozone_get_reporter_stats reply into an owning typed
 * value. On WF_OK, *out must be released with
 * wf_lex_tools_ozone_moderation_get_reporter_stats_main_output_free.
 */
wf_status wf_ozone_get_reporter_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_reporter_stats_main_output **out);

/*
 * Scan account verdicts via tools.ozone.moderation.scanVerdicts.
 *
 * TODO: scanVerdicts is NOT present in the local atproto lexicon snapshot
 * (/Volumes/Storage/Developer/Local/atproto/lexicons). The NSID is taken from
 * the current upstream lexicon; confirm it against the deployment before
 * relying on it. The request/response shape is therefore accepted and
 * returned as raw JSON: `body_json` is borrowed; on WF_OK *out_json is an
 * owned NUL-terminated JSON string freed with free(). Returns
 * WF_ERR_INVALID_ARG for NULL client/body_json/out_json, or a transport error.
 */
wf_status wf_ozone_scan_verdicts(wf_xrpc_client *client,
                                  const char *body_json,
                                  char **out_json);

/* ------------------------------------------------------------------ */
/* Subjects / suggestions                                             */
/* ------------------------------------------------------------------ */

/*
 * Fetch subject views via tools.ozone.moderation.getSubjects.
 *
 * `subjects` is an array of `n` atproto URIs/DIDs. On WF_OK, *out is populated
 * with the raw server response freed with wf_response_free. Decode a
 * successful reply with wf_ozone_get_subjects_parse. Returns
 * WF_ERR_INVALID_ARG for NULL client/subjects/out, or a transport error.
 */
wf_status wf_ozone_get_subjects(
    wf_xrpc_client *client,
    const char **subjects,
    size_t n,
    wf_response *out);

/*
 * Decode a successful wf_ozone_get_subjects reply into an owning typed value.
 * On WF_OK, *out must be released with
 * wf_lex_tools_ozone_moderation_get_subjects_main_output_free.
 */
wf_status wf_ozone_get_subjects_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_subjects_main_output **out);

/*
 * Fetch moderation suggestions via tools.ozone.moderation.getSuggestions.
 *
 * TODO: getSuggestions is NOT present in the local atproto lexicon snapshot,
 * so no generated params struct exists. This wrapper builds the best-effort
 * query parameters (`limit`, `cursor`, repeated `ignoreSubjects`) and returns
 * the raw response. Confirm the exact parameter names against the deployment.
 * On WF_OK, *out is populated with the raw response freed with wf_response_free.
 */
wf_status wf_ozone_get_suggestions(wf_xrpc_client *client,
                                    const char **ignore_subjects,
                                    size_t n,
                                    int64_t limit,
                                    const char *cursor,
                                    wf_response *out);

/*
 * Look up a single tag via tools.ozone.moderation.getTag.
 *
 * TODO: getTag is NOT present in the local atproto lexicon snapshot; the NSID
 * is taken from the current upstream lexicon and should be confirmed. Returns
 * the raw response body. On WF_OK *out_json is an owned NUL-terminated JSON
 * string freed with free(). Returns WF_ERR_INVALID_ARG for NULL client/tag/
 * out_json, or a transport error.
 */
wf_status wf_ozone_get_tag(wf_xrpc_client *client,
                            const char *tag,
                            char **out_json);

/*
 * Query tags via tools.ozone.moderation.queryTags.
 *
 * TODO: queryTags is NOT present in the local atproto lexicon snapshot; the
 * NSID is taken from the current upstream lexicon and should be confirmed.
 * On WF_OK *out_json is an owned NUL-terminated JSON string freed with free().
 */
wf_status wf_ozone_query_tags(wf_xrpc_client *client,
                               int64_t limit,
                               const char *cursor,
                               char **out_json);

/* ------------------------------------------------------------------ */
/* Communication templates (tools.ozone.communication.*)             */
/* ------------------------------------------------------------------ */

/*
 * List communication templates via tools.ozone.communication.listTemplates.
 *
 * On WF_OK, *out is populated with the raw server response freed with
 * wf_response_free. Decode a successful reply with
 * wf_ozone_list_communication_templates_parse. Returns WF_ERR_INVALID_ARG for
 * NULL client/out, or a transport error.
 */
wf_status wf_ozone_list_communication_templates(
    wf_xrpc_client *client,
    wf_response *out);

/*
 * Decode a successful wf_ozone_list_communication_templates reply into an
 * owning typed value. On WF_OK, *out must be released with
 * wf_lex_tools_ozone_communication_list_templates_main_output_free.
 */
wf_status wf_ozone_list_communication_templates_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_communication_list_templates_main_output **out);

/*
 * Create a communication template via tools.ozone.communication.createTemplate.
 *
 * `input` is the borrowed generated input struct. On WF_OK, *out is populated
 * with the raw server response freed with wf_response_free (no output decoder
 * is generated for this lexicon). Returns WF_ERR_INVALID_ARG for NULL
 * client/input/out, or a transport error.
 */
wf_status wf_ozone_create_communication_template(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_communication_create_template_main_input *input,
    wf_response *out);

/*
 * Update a communication template via tools.ozone.communication.updateTemplate.
 *
 * `input` is the borrowed generated input struct. On WF_OK, *out is populated
 * with the raw server response freed with wf_response_free. Returns
 * WF_ERR_INVALID_ARG for NULL client/input/out, or a transport error.
 */
wf_status wf_ozone_update_communication_template(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_communication_update_template_main_input *input,
    wf_response *out);

/*
 * Delete a communication template via tools.ozone.communication.deleteTemplate.
 *
 * `input` is the borrowed generated input struct ({ "id": ... }). On WF_OK,
 * *out is populated with the raw server response freed with wf_response_free.
 * Returns WF_ERR_INVALID_ARG for NULL client/input/out, or a transport error.
 */
wf_status wf_ozone_delete_communication_template(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_communication_delete_template_main_input *input,
    wf_response *out);

/* ------------------------------------------------------------------ */
/* Set values (tools.ozone.set.*)                                     */
/* ------------------------------------------------------------------ */

/*
 * Add values to a set via tools.ozone.set.addValues.
 *
 * `input` is the borrowed generated input struct ({ "name", "values" }).
 * On WF_OK, *out is populated with the raw server response freed with
 * wf_response_free. Returns WF_ERR_INVALID_ARG for NULL client/input/out, or a
 * transport error.
 */
wf_status wf_ozone_set_add_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_add_values_main_input *input,
    wf_response *out);

/*
 * Delete values from a set via tools.ozone.set.deleteValues.
 *
 * `input` is the borrowed generated input struct ({ "name", "values" }).
 * On WF_OK, *out is populated with the raw server response freed with
 * wf_response_free. Returns WF_ERR_INVALID_ARG for NULL client/input/out, or a
 * transport error.
 */
wf_status wf_ozone_set_delete_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_delete_values_main_input *input,
    wf_response *out);

/*
 * Query the values of a set via tools.ozone.set.getValues.
 *
 * `params` is the borrowed generated params struct
 * (wf_lex_tools_ozone_set_get_values_main_params: name, limit, cursor). On
 * WF_OK, *out is populated with the raw server response freed with
 * wf_response_free. Decode a successful reply with wf_ozone_set_query_values_parse.
 * Returns WF_ERR_INVALID_ARG for NULL client/params/out, or a transport error.
 */
wf_status wf_ozone_set_query_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_get_values_main_params *params,
    wf_response *out);

/*
 * Decode a successful wf_ozone_set_query_values reply into an owning typed
 * value. On WF_OK, *out must be released with
 * wf_lex_tools_ozone_set_get_values_main_output_free.
 */
wf_status wf_ozone_set_query_values_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_set_get_values_main_output **out);

/*
 * Upsert a set's metadata via tools.ozone.set.upsertSet.
 *
 * NOTE: there is no per-value "upsertValues" lexicon; the closest real
 * endpoint is tools.ozone.set.upsertSet, which creates or updates a set's
 * metadata. `input` is the borrowed generated input struct
 * (wf_lex_tools_ozone_set_upsert_set_main_input: { name, description }). On
 * WF_OK, *out is populated with the raw server response freed with
 * wf_response_free (no output decoder is generated for this lexicon).
 * Returns WF_ERR_INVALID_ARG for NULL client/input/out, or a transport error.
 */
wf_status wf_ozone_set_upsert_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_upsert_set_main_input *input,
    wf_response *out);

/* ------------------------------------------------------------------ */
/* Moderation — additional typed wrappers                              */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_get_account_timeline(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_account_timeline_main_params *params,
    wf_response *out);
wf_status wf_ozone_get_account_timeline_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_account_timeline_main_output **out);

wf_status wf_ozone_get_records(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_records_main_params *params,
    wf_response *out);
wf_status wf_ozone_get_records_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_records_main_output **out);

wf_status wf_ozone_get_repo(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_repo_main_params *params,
    wf_response *out);

wf_status wf_ozone_get_repos(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_repos_main_params *params,
    wf_response *out);
wf_status wf_ozone_get_repos_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_repos_main_output **out);

wf_status wf_ozone_search_repos(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_search_repos_main_params *params,
    wf_response *out);
wf_status wf_ozone_search_repos_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_search_repos_main_output **out);

wf_status wf_ozone_cancel_scheduled_actions(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_cancel_scheduled_actions_main_input *input,
    wf_response *out);

wf_status wf_ozone_schedule_action(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_schedule_action_main_input *input,
    wf_response *out);

wf_status wf_ozone_list_scheduled_actions(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_list_scheduled_actions_main_input *input,
    wf_response *out);
wf_status wf_ozone_list_scheduled_actions_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_list_scheduled_actions_main_output **out);

/* ------------------------------------------------------------------ */
/* Queue (tools.ozone.queue.*)                                         */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_queue_assign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_assign_moderator_main_input *input,
    wf_response *out);

wf_status wf_ozone_queue_create_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_create_queue_main_input *input,
    wf_response *out);
wf_status wf_ozone_queue_create_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_create_queue_main_output **out);

wf_status wf_ozone_queue_delete_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_delete_queue_main_input *input,
    wf_response *out);
wf_status wf_ozone_queue_delete_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_delete_queue_main_output **out);

wf_status wf_ozone_queue_get_assignments(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_get_assignments_main_params *params,
    wf_response *out);
wf_status wf_ozone_queue_get_assignments_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_get_assignments_main_output **out);

wf_status wf_ozone_queue_list_queues(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_list_queues_main_params *params,
    wf_response *out);
wf_status wf_ozone_queue_list_queues_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_list_queues_main_output **out);

wf_status wf_ozone_queue_route_reports(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_route_reports_main_input *input,
    wf_response *out);
wf_status wf_ozone_queue_route_reports_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_route_reports_main_output **out);

wf_status wf_ozone_queue_unassign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_unassign_moderator_main_input *input,
    wf_response *out);

wf_status wf_ozone_queue_update_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_update_queue_main_input *input,
    wf_response *out);
wf_status wf_ozone_queue_update_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_update_queue_main_output **out);

/* ------------------------------------------------------------------ */
/* Report (tools.ozone.report.*)                                       */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_report_assign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_assign_moderator_main_input *input,
    wf_response *out);

wf_status wf_ozone_report_create_activity(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_create_activity_main_input *input,
    wf_response *out);
wf_status wf_ozone_report_create_activity_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_create_activity_main_output **out);

wf_status wf_ozone_report_get_assignments(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_assignments_main_params *params,
    wf_response *out);
wf_status wf_ozone_report_get_assignments_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_assignments_main_output **out);

wf_status wf_ozone_report_get_historical_stats(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_historical_stats_main_params *params,
    wf_response *out);
wf_status wf_ozone_report_get_historical_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_historical_stats_main_output **out);

wf_status wf_ozone_report_get_latest_report(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_latest_report_main_params *params,
    wf_response *out);
wf_status wf_ozone_report_get_latest_report_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_latest_report_main_output **out);

wf_status wf_ozone_report_get_live_stats(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_live_stats_main_params *params,
    wf_response *out);
wf_status wf_ozone_report_get_live_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_live_stats_main_output **out);

wf_status wf_ozone_report_get_report(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_report_main_params *params,
    wf_response *out);

wf_status wf_ozone_report_list_activities(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_list_activities_main_params *params,
    wf_response *out);
wf_status wf_ozone_report_list_activities_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_list_activities_main_output **out);

wf_status wf_ozone_report_query_activities(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_query_activities_main_params *params,
    wf_response *out);
wf_status wf_ozone_report_query_activities_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_query_activities_main_output **out);

wf_status wf_ozone_report_query_reports(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_query_reports_main_params *params,
    wf_response *out);
wf_status wf_ozone_report_query_reports_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_query_reports_main_output **out);

wf_status wf_ozone_report_reassign_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_reassign_queue_main_input *input,
    wf_response *out);
wf_status wf_ozone_report_reassign_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_reassign_queue_main_output **out);

wf_status wf_ozone_report_refresh_stats(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_refresh_stats_main_input *input,
    wf_response *out);
wf_status wf_ozone_report_refresh_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_refresh_stats_main_output **out);

wf_status wf_ozone_report_unassign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_unassign_moderator_main_input *input,
    wf_response *out);

/* ------------------------------------------------------------------ */
/* Team (tools.ozone.team.*)                                           */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_team_add_member(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_add_member_main_input *input,
    wf_response *out);

wf_status wf_ozone_team_delete_member(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_delete_member_main_input *input,
    wf_response *out);

wf_status wf_ozone_team_list_members(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_list_members_main_params *params,
    wf_response *out);
wf_status wf_ozone_team_list_members_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_team_list_members_main_output **out);

wf_status wf_ozone_team_update_member(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_update_member_main_input *input,
    wf_response *out);

/* ------------------------------------------------------------------ */
/* Verification (tools.ozone.verification.*)                           */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_verification_grant(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_verification_grant_verifications_main_input *input,
    wf_response *out);
wf_status wf_ozone_verification_grant_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_verification_grant_verifications_main_output **out);

wf_status wf_ozone_verification_list(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_verification_list_verifications_main_params *params,
    wf_response *out);
wf_status wf_ozone_verification_list_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_verification_list_verifications_main_output **out);

wf_status wf_ozone_verification_revoke(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_verification_revoke_verifications_main_input *input,
    wf_response *out);
wf_status wf_ozone_verification_revoke_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_verification_revoke_verifications_main_output **out);

/* ------------------------------------------------------------------ */
/* Signature (tools.ozone.signature.*)                                 */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_signature_find_correlation(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_signature_find_correlation_main_params *params,
    wf_response *out);
wf_status wf_ozone_signature_find_correlation_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_signature_find_correlation_main_output **out);

wf_status wf_ozone_signature_find_related_accounts(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_signature_find_related_accounts_main_params *params,
    wf_response *out);
wf_status wf_ozone_signature_find_related_accounts_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_signature_find_related_accounts_main_output **out);

wf_status wf_ozone_signature_search_accounts(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_signature_search_accounts_main_params *params,
    wf_response *out);
wf_status wf_ozone_signature_search_accounts_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_signature_search_accounts_main_output **out);

/* ------------------------------------------------------------------ */
/* Setting (tools.ozone.setting.*)                                     */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_setting_list_options(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_setting_list_options_main_params *params,
    wf_response *out);
wf_status wf_ozone_setting_list_options_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_setting_list_options_main_output **out);

wf_status wf_ozone_setting_remove_options(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_setting_remove_options_main_input *input,
    wf_response *out);
wf_status wf_ozone_setting_remove_options_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_setting_remove_options_main_output **out);

wf_status wf_ozone_setting_upsert_option(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_setting_upsert_option_main_input *input,
    wf_response *out);
wf_status wf_ozone_setting_upsert_option_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_setting_upsert_option_main_output **out);

/* ------------------------------------------------------------------ */
/* Hosting (tools.ozone.hosting.*)                                     */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_hosting_get_account_history(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_hosting_get_account_history_main_params *params,
    wf_response *out);
wf_status wf_ozone_hosting_get_account_history_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_hosting_get_account_history_main_output **out);

/* ------------------------------------------------------------------ */
/* Server (tools.ozone.server.*)                                       */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_server_get_config(
    wf_xrpc_client *client,
    wf_response *out);
wf_status wf_ozone_server_get_config_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_server_get_config_main_output **out);

/* ------------------------------------------------------------------ */
/* Safelink (tools.ozone.safelink.*)                                   */
/* ------------------------------------------------------------------ */

wf_status wf_ozone_safelink_add_rule(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_add_rule_main_input *input,
    wf_response *out);

wf_status wf_ozone_safelink_query_events(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_query_events_main_input *input,
    wf_response *out);
wf_status wf_ozone_safelink_query_events_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_safelink_query_events_main_output **out);

wf_status wf_ozone_safelink_query_rules(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_query_rules_main_input *input,
    wf_response *out);
wf_status wf_ozone_safelink_query_rules_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_safelink_query_rules_main_output **out);

wf_status wf_ozone_safelink_remove_rule(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_remove_rule_main_input *input,
    wf_response *out);

wf_status wf_ozone_safelink_update_rule(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_update_rule_main_input *input,
    wf_response *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OZONE_H */
