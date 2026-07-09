/*
 * actor_status_typed.h — owned typed wrappers for the app.bsky.actor.status
 * namespace: the stored record (app.bsky.actor.status main) and its status view
 * (app.bsky.actor.defs#statusView). See src/actor_status_typed.c for ownership
 * rules and the authoritative wire format.
 *
 * Conventions mirror labeler_typed.h / actor_typed.h: wf_status error codes,
 * static strdup/set_string/reset helpers, ownership via cJSON_DetachItem* into
 * an owned `extra` subtree for open/unbounded fields, and a matching `_free`
 * for every owned struct (a freed/zeroed struct frees safely — idempotent).
 *
 * Supported shapes (cross-referenced against the atproto lexicons):
 *   - wf_actor_status      app.bsky.actor.status record
 *                          (status, createdAt, durationMinutes, embed)
 *   - wf_actor_status_view app.bsky.actor.defs#statusView
 *                          (uri, cid, status, record-derived fields,
 *                           embed, expiresAt, isActive, isDisabled; plus the
 *                           task-described envelope fields actor/lastUpdated/
 *                           viewerState when present)
 *
 * Parsers (wf_actor_status_parse_*):
 *   - wf_actor_status_parse_record  parse a stored record JSON body
 *   - wf_actor_status_parse_view    parse a statusView JSON body
 *
 * Builder (wf_actor_status_build_record): owned struct/inputs -> owned JSON
 * string (caller frees with free()).
 *
 * Agent wrappers (wf_agent_*): convenience calls layered on the generated lex
 * wrappers after syncing auth onto the agent's primary XRPC client. NOTE: the
 * atproto lexicon corpus currently ships only the record type for
 * app.bsky.actor.status and does NOT generate `getActorStatus`/`getStatus`/
 * `putStatus` call helpers; the three wrappers below are therefore honest
 * stubs returning WF_ERR_INVALID_ARG with a TODO (see src/actor_status_typed.c).
 */

#ifndef WOLFRAM_ACTOR_STATUS_TYPED_H
#define WOLFRAM_ACTOR_STATUS_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A stored app.bsky.actor.status record. `embed` and `extra` hold owned
 * detached cJSON subtrees; NULL when absent. */
typedef struct wf_actor_status {
    char *status;            /* status enum token (e.g. "app.bsky.actor.status#live") */
    char *created_at;        /* RFC 3339 datetime (required on the wire) */
    bool has_duration_minutes;
    int64_t duration_minutes;
    cJSON *embed;            /* owned detached embed subtree; NULL absent */
    cJSON *extra;            /* owned detached subtree of unknown fields; NULL absent */
} wf_actor_status;

/* A statusView (app.bsky.actor.defs#statusView). Core/record-derived fields are
 * copied; any other fields remain in owned `extra`. The optional task-described
 * envelope fields (actor/lastUpdated/viewerState) are tolerated if present. */
typedef struct wf_actor_status_view {
    char *uri;               /* at-uri of the record */
    char *cid;               /* cid of the record */
    char *status;            /* status enum token */
    char *created_at;        /* RFC 3339 datetime (from the record) */
    bool has_duration_minutes;
    int64_t duration_minutes;
    cJSON *embed;            /* owned detached embed subtree; NULL absent */
    char *expires_at;        /* RFC 3339 datetime; NULL absent */
    bool has_is_active;
    bool is_active;
    bool has_is_disabled;
    bool is_disabled;
    char *actor;             /* did (task envelope field; NULL absent) */
    char *last_updated;      /* RFC 3339 datetime (task envelope field) */
    cJSON *viewer_state;     /* owned detached #viewerState subtree; NULL absent */
    cJSON *extra;            /* owned detached subtree of unknown fields; NULL absent */
} wf_actor_status_view;

/* The putStatus output: { uri, cid, value }. Mirrors the record-write shape;
 * `value` is the stored record as an owned JSON string. */
typedef struct wf_actor_status_put_result {
    char *uri;
    char *cid;
    char *value;
} wf_actor_status_put_result;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a stored app.bsky.actor.status record JSON body. */
wf_status wf_actor_status_parse_record(const char *json, size_t json_len,
                                       wf_actor_status *out);

/* Parse an app.bsky.actor.defs#statusView JSON body. */
wf_status wf_actor_status_parse_view(const char *json, size_t json_len,
                                     wf_actor_status_view *out);

/* ---- Builder (owned inputs -> owned JSON string) ----
 * Build a stored app.bsky.actor.status record JSON string. `embed` may be NULL
 * (omitted). On WF_OK, `*out_json` is a heap string the caller frees with
 * free(). Returns WF_ERR_INVALID_ARG when required inputs are NULL/empty. */
wf_status wf_actor_status_build_record(const char *created_at,
                                       const char *status, const cJSON *embed,
                                       char **out_json);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_actor_status_free(wf_actor_status *r);
void wf_actor_status_view_free(wf_actor_status_view *v);
void wf_actor_status_put_result_free(wf_actor_status_put_result *r);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding lex call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty.
 *
 * NOTE: the atproto lexicon corpus currently provides no generated call helper
 * for app.bsky.actor.status query/procedure endpoints
 * (getActorStatus/getStatus/putStatus), so these are honest stubs returning
 * WF_ERR_INVALID_ARG with a TODO until the generated lex bindings exist. The
 * parsers/builder above are fully functional and network-independent. */
wf_status wf_agent_get_actor_status(wf_agent *agent, const char *actor,
                                    wf_actor_status_view *out);
wf_status wf_agent_get_status(wf_agent *agent, wf_actor_status_view *out);
wf_status wf_agent_put_status(wf_agent *agent, const wf_actor_status *in,
                              wf_actor_status_put_result *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_ACTOR_STATUS_TYPED_H */
