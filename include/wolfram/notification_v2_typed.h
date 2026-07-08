/*
 * notification_v2_typed.h — owned "typed" wrappers for the newer
 * app.bsky.notification preferences v2 and activity subscription endpoints.
 *
 * Wire formats (authoritative, per app.bsky.notification lexicons and the
 * generated wrappers in wolfram/atproto_lex.h):
 *
 *   putPreferencesV2 (procedure)
 *     input : { chat?, follow?, like?, likeViaRepost?, mention?, quote?,
 *               reply?, repost?, repostViaRepost?, starterpackJoined?,
 *               subscribedPost?, unverified?, verified? }  (one object per
 *               preference kind; see app.bsky.notification.defs)
 *     output: { preferences: {...} }
 *     NOTE: the generated input is NOT a flat `preferences[]`/`deletedPrefs[]`
 *     array — it is a structured object with one optional field per preference
 *     kind. wf_agent_put_notification_preferences_v2 therefore accepts the full
 *     input as a raw JSON string and maps each known key onto the structured
 *     generated input. The `deleted_prefs`/`deleted_count` arguments are kept
 *     for API parity but the current lexicon has no deletedPrefs field; passing
 *     a non-zero count is rejected with WF_ERR_INVALID_ARG.
 *
 *   listActivitySubscriptions (query)
 *     output: { cursor?, subscriptions: app.bsky.actor.defs#profileView[] }
 *     NOTE: the items are profileView objects (the subscribed accounts), not
 *     activitySubscription objects. Each is parsed as a bounded owned view with
 *     the remaining object fields detached into `extra`.
 *
 *   putActivitySubscription (procedure)
 *     input : { subject: did, activitySubscription: { post, reply } }
 *     output: { subject: did, activitySubscription?: { post, reply } }
 *
 * Every owning output has a matching `_free`. Open / union sub-objects are
 * represented as an owned detached cJSON `extra` so the parsers stay bounded.
 */

#ifndef WOLFRAM_NOTIFICATION_V2_TYPED_H
#define WOLFRAM_NOTIFICATION_V2_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- activitySubscription (#activitySubscription: post/reply booleans) ---- */

typedef struct wf_notif_v2_activity_subscription {
    int    has_post;     /* 1 if `post` was present in the source JSON */
    int    post;         /* boolean value when has_post != 0 */
    int    has_reply;    /* 1 if `reply` was present in the source JSON */
    int    reply;        /* boolean value when has_reply != 0 */
    cJSON *extra;        /* owned remaining object fields; NULL if absent */
} wf_notif_v2_activity_subscription;

/* ---- putActivitySubscription output ---- */

typedef struct wf_notif_v2_activity_subscription_result {
    char   *subject;     /* owned did string; NULL if absent */
    int     has_activity_subscription;
    wf_notif_v2_activity_subscription activity_subscription;
    cJSON  *extra;       /* owned remaining object fields; NULL if absent */
} wf_notif_v2_activity_subscription_result;

/* ---- listActivitySubscriptions output (subscriptions: profileView[]) ---- */

typedef struct wf_notif_v2_subscription_view {
    char   *did;
    char   *handle;
    char   *display_name;
    char   *avatar;
    cJSON  *extra;       /* owned remaining profileView fields; NULL if absent */
} wf_notif_v2_subscription_view;

typedef struct wf_notif_v2_subscription_view_list {
    wf_notif_v2_subscription_view *items;
    size_t                         count;
    char                          *cursor;
} wf_notif_v2_subscription_view_list;

/* ---- putPreferencesV2 output (preferences object) ---- */

typedef struct wf_notif_v2_preferences {
    cJSON *preferences;  /* owned full preferences object; NULL if absent */
} wf_notif_v2_preferences;

/*
 * Parse a putActivitySubscription response body
 * (`{ subject?, activitySubscription? }`) into an owned result. On any error
 * `out` is left zeroed and any partial allocations are released.
 */
wf_status wf_notif_v2_parse_put_activity_subscription(
    const char *json, size_t json_len,
    wf_notif_v2_activity_subscription_result *out);

/*
 * Parse a listActivitySubscriptions response body
 * (`{ cursor?, subscriptions: profileView[] }`) into an owned list. On any
 * error `out` is left zeroed and any partial allocations are released.
 */
wf_status wf_notif_v2_parse_list_activity_subscriptions(
    const char *json, size_t json_len,
    wf_notif_v2_subscription_view_list *out);

/*
 * Parse a putPreferencesV2 response body (`{ preferences: {...} }`) into an
 * owned preferences object. On any error `out` is left zeroed and any partial
 * allocation is released.
 */
wf_status wf_notif_v2_parse_preferences(const char *json, size_t json_len,
                                        wf_notif_v2_preferences *out);

/* Release owned state. Safe to call on a zeroed struct. */
void wf_notif_v2_activity_subscription_result_free(
    wf_notif_v2_activity_subscription_result *r);
void wf_notif_v2_subscription_view_list_free(
    wf_notif_v2_subscription_view_list *l);
void wf_notif_v2_preferences_free(wf_notif_v2_preferences *p);

/*
 * Set the authenticated account's v2 notification preferences.
 *
 * `preferences_json` is the full putPreferencesV2 input object as a raw JSON
 * string (e.g. `{"like":{"include":"all","list":true,"push":false}}`). It is
 * mapped onto the structured generated input. `deleted_prefs`/`deleted_count`
 * are reserved for API parity; the current lexicon has no deletedPrefs field,
 * so a non-zero `deleted_count` yields WF_ERR_INVALID_ARG. `out` is optional;
 * when non-NULL the response `preferences` object is detached into it (owned).
 * Returns WF_ERR_INVALID_ARG if `agent` or `preferences_json` is NULL/empty, or
 * WF_ERR_PARSE if `preferences_json` is not a JSON object.
 */
wf_status wf_agent_put_notification_preferences_v2(
    wf_agent *agent, const char *preferences_json,
    const char *const *deleted_prefs, size_t deleted_count,
    wf_notif_v2_preferences *out);

/*
 * Enumerate the accounts the authenticated account is subscribed to for
 * activity notifications. `limit` <= 0 means "use the server default";
 * `cursor` may be NULL. On success `*out` holds the owned list.
 * Returns WF_ERR_INVALID_ARG if `agent` or `out` is NULL.
 */
wf_status wf_agent_list_activity_subscriptions(
    wf_agent *agent, int limit, const char *cursor,
    wf_notif_v2_subscription_view_list *out);

/*
 * Put (create or update) a single activity subscription. `subscription_json`
 * is the input object `{ "subject": did, "activitySubscription": {post,reply} }`
 * as a raw JSON string. On success `*out` holds the owned result.
 * Returns WF_ERR_INVALID_ARG if `agent`, `subscription_json`, or `out` is NULL,
 * or WF_ERR_PARSE if the JSON is malformed or missing required fields.
 */
wf_status wf_agent_put_activity_subscription(
    wf_agent *agent, const char *subscription_json,
    wf_notif_v2_activity_subscription_result *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_NOTIFICATION_V2_TYPED_H */
