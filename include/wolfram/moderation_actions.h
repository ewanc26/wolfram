/*
 * moderation_actions.h — typed parsers and agent wrappers for moderation /
 * social-graph *action* XRPC procedures (unlike the query-based list parsers
 * in moderation_typed.h / graph_typed.h, these are POST procedures that take a
 * JSON input body and return a typed output envelope).
 *
 *   - com.atproto.moderation.createReport   (procedure)
 *   - app.bsky.graph.muteActorList          (procedure)
 *   - app.bsky.graph.blockActorList         (procedure)
 *
 * Output structs are owned by the caller and released with the matching
 * `_free` function. Parsers follow the conventions of feed_typed.h /
 * notification.c: static strdup/set_string/reset helpers local to the TU,
 * ownership via cJSON, and a full reset-on-error contract so callers never
 * observe a partially-populated struct on failure.
 */

#ifndef WOLFRAM_MODERATION_ACTIONS_H
#define WOLFRAM_MODERATION_ACTIONS_H

#include "wolfram/agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Result of com.atproto.moderation.createReport.
 *
 * `id` is the server-assigned report id, captured as a string (the wire format
 * is an integer; it is formatted into `id` for stable call-site handling).
 * `reason` is the free-text context echoed from the request (may be NULL when
 * the request omitted it). `subject_uri` is the AT-URI (record strongRef) or
 * repo DID (repoRef) the report targets; NULL when the output omitted subject
 * or it was of an unrecognised shape. */
typedef struct wf_moderation_report {
    char *id;            /* server-assigned report id, or NULL */
    char *reason;        /* free-text reason, or NULL */
    char *subject_uri;   /* uri / repo of the reported subject, or NULL */
} wf_moderation_report;

/* A returned app.bsky.graph.defs#listView.
 *
 * This is a LOCAL mirror of the listView shape returned by the muteActorList /
 * blockActorList procedures; it is intentionally distinct from the
 * `wf_agent_list_view` type in graph_typed.h to avoid a name collision. */
typedef struct wf_mod_list_view {
    char *uri;          /* at-uri of the list (required) */
    char *cid;          /* cid of the list record (required) */
    char *name;         /* list display name (required) */
    char *purpose;      /* list purpose token (required) */
    char *description;  /* optional list description */
    char *avatar;       /* optional avatar uri */
} wf_mod_list_view;

/* Result envelope for app.bsky.graph.muteActorList / blockActorList.
 *
 * The procedure returns `{ "list": listView, "cursor": ... }`; `cursor` is
 * optional and NULL when absent. The `list` member is always owned by the
 * struct (never NULL on a successful parse), per the lexicon's required `list`
 * output. */
typedef struct wf_mod_list_view_result {
    wf_mod_list_view list;
    char *cursor;       /* optional pagination cursor, or NULL */
} wf_mod_list_view_result;

/* Parse a raw com.atproto.moderation.createReport JSON body.
 *
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON,
 * WF_ERR_ALLOC on allocation failure, WF_OK on success. On any error `out` is
 * left fully reset (no partial leaks). */
wf_status wf_agent_parse_report(const char *json, size_t json_len,
                                wf_moderation_report *out);

/* Free a parsed moderation report and every owned field it holds. */
void wf_moderation_report_free(wf_moderation_report *report);

/* Parse a raw app.bsky.graph.muteActorList / blockActorList JSON body into a
 * list-view result.
 *
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing `list` object, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset. */
wf_status wf_agent_parse_list_view_result(const char *json, size_t json_len,
                                          wf_mod_list_view_result *out);

/* Free a parsed list-view result and every owned field it holds. */
void wf_mod_list_view_result_free(wf_mod_list_view_result *result);

/* Typed high-level wrappers — build the JSON input, issue the procedure, then
 * parse the response body into `out`. On success `out` is owned by the caller
 * and must be freed with the matching `_free`; on error it is left reset.
 *
 * wf_agent_create_report builds the `subject` as a recordRef `{ "uri": ... }`
 * when `subject_repo_did` is NULL, or as a repoRef `{ "repo": <did> }` when a
 * repo DID is supplied. `reason` and `subject_repo_did` may be NULL.
 *
 * NULL agent or out -> WF_ERR_INVALID_ARG. */
wf_status wf_agent_create_report(wf_agent *agent, const char *reason,
                                 const char *subject_uri,
                                 const char *subject_repo_did,
                                 wf_moderation_report *out);
wf_status wf_agent_mute_actor_list(wf_agent *agent, const char *list_at_uri,
                                   wf_mod_list_view_result *out);
wf_status wf_agent_block_actor_list(wf_agent *agent, const char *list_at_uri,
                                   wf_mod_list_view_result *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MODERATION_ACTIONS_H */
