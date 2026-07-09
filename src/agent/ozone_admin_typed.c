/*
 * ozone_admin_typed.c — owned typed parsers + agent convenience wrappers for
 * the tools.ozone.moderation.* endpoints not yet covered by the general layer
 * in src/ozone_typed.c. See include/wolfram/ozone_admin_typed.h for the public
 * API, the authoritative wire format, and ownership rules.
 *
 * The agent wrappers reuse the generated owning lex wrappers in
 * wolfram/atproto_lex.h (call + decode + free) after syncing auth onto the
 * agent's primary XRPC client via wf_agent_sync_auth(agent) — the same idiom
 * used by the existing ozone_typed.c wrappers (moderation.get/query,
 * team.listMembers, etc.). The Ozone service endpoint is resolved by the
 * agent's XRPC client host; no new auth path is invented. Endpoints whose
 * lexicon defines no owning output decoder (getRecord, getRepo,
 * cancelScheduledActions, scheduleAction) return the raw wf_response, freed by
 * the caller with wf_response_free.
 */

#include "wolfram/ozone_admin_typed.h"

#include "_internal.h"
#include "wolfram/atproto_lex.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Generated-decode wrapper definitions                                */
/* ------------------------------------------------------------------ */

#define WF_OZONE_ADMIN_DEF_Q(ns, op, genop)                                 \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,     \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {           \
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
        wf_response_free(&res);                                            \
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

#define WF_OZONE_ADMIN_DEF_P(ns, op, genop)                                 \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,       \
        wf_lex_tools_ozone_##ns##_##genop##_main_output **out) {           \
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
        wf_response_free(&res);                                            \
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

#define WF_OZONE_ADMIN_DEF_PR(ns, op, genop)                                \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_input *input,       \
        wf_response *out) {                                                \
        if (!agent || !agent->client || !input || !out) {                  \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        wf_agent_sync_auth(agent);                                         \
        return wf_lex_tools_ozone_##ns##_##genop##_main_call(              \
            agent->client, input, out);                                     \
    }

#define WF_OZONE_ADMIN_DEF_QR(ns, op, genop)                                \
    wf_status wf_ozone_##ns##_##op(                                        \
        wf_agent *agent,                                                   \
        const wf_lex_tools_ozone_##ns##_##genop##_main_params *params,     \
        wf_response *out) {                                                \
        if (!agent || !agent->client || !params || !out) {                 \
            return WF_ERR_INVALID_ARG;                                     \
        }                                                                  \
        wf_agent_sync_auth(agent);                                         \
        return wf_lex_tools_ozone_##ns##_##genop##_main_call(              \
            agent->client, params, out);                                    \
    }

#define WF_OZONE_ADMIN_ENDPOINTS                                          \
    X(moderation, getRecords, get_records, Q)                              \
    X(moderation, getRepos, get_repos, Q)                                  \
    X(moderation, getAccountTimeline, get_account_timeline, Q)            \
    X(moderation, searchRepos, search_repos, Q)                            \
    X(moderation, listScheduledActions, list_scheduled_actions, P)         \
    X(moderation, getRecord, get_record, QR)                              \
    X(moderation, getRepo, get_repo, QR)                                    \
    X(moderation, cancelScheduledActions, cancel_scheduled_actions, PR)    \
    X(moderation, scheduleAction, schedule_action, PR)

#define X(ns, op, genop, kind) WF_OZONE_ADMIN_DEF_##kind(ns, op, genop)
WF_OZONE_ADMIN_ENDPOINTS
#undef X
