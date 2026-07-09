/*
 * ozone_admin_typed.h — owned typed parsers + agent convenience wrappers for
 * the tools.ozone.moderation.* endpoints that are NOT yet covered by the
 * general tools.ozone.* typed layer in wolfram/ozone_typed.h.
 *
 * These endpoints all call the Ozone MODERATION SERVICE (a different host than
 * the PDS) and require admin/service auth. The agent wrappers therefore mirror
 * EXACTLY the idiom established in wolfram/ozone_typed.h:
 *   - wf_agent_sync_auth(agent) syncs the service auth onto the agent's
 *     primary XRPC client, and
 *   - the generated owning lex wrappers in wolfram/atproto_lex.h
 *     (wf_lex_tools_ozone_*_main_call + *_main_output_decode_json +
 *     *_main_output_free) perform the wire call and own the decoded output.
 * No new auth path is invented; any endpoint whose lexicon defines no owning
 * output decoder returns the raw wf_response (freed with wf_response_free).
 *
 * Endpoints added here (all currently uncovered in ozone_typed.h):
 *   - moderation.getRecords (Q, owning output)
 *   - moderation.getRepos   (Q, owning output)
 *   - moderation.getAccountTimeline (Q, owning output)
 *   - moderation.searchRepos (Q, owning output)
 *   - moderation.listScheduledActions (P, owning output)
 *   - moderation.getRecord  (QR, raw wf_response output)
 *   - moderation.getRepo    (QR, raw wf_response output)
 *   - moderation.cancelScheduledActions (PR, raw wf_response output)
 *   - moderation.scheduleAction (PR, raw wf_response output)
 *
 * Ownership summary:
 *   - (wf_lex_tools_ozone_moderation_*_main_output *) parse/wrapper results
 *     are freed with the matching generated *_main_output_free.
 *   - (wf_response *) results (raw-output endpoints) are freed with
 *     wf_response_free.
 */

#ifndef WOLFRAM_OZONE_ADMIN_TYPED_H
#define WOLFRAM_OZONE_ADMIN_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Generated-decode wrappers + parsers                                 */
/* (declarations generated from the endpoint table below)              */
/* ------------------------------------------------------------------ */

/* Query with params, owning output decoder. */
#define WF_OZONE_ADMIN_DECL_Q(ns, op, genop)                                \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,     \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);            \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);

/* Procedure with input, owning output decoder. */
#define WF_OZONE_ADMIN_DECL_P(ns, op, genop)                                \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,       \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);            \
    wf_status wf_ozone_parse_##ns##_##op(                                  \
        const char *json, size_t json_len,                                 \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out);

/* Procedure with input, raw wf_response output. */
#define WF_OZONE_ADMIN_DECL_PR(ns, op, genop)                               \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,       \
        wf_response *out);

/* Query with params, raw wf_response output. */
#define WF_OZONE_ADMIN_DECL_QR(ns, op, genop)                               \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,     \
        wf_response *out);

#define WF_OZONE_ADMIN_ENDPOINTS                                            \
    X(moderation, getRecords, get_records, Q)                              \
    X(moderation, getRepos, get_repos, Q)                                  \
    X(moderation, getAccountTimeline, get_account_timeline, Q)            \
    X(moderation, searchRepos, search_repos, Q)                            \
    X(moderation, listScheduledActions, list_scheduled_actions, P)         \
    X(moderation, getRecord, get_record, QR)                              \
    X(moderation, getRepo, get_repo, QR)                                    \
    X(moderation, cancelScheduledActions, cancel_scheduled_actions, PR)    \
    X(moderation, scheduleAction, schedule_action, PR)

#define X(ns, op, genop, kind) WF_OZONE_ADMIN_DECL_##kind(ns, op, genop)
WF_OZONE_ADMIN_ENDPOINTS
#undef X

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OZONE_ADMIN_TYPED_H */
