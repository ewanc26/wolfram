/*
 * notification_typed.h — owned typed wrappers for the app.bsky.notification
 * CORE endpoints: listNotifications, getUnreadCount, updateSeen, registerPush,
 * and unregisterPush.
 *
 * This module is DISTINCT from:
 *   - notification_v2_typed.h (v2 preferences + activity subscriptions),
 *   - agent.h's wf_agent_notification / wf_agent_list_notifications_typed
 *     (a separate, simpler typed view), and
 *   - agent.h's wf_agent_get_unread_count_typed (which writes an `int`).
 *
 * The richer `wf_notification_view` here reuses the `wf_agent_profile_view`
 * (app.bsky.actor.defs#profileView core fields) for the author and owns the
 * full reason union, the embedded record (as a detached cJSON subtree), and
 * any trailing fields via an `extra` subtree.
 *
 * Conventions mirror actor_typed.h / labeler_typed.c: wf_status error codes,
 * static strdup/set_string/reset helpers, ownership via cJSON_DetachItem*,
 * and a matching `_free` for every owned struct (a freed/zeroed struct frees
 * safely). Every owned string is heap-allocated; `extra`/`record`/`labels`
 * hold an owned detached cJSON subtree of unbound fields.
 */

#ifndef WOLFRAM_NOTIFICATION_TYPED_H
#define WOLFRAM_NOTIFICATION_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A rich notification view (app.bsky.notification.listNotifications#notification
 * plus a few context fields). `author` is a profileView core view; `record` is
 * the embedded record kept verbatim (its shape varies by `reason`); `labels`
 * and `extra` capture open/unbounded fields. */
typedef struct wf_notification_view {
    char *uri;
    char *cid;
    wf_agent_profile_view author;   /* did/handle/displayName/avatar */
    char *reason;                   /* owned reason string (like/repost/... ) */
    char *reason_subject;           /* AT-URI subject; NULL when absent */
    cJSON *record;                  /* owned detached record subtree; NULL absent */
    bool is_read;
    char *indexed_at;
    cJSON *labels;                  /* owned detached labels array; NULL absent */
    cJSON *extra;                   /* owned detached subtree of other fields */
} wf_notification_view;

/* A list of rich notification views (listNotifications output). */
typedef struct wf_notification_list {
    wf_notification_view *items;
    size_t count;
    char *cursor;
    char *seen_at;
    bool has_priority;
    bool priority;
} wf_notification_list;

/* An unread-count result (getUnreadCount output). */
typedef struct wf_notification_unread_count {
    int64_t count;
} wf_notification_unread_count;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a listNotifications JSON body ("notifications" array, optional "cursor"
 * / "seenAt" / "priority"). */
wf_status wf_notification_parse_list(const char *json, size_t json_len,
                                     wf_notification_list *out);

/* Parse a getUnreadCount JSON body ("count"). */
wf_status wf_notification_parse_unread_count(const char *json, size_t json_len,
                                             wf_notification_unread_count *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_notification_list_free(wf_notification_list *list);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding lex call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty. The wrappers require a live agent (they perform network I/O);
 * offline callers should use the parsers above. */

/* listNotifications — richer than wf_agent_list_notifications_typed. */
wf_status wf_agent_list_notifications_rich_typed(wf_agent *agent, int limit,
                                                 const char *cursor,
                                                 wf_notification_list *out);

/* getUnreadCount — writes the int64 `count`. NOTE: this is intentionally named
 * `_rich_typed` to avoid colliding with the existing `wf_agent_get_unread_count_typed`
 * (agent.h) which writes an `int`. */
wf_status wf_agent_get_unread_count_rich_typed(wf_agent *agent,
                                               int64_t *out_count);

/* updateSeen — notify that notifications have been seen at `seen_at`
 * (RFC 3339 datetime). Returns WF_OK on success. */
wf_status wf_agent_update_seen_typed(wf_agent *agent, const char *seen_at);

/* registerPush — register `token` with `service_did` for the given `platform`
 * (ios/android/web) and `app_id`. `has_age_restricted`/`age_restricted` are
 * optional. Returns WF_OK on success. */
wf_status wf_agent_register_push_typed(wf_agent *agent, const char *service_did,
                                       const char *token, const char *platform,
                                       const char *app_id, bool has_age_restricted,
                                       bool age_restricted);

/* unregisterPush — the inverse of registerPush for the given `token` /
 * `service_did` / `platform` / `app_id`. Returns WF_OK on success. */
wf_status wf_agent_unregister_push_typed(wf_agent *agent, const char *service_did,
                                         const char *token, const char *platform,
                                         const char *app_id);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_NOTIFICATION_TYPED_H */
