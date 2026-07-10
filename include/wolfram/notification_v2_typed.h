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

/* ----------------------------------------------------------------------------
 * Typed builder + parser for putPreferencesV2 (app.bsky.notification)
 *
 * The v2 preferences input/output carries 13 preference slots, each a small
 * object. To let callers set only the fields they care about, every scalar is
 * guarded by a `has_*` flag: a sub-object is omitted entirely unless its
 * owning `has_<slot>` flag is set, and a scalar inside a sub-object is omitted
 * unless its own `has_*` flag is set. The OUTPUT (`defs#preferences`) lists all
 * 13 slots as `required`, so a parse of a real response populates every field.
 *
 * `include` is a closed enum: filterablePreference uses "all"/"follows" and the
 * deprecated chatPreference uses "all"/"accepted". The same enum covers both
 * since "all" is shared; setting an out-of-range value is an error for the
 * builder.
 * ------------------------------------------------------------------------- */

typedef enum wf_notif_v2_include {
    WF_NOTIF_V2_INCLUDE_UNSET = 0,
    WF_NOTIF_V2_INCLUDE_ALL,
    WF_NOTIF_V2_INCLUDE_FOLLOWS, /* filterablePreference */
    WF_NOTIF_V2_INCLUDE_ACCEPTED /* chatPreference (deprecated) */
} wf_notif_v2_include;

/* A filterablePreference: { include?, list?, push? }. */
typedef struct wf_notif_v2_filterable_pref {
    int              has_include;
    wf_notif_v2_include include; /* valid only when has_include != 0 */
    int              has_list;
    int              list;        /* boolean when has_list != 0 */
    int              has_push;
    int              push;        /* boolean when has_push != 0 */
} wf_notif_v2_filterable_pref;

/* A plain preference: { list?, push? }. */
typedef struct wf_notif_v2_pref {
    int has_list;
    int list;  /* boolean when has_list != 0 */
    int has_push;
    int push;  /* boolean when has_push != 0 */
} wf_notif_v2_pref;

/* The deprecated chatPreference: { include?, push? }. */
typedef struct wf_notif_v2_chat_pref {
    int              has_include;
    wf_notif_v2_include include; /* valid only when has_include != 0 */
    int              has_push;
    int              push;        /* boolean when has_push != 0 */
} wf_notif_v2_chat_pref;

/* All 13 v2 preference slots. A slot is emitted only when `has_<slot>` != 0. */
typedef struct wf_notification_v2_preferences {
    int                   has_chat;
    wf_notif_v2_chat_pref chat;

    int                        has_follow;
    wf_notif_v2_filterable_pref follow;
    int                        has_like;
    wf_notif_v2_filterable_pref like;
    int                        has_like_via_repost;
    wf_notif_v2_filterable_pref like_via_repost;
    int                        has_mention;
    wf_notif_v2_filterable_pref mention;
    int                        has_quote;
    wf_notif_v2_filterable_pref quote;
    int                        has_reply;
    wf_notif_v2_filterable_pref reply;
    int                        has_repost;
    wf_notif_v2_filterable_pref repost;
    int                        has_repost_via_repost;
    wf_notif_v2_filterable_pref repost_via_repost;

    int                   has_starterpack_joined;
    wf_notif_v2_pref      starterpack_joined;
    int                   has_subscribed_post;
    wf_notif_v2_pref      subscribed_post;
    int                   has_unverified;
    wf_notif_v2_pref      unverified;
    int                   has_verified;
    wf_notif_v2_pref      verified;
} wf_notification_v2_preferences;

/*
 * Serialize `prefs` into the exact putPreferencesV2 input JSON. Only the slots
 * whose `has_<slot>` flag is set are emitted, and within a slot only the scalar
 * fields whose `has_*` flag is set. `*out_json` is owned by the caller and must
 * be released with free() (never NULL on success). Returns WF_ERR_INVALID_ARG
 * if `prefs` or `out_json` is NULL, or WF_ERR_INVALID_ARG if a set `include`
 * value is out of range. Returns WF_ERR_ALLOC on allocation failure.
 */
wf_status wf_notification_v2_preferences_build(
    const wf_notification_v2_preferences *prefs, char **out_json);

/*
 * Parse a `#preferences` object (or a putPreferencesV2 output body
 * `{ preferences: {...} }`) into the owned typed struct. All 13 slots are
 * `required` on the wire, so a valid response populates every field; unknown
 * fields are ignored. Returns WF_ERR_INVALID_ARG if `json` or `out` is NULL, or
 * WF_ERR_PARSE if the document is not a JSON object / lacks a preferences
 * object. On error `out` is left zeroed.
 */
wf_status wf_notification_v2_preferences_parse(const char *json, size_t len,
                                               wf_notification_v2_preferences *out);

/* Zero an owned typed preferences struct. Safe to call on a zeroed struct. */
void wf_notification_v2_preferences_free(wf_notification_v2_preferences *p);

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
 * Set the authenticated account's v2 notification preferences from a typed
 * struct (no hand-built JSON). `prefs` must be non-NULL. The input is built via
 * wf_notification_v2_preferences_build and sent through the generated
 * putPreferencesV2 call (after syncing auth). When `out` is non-NULL the
 * response `#preferences` object is parsed into it (owned). Returns
 * WF_ERR_INVALID_ARG if `agent` or `prefs` is NULL, or a transport/parse error.
 */
wf_status wf_agent_put_notification_preferences_v2_typed(
    wf_agent *agent, const wf_notification_v2_preferences *prefs,
    wf_notification_v2_preferences *out);

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
