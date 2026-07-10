/*
 * notification_v2_typed.c — owned typed parsers + agent convenience wrappers
 * for the newer app.bsky.notification preferences v2 and activity subscription
 * endpoints. See include/wolfram/notification_v2_typed.h for the public API and
 * ownership rules. Mirrors contact_typed.c / admin_typed.c: static
 * strdup/set_string/reset helpers, owned strings, and full cleanup on the
 * first error. The agent wrappers call the generated lex wrappers directly
 * (`wf_lex_app_bsky_notification_*_main_call`) after syncing auth onto the
 * agent's primary XRPC client via `wf_agent_sync_auth`.
 */

#include "wolfram/notification_v2_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_notif_v2_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_notif_v2_set_string(char **dst, const char *src) {
    char *copy = wf_notif_v2_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* Remove a known scalar key from `obj` after its value has been copied. */
static void wf_notif_v2_strip_key(cJSON *obj, const char *key) {
    cJSON *item = cJSON_DetachItemFromObject(obj, key);
    if (item) {
        cJSON_Delete(item);
    }
}

/* ---- activitySubscription (#activitySubscription: post/reply) ---- */

static wf_status wf_notif_v2_parse_activity_subscription(
    cJSON *obj, wf_notif_v2_activity_subscription *a) {
    wf_status status = WF_OK;
    cJSON *post = cJSON_GetObjectItemCaseSensitive(obj, "post");
    cJSON *reply = cJSON_GetObjectItemCaseSensitive(obj, "reply");
    if (cJSON_IsBool(post)) {
        a->has_post = 1;
        a->post = cJSON_IsTrue(post);
    }
    if (cJSON_IsBool(reply)) {
        a->has_reply = 1;
        a->reply = cJSON_IsTrue(reply);
    }
    return status;
}

static void wf_notif_v2_activity_subscription_reset(
    wf_notif_v2_activity_subscription *a) {
    if (!a) {
        return;
    }
    if (a->extra) {
        cJSON_Delete(a->extra);
    }
    memset(a, 0, sizeof(*a));
}

/* ---- putActivitySubscription output ---- */

wf_status wf_notif_v2_parse_put_activity_subscription(
    const char *json, size_t json_len,
    wf_notif_v2_activity_subscription_result *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *subject = cJSON_GetObjectItemCaseSensitive(root, "subject");
    if (cJSON_IsString(subject) && subject->valuestring) {
        status = wf_notif_v2_set_string(&out->subject, subject->valuestring);
    }

    if (status == WF_OK) {
        cJSON *as =
            cJSON_GetObjectItemCaseSensitive(root, "activitySubscription");
        if (cJSON_IsObject(as)) {
            out->has_activity_subscription = 1;
            status = wf_notif_v2_parse_activity_subscription(
                as, &out->activity_subscription);
            if (status != WF_OK) {
                wf_notif_v2_activity_subscription_reset(
                    &out->activity_subscription);
            }
        }
    }

    if (status == WF_OK) {
        wf_notif_v2_strip_key(root, "subject");
        wf_notif_v2_strip_key(root, "activitySubscription");
        out->extra = root;
        return WF_OK;
    }

    wf_notif_v2_activity_subscription_result_free(out);
    cJSON_Delete(root);
    return status;
}

void wf_notif_v2_activity_subscription_result_free(
    wf_notif_v2_activity_subscription_result *r) {
    if (!r) {
        return;
    }
    free(r->subject);
    if (r->activity_subscription.extra) {
        cJSON_Delete(r->activity_subscription.extra);
    }
    if (r->extra) {
        cJSON_Delete(r->extra);
    }
    memset(r, 0, sizeof(*r));
}

/* ---- listActivitySubscriptions output (subscriptions: profileView[]) ---- */

static wf_status wf_notif_v2_read_subscription_view(
    cJSON *obj, wf_notif_v2_subscription_view *v) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *dn = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_notif_v2_set_string(&v->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_notif_v2_set_string(&v->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(dn) && dn->valuestring) {
        status = wf_notif_v2_set_string(&v->display_name, dn->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_notif_v2_set_string(&v->avatar, avatar->valuestring);
    }
    return status;
}

static void wf_notif_v2_subscription_view_reset(
    wf_notif_v2_subscription_view *v) {
    if (!v) {
        return;
    }
    free(v->did);
    free(v->handle);
    free(v->display_name);
    free(v->avatar);
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

wf_status wf_notif_v2_parse_list_activity_subscriptions(
    const char *json, size_t json_len,
    wf_notif_v2_subscription_view_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "subscriptions");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_notif_v2_subscription_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_notif_v2_subscription_view *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        /* Capture item pointers up front: detaching later shifts indices. */
        for (size_t i = 0; i < count; ++i) {
            ptrs[i] = cJSON_GetArrayItem(arr, (int)i);
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = ptrs[i];
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_notif_v2_read_subscription_view(obj, &items[i]);
        if (status == WF_OK) {
            wf_notif_v2_strip_key(obj, "did");
            wf_notif_v2_strip_key(obj, "handle");
            wf_notif_v2_strip_key(obj, "displayName");
            wf_notif_v2_strip_key(obj, "avatar");
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_notif_v2_subscription_view_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->items = items;
        out->count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_notif_v2_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_notif_v2_subscription_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_notif_v2_subscription_view_list_free(
    wf_notif_v2_subscription_view_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->count; ++i) {
        wf_notif_v2_subscription_view_reset(&l->items[i]);
    }
    free(l->items);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ---- putPreferencesV2 output (preferences object) ---- */

wf_status wf_notif_v2_parse_preferences(const char *json, size_t json_len,
                                        wf_notif_v2_preferences *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *prefs = cJSON_DetachItemFromObject(root, "preferences");
    if (!cJSON_IsObject(prefs)) {
        cJSON_Delete(prefs);
        cJSON_Delete(root);
        wf_notif_v2_preferences_free(out);
        memset(out, 0, sizeof(*out));
        return WF_ERR_PARSE;
    }

    out->preferences = prefs;
    cJSON_Delete(root);
    return WF_OK;
}

void wf_notif_v2_preferences_free(wf_notif_v2_preferences *p) {
    if (!p) {
        return;
    }
    if (p->preferences) {
        cJSON_Delete(p->preferences);
    }
    memset(p, 0, sizeof(*p));
}

/* ---- putPreferencesV2 input builder (raw JSON -> structured generated input) */

static wf_lex_app_bsky_notification_defs_chat_preference *
wf_notif_v2_make_chat_pref(cJSON *o, wf_status *st) {
    *st = WF_OK;
    wf_lex_app_bsky_notification_defs_chat_preference *p =
        (wf_lex_app_bsky_notification_defs_chat_preference *)calloc(1,
                                                                     sizeof(*p));
    if (!p) {
        *st = WF_ERR_ALLOC;
        return NULL;
    }
    cJSON *include = cJSON_GetObjectItemCaseSensitive(o, "include");
    if (cJSON_IsString(include)) {
        p->include = include->valuestring;
    }
    cJSON *push = cJSON_GetObjectItemCaseSensitive(o, "push");
    if (cJSON_IsBool(push)) {
        p->push = cJSON_IsTrue(push);
    }
    return p;
}

static wf_lex_app_bsky_notification_defs_filterable_preference *
wf_notif_v2_make_filterable_pref(cJSON *o, wf_status *st) {
    *st = WF_OK;
    wf_lex_app_bsky_notification_defs_filterable_preference *p =
        (wf_lex_app_bsky_notification_defs_filterable_preference *)calloc(
            1, sizeof(*p));
    if (!p) {
        *st = WF_ERR_ALLOC;
        return NULL;
    }
    cJSON *include = cJSON_GetObjectItemCaseSensitive(o, "include");
    if (cJSON_IsString(include)) {
        p->include = include->valuestring;
    }
    cJSON *list = cJSON_GetObjectItemCaseSensitive(o, "list");
    if (cJSON_IsBool(list)) {
        p->list = cJSON_IsTrue(list);
    }
    cJSON *push = cJSON_GetObjectItemCaseSensitive(o, "push");
    if (cJSON_IsBool(push)) {
        p->push = cJSON_IsTrue(push);
    }
    return p;
}

static wf_lex_app_bsky_notification_defs_preference *
wf_notif_v2_make_pref(cJSON *o, wf_status *st) {
    *st = WF_OK;
    wf_lex_app_bsky_notification_defs_preference *p =
        (wf_lex_app_bsky_notification_defs_preference *)calloc(1, sizeof(*p));
    if (!p) {
        *st = WF_ERR_ALLOC;
        return NULL;
    }
    cJSON *list = cJSON_GetObjectItemCaseSensitive(o, "list");
    if (cJSON_IsBool(list)) {
        p->list = cJSON_IsTrue(list);
    }
    cJSON *push = cJSON_GetObjectItemCaseSensitive(o, "push");
    if (cJSON_IsBool(push)) {
        p->push = cJSON_IsTrue(push);
    }
    return p;
}

static void wf_notif_v2_prefs_v2_input_free(
    wf_lex_app_bsky_notification_put_preferences_v2_main_input *in) {
    if (!in) {
        return;
    }
    free((void *)in->chat);
    free((void *)in->follow);
    free((void *)in->like);
    free((void *)in->like_via_repost);
    free((void *)in->mention);
    free((void *)in->quote);
    free((void *)in->reply);
    free((void *)in->repost);
    free((void *)in->repost_via_repost);
    free((void *)in->starterpack_joined);
    free((void *)in->subscribed_post);
    free((void *)in->unverified);
    free((void *)in->verified);
    memset(in, 0, sizeof(*in));
}

static wf_status wf_notif_v2_build_prefs_v2_input(
    cJSON *root, wf_lex_app_bsky_notification_put_preferences_v2_main_input *in) {
    wf_status status = WF_OK;
    cJSON *child = root->child;
    while (child) {
        const char *key = child->string;
        if (key) {
            if (strcmp(key, "chat") == 0) {
                in->has_chat = true;
                in->chat = wf_notif_v2_make_chat_pref(child, &status);
            } else if (strcmp(key, "follow") == 0) {
                in->has_follow = true;
                in->follow = wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "like") == 0) {
                in->has_like = true;
                in->like = wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "likeViaRepost") == 0) {
                in->has_like_via_repost = true;
                in->like_via_repost =
                    wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "mention") == 0) {
                in->has_mention = true;
                in->mention = wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "quote") == 0) {
                in->has_quote = true;
                in->quote = wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "reply") == 0) {
                in->has_reply = true;
                in->reply = wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "repost") == 0) {
                in->has_repost = true;
                in->repost = wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "repostViaRepost") == 0) {
                in->has_repost_via_repost = true;
                in->repost_via_repost =
                    wf_notif_v2_make_filterable_pref(child, &status);
            } else if (strcmp(key, "starterpackJoined") == 0) {
                in->has_starterpack_joined = true;
                in->starterpack_joined = wf_notif_v2_make_pref(child, &status);
            } else if (strcmp(key, "subscribedPost") == 0) {
                in->has_subscribed_post = true;
                in->subscribed_post = wf_notif_v2_make_pref(child, &status);
            } else if (strcmp(key, "unverified") == 0) {
                in->has_unverified = true;
                in->unverified = wf_notif_v2_make_pref(child, &status);
            } else if (strcmp(key, "verified") == 0) {
                in->has_verified = true;
                in->verified = wf_notif_v2_make_pref(child, &status);
            }
        }
        if (status != WF_OK) {
            wf_notif_v2_prefs_v2_input_free(in);
            return status;
        }
        child = child->next;
    }
    return WF_OK;
}

/* ---- Typed builder/parser for putPreferencesV2 (13 preference slots) ---- */

static const char *wf_notif_v2_include_to_str(wf_notif_v2_include inc) {
    switch (inc) {
        case WF_NOTIF_V2_INCLUDE_ALL:
            return "all";
        case WF_NOTIF_V2_INCLUDE_FOLLOWS:
            return "follows";
        case WF_NOTIF_V2_INCLUDE_ACCEPTED:
            return "accepted";
        default:
            return NULL;
    }
}

/* Append a sub-object for one filterablePreference slot (include/list/push). */
static void wf_notif_v2_add_filterable(cJSON *root, const char *key,
                                       const wf_notif_v2_filterable_pref *p) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return;
    }
    if (p->has_include) {
        const char *s = wf_notif_v2_include_to_str(p->include);
        if (s) {
            cJSON_AddItemToObject(obj, "include", cJSON_CreateString(s));
        }
    }
    if (p->has_list) {
        cJSON_AddItemToObject(obj, "list", cJSON_CreateBool(p->list != 0));
    }
    if (p->has_push) {
        cJSON_AddItemToObject(obj, "push", cJSON_CreateBool(p->push != 0));
    }
    cJSON_AddItemToObject(root, key, obj);
}

/* Append a sub-object for one plain preference slot (list/push). */
static void wf_notif_v2_add_pref(cJSON *root, const char *key,
                                 const wf_notif_v2_pref *p) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return;
    }
    if (p->has_list) {
        cJSON_AddItemToObject(obj, "list", cJSON_CreateBool(p->list != 0));
    }
    if (p->has_push) {
        cJSON_AddItemToObject(obj, "push", cJSON_CreateBool(p->push != 0));
    }
    cJSON_AddItemToObject(root, key, obj);
}

/* Append a sub-object for the deprecated chatPreference slot (include/push). */
static void wf_notif_v2_add_chat(cJSON *root, const char *key,
                                 const wf_notif_v2_chat_pref *p) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return;
    }
    if (p->has_include) {
        const char *s = wf_notif_v2_include_to_str(p->include);
        if (s) {
            cJSON_AddItemToObject(obj, "include", cJSON_CreateString(s));
        }
    }
    if (p->has_push) {
        cJSON_AddItemToObject(obj, "push", cJSON_CreateBool(p->push != 0));
    }
    cJSON_AddItemToObject(root, key, obj);
}

wf_status wf_notification_v2_preferences_build(
    const wf_notification_v2_preferences *prefs, char **out_json) {
    if (!prefs || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    /* Validate every set `include` resolves to a legal wire string. */
    if (prefs->has_chat && prefs->chat.has_include) {
        if (!wf_notif_v2_include_to_str(prefs->chat.include)) {
            return WF_ERR_INVALID_ARG;
        }
    }
    if (prefs->has_follow && prefs->follow.has_include &&
        !wf_notif_v2_include_to_str(prefs->follow.include)) {
        return WF_ERR_INVALID_ARG;
    }
    if (prefs->has_like && prefs->like.has_include &&
        !wf_notif_v2_include_to_str(prefs->like.include)) {
        return WF_ERR_INVALID_ARG;
    }
    if (prefs->has_like_via_repost && prefs->like_via_repost.has_include &&
        !wf_notif_v2_include_to_str(prefs->like_via_repost.include)) {
        return WF_ERR_INVALID_ARG;
    }
    if (prefs->has_mention && prefs->mention.has_include &&
        !wf_notif_v2_include_to_str(prefs->mention.include)) {
        return WF_ERR_INVALID_ARG;
    }
    if (prefs->has_quote && prefs->quote.has_include &&
        !wf_notif_v2_include_to_str(prefs->quote.include)) {
        return WF_ERR_INVALID_ARG;
    }
    if (prefs->has_reply && prefs->reply.has_include &&
        !wf_notif_v2_include_to_str(prefs->reply.include)) {
        return WF_ERR_INVALID_ARG;
    }
    if (prefs->has_repost && prefs->repost.has_include &&
        !wf_notif_v2_include_to_str(prefs->repost.include)) {
        return WF_ERR_INVALID_ARG;
    }
    if (prefs->has_repost_via_repost && prefs->repost_via_repost.has_include &&
        !wf_notif_v2_include_to_str(prefs->repost_via_repost.include)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (prefs->has_chat) {
        wf_notif_v2_add_chat(root, "chat", &prefs->chat);
    }
    if (prefs->has_follow) {
        wf_notif_v2_add_filterable(root, "follow", &prefs->follow);
    }
    if (prefs->has_like) {
        wf_notif_v2_add_filterable(root, "like", &prefs->like);
    }
    if (prefs->has_like_via_repost) {
        wf_notif_v2_add_filterable(root, "likeViaRepost",
                                   &prefs->like_via_repost);
    }
    if (prefs->has_mention) {
        wf_notif_v2_add_filterable(root, "mention", &prefs->mention);
    }
    if (prefs->has_quote) {
        wf_notif_v2_add_filterable(root, "quote", &prefs->quote);
    }
    if (prefs->has_reply) {
        wf_notif_v2_add_filterable(root, "reply", &prefs->reply);
    }
    if (prefs->has_repost) {
        wf_notif_v2_add_filterable(root, "repost", &prefs->repost);
    }
    if (prefs->has_repost_via_repost) {
        wf_notif_v2_add_filterable(root, "repostViaRepost",
                                   &prefs->repost_via_repost);
    }
    if (prefs->has_starterpack_joined) {
        wf_notif_v2_add_pref(root, "starterpackJoined",
                             &prefs->starterpack_joined);
    }
    if (prefs->has_subscribed_post) {
        wf_notif_v2_add_pref(root, "subscribedPost", &prefs->subscribed_post);
    }
    if (prefs->has_unverified) {
        wf_notif_v2_add_pref(root, "unverified", &prefs->unverified);
    }
    if (prefs->has_verified) {
        wf_notif_v2_add_pref(root, "verified", &prefs->verified);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }
    *out_json = json;
    return WF_OK;
}

/* Read one filterablePreference sub-object into `p` (all fields required). */
static void wf_notif_v2_read_filterable(cJSON *o, wf_notif_v2_filterable_pref *p) {
    cJSON *include = cJSON_GetObjectItemCaseSensitive(o, "include");
    cJSON *list = cJSON_GetObjectItemCaseSensitive(o, "list");
    cJSON *push = cJSON_GetObjectItemCaseSensitive(o, "push");
    if (cJSON_IsString(include) && include->valuestring) {
        p->has_include = 1;
        if (strcmp(include->valuestring, "follows") == 0) {
            p->include = WF_NOTIF_V2_INCLUDE_FOLLOWS;
        } else if (strcmp(include->valuestring, "accepted") == 0) {
            p->include = WF_NOTIF_V2_INCLUDE_ACCEPTED;
        } else {
            p->include = WF_NOTIF_V2_INCLUDE_ALL;
        }
    }
    if (cJSON_IsBool(list)) {
        p->has_list = 1;
        p->list = cJSON_IsTrue(list);
    }
    if (cJSON_IsBool(push)) {
        p->has_push = 1;
        p->push = cJSON_IsTrue(push);
    }
}

/* Read one plain preference sub-object into `p` (all fields required). */
static void wf_notif_v2_read_pref(cJSON *o, wf_notif_v2_pref *p) {
    cJSON *list = cJSON_GetObjectItemCaseSensitive(o, "list");
    cJSON *push = cJSON_GetObjectItemCaseSensitive(o, "push");
    if (cJSON_IsBool(list)) {
        p->has_list = 1;
        p->list = cJSON_IsTrue(list);
    }
    if (cJSON_IsBool(push)) {
        p->has_push = 1;
        p->push = cJSON_IsTrue(push);
    }
}

/* Read the deprecated chatPreference sub-object into `p` (all fields required). */
static void wf_notif_v2_read_chat(cJSON *o, wf_notif_v2_chat_pref *p) {
    cJSON *include = cJSON_GetObjectItemCaseSensitive(o, "include");
    cJSON *push = cJSON_GetObjectItemCaseSensitive(o, "push");
    if (cJSON_IsString(include) && include->valuestring) {
        p->has_include = 1;
        if (strcmp(include->valuestring, "follows") == 0) {
            p->include = WF_NOTIF_V2_INCLUDE_FOLLOWS;
        } else if (strcmp(include->valuestring, "accepted") == 0) {
            p->include = WF_NOTIF_V2_INCLUDE_ACCEPTED;
        } else {
            p->include = WF_NOTIF_V2_INCLUDE_ALL;
        }
    }
    if (cJSON_IsBool(push)) {
        p->has_push = 1;
        p->push = cJSON_IsTrue(push);
    }
}

wf_status wf_notification_v2_preferences_parse(const char *json, size_t len,
                                               wf_notification_v2_preferences *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    /* Accept either a bare `#preferences` object or a wrapping output body
     * `{ preferences: {...} }`. */
    cJSON *prefs = cJSON_GetObjectItemCaseSensitive(root, "preferences");
    if (cJSON_IsObject(prefs)) {
        prefs = cJSON_DetachItemFromObject(root, "preferences");
        cJSON_Delete(root);
        root = prefs;
    }

    wf_notif_v2_chat_pref chat = {0};
    wf_notif_v2_filterable_pref follow = {0}, like = {0}, like_via_repost = {0},
                                mention = {0}, quote = {0}, reply = {0},
                                repost = {0}, repost_via_repost = {0};
    wf_notif_v2_pref starterpack_joined = {0}, subscribed_post = {0},
                     unverified = {0}, verified = {0};

    cJSON *o;
#define READ_SLOT(field, name, reader)                                       \
    do {                                                                     \
        o = cJSON_GetObjectItemCaseSensitive(root, name);                    \
        if (cJSON_IsObject(o)) {                                             \
            reader(o, &field);                                               \
        }                                                                    \
    } while (0)

    READ_SLOT(chat, "chat", wf_notif_v2_read_chat);
    READ_SLOT(follow, "follow", wf_notif_v2_read_filterable);
    READ_SLOT(like, "like", wf_notif_v2_read_filterable);
    READ_SLOT(like_via_repost, "likeViaRepost", wf_notif_v2_read_filterable);
    READ_SLOT(mention, "mention", wf_notif_v2_read_filterable);
    READ_SLOT(quote, "quote", wf_notif_v2_read_filterable);
    READ_SLOT(reply, "reply", wf_notif_v2_read_filterable);
    READ_SLOT(repost, "repost", wf_notif_v2_read_filterable);
    READ_SLOT(repost_via_repost, "repostViaRepost", wf_notif_v2_read_filterable);
    READ_SLOT(starterpack_joined, "starterpackJoined", wf_notif_v2_read_pref);
    READ_SLOT(subscribed_post, "subscribedPost", wf_notif_v2_read_pref);
    READ_SLOT(unverified, "unverified", wf_notif_v2_read_pref);
    READ_SLOT(verified, "verified", wf_notif_v2_read_pref);
#undef READ_SLOT

    out->has_chat = 1;
    out->chat = chat;
    out->has_follow = 1;
    out->follow = follow;
    out->has_like = 1;
    out->like = like;
    out->has_like_via_repost = 1;
    out->like_via_repost = like_via_repost;
    out->has_mention = 1;
    out->mention = mention;
    out->has_quote = 1;
    out->quote = quote;
    out->has_reply = 1;
    out->reply = reply;
    out->has_repost = 1;
    out->repost = repost;
    out->has_repost_via_repost = 1;
    out->repost_via_repost = repost_via_repost;
    out->has_starterpack_joined = 1;
    out->starterpack_joined = starterpack_joined;
    out->has_subscribed_post = 1;
    out->subscribed_post = subscribed_post;
    out->has_unverified = 1;
    out->unverified = unverified;
    out->has_verified = 1;
    out->verified = verified;

    cJSON_Delete(root);
    return WF_OK;
}

void wf_notification_v2_preferences_free(wf_notification_v2_preferences *p) {
    if (!p) {
        return;
    }
    memset(p, 0, sizeof(*p));
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_put_notification_preferences_v2(
    wf_agent *agent, const char *preferences_json,
    const char *const *deleted_prefs, size_t deleted_count,
    wf_notif_v2_preferences *out) {
    if (!agent || !agent->client || !preferences_json || !preferences_json[0]) {
        return WF_ERR_INVALID_ARG;
    }
    /* The current lexicon (app.bsky.notification.putPreferencesV2) has no
     * deletedPrefs field; reject non-zero counts rather than silently drop. */
    (void)deleted_prefs;
    if (deleted_count != 0) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(preferences_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_lex_app_bsky_notification_put_preferences_v2_main_input in = {0};
    wf_status status = wf_notif_v2_build_prefs_v2_input(root, &in);
    if (status != WF_OK) {
        cJSON_Delete(root);
        return status;
    }

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    status = wf_lex_app_bsky_notification_put_preferences_v2_main_call(
        agent->client, &in, &res);
    wf_notif_v2_prefs_v2_input_free(&in);
    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    if (out) {
        wf_notif_v2_preferences prefs = {0};
        status = wf_notif_v2_parse_preferences(res.body, res.body_len, &prefs);
        if (status == WF_OK) {
            *out = prefs;
        } else {
            wf_notif_v2_preferences_free(&prefs);
        }
    }

    wf_response_free(&res);
    return status;
}

/* Build the structured generated input from the owned typed prefs struct. The
 * generated input borrows `include` string pointers from our static table, so
 * no sub-struct string fields need freeing (only the sub-structs themselves). */
static wf_status wf_notif_v2_prefs_to_lex_input(
    const wf_notification_v2_preferences *p,
    wf_lex_app_bsky_notification_put_preferences_v2_main_input *in) {
    wf_status status = WF_OK;

    if (p->has_chat) {
        if (p->chat.has_include &&
            !wf_notif_v2_include_to_str(p->chat.include)) {
            return WF_ERR_INVALID_ARG;
        }
        wf_lex_app_bsky_notification_defs_chat_preference *c = calloc(1, sizeof(*c));
        if (!c) {
            return WF_ERR_ALLOC;
        }
        if (p->chat.has_include) {
            c->include = wf_notif_v2_include_to_str(p->chat.include);
        }
        if (p->chat.has_push) {
            c->push = p->chat.push != 0;
        }
        in->has_chat = true;
        in->chat = c;
    }
#define FILL_FILTERABLE(slot, lexfield)                                      \
    do {                                                                     \
        if (p->has_##slot) {                                                 \
            if (p->slot.has_include &&                                       \
                !wf_notif_v2_include_to_str(p->slot.include)) {              \
                return WF_ERR_INVALID_ARG;                                   \
            }                                                                \
            wf_lex_app_bsky_notification_defs_filterable_preference *f =     \
                calloc(1, sizeof(*f));                                       \
            if (!f) {                                                        \
                return WF_ERR_ALLOC;                                         \
            }                                                                \
            if (p->slot.has_include) {                                       \
                f->include = wf_notif_v2_include_to_str(p->slot.include);    \
            }                                                                \
            if (p->slot.has_list) {                                          \
                f->list = p->slot.list != 0;                                 \
            }                                                                \
            if (p->slot.has_push) {                                          \
                f->push = p->slot.push != 0;                                 \
            }                                                                \
            in->has_##slot = true;                                           \
            in->lexfield = f;                                                \
        }                                                                    \
    } while (0)
#define FILL_PREF(slot, lexfield)                                            \
    do {                                                                     \
        if (p->has_##slot) {                                                 \
            wf_lex_app_bsky_notification_defs_preference *q =                \
                calloc(1, sizeof(*q));                                       \
            if (!q) {                                                        \
                return WF_ERR_ALLOC;                                         \
            }                                                                \
            if (p->slot.has_list) {                                          \
                q->list = p->slot.list != 0;                                 \
            }                                                                \
            if (p->slot.has_push) {                                          \
                q->push = p->slot.push != 0;                                 \
            }                                                                \
            in->has_##slot = true;                                           \
            in->lexfield = q;                                                \
        }                                                                    \
    } while (0)

    FILL_FILTERABLE(follow, follow);
    FILL_FILTERABLE(like, like);
    FILL_FILTERABLE(like_via_repost, like_via_repost);
    FILL_FILTERABLE(mention, mention);
    FILL_FILTERABLE(quote, quote);
    FILL_FILTERABLE(reply, reply);
    FILL_FILTERABLE(repost, repost);
    FILL_FILTERABLE(repost_via_repost, repost_via_repost);
    FILL_PREF(starterpack_joined, starterpack_joined);
    FILL_PREF(subscribed_post, subscribed_post);
    FILL_PREF(unverified, unverified);
    FILL_PREF(verified, verified);
#undef FILL_FILTERABLE
#undef FILL_PREF
    return status;
}

static void wf_notif_v2_lex_input_free(
    wf_lex_app_bsky_notification_put_preferences_v2_main_input *in) {
    if (!in) {
        return;
    }
    free((void *)in->chat);
    free((void *)in->follow);
    free((void *)in->like);
    free((void *)in->like_via_repost);
    free((void *)in->mention);
    free((void *)in->quote);
    free((void *)in->reply);
    free((void *)in->repost);
    free((void *)in->repost_via_repost);
    free((void *)in->starterpack_joined);
    free((void *)in->subscribed_post);
    free((void *)in->unverified);
    free((void *)in->verified);
    memset(in, 0, sizeof(*in));
}

wf_status wf_agent_put_notification_preferences_v2_typed(
    wf_agent *agent, const wf_notification_v2_preferences *prefs,
    wf_notification_v2_preferences *out) {
    if (!agent || !agent->client || !prefs) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_notification_put_preferences_v2_main_input in = {0};
    wf_status status = wf_notif_v2_prefs_to_lex_input(prefs, &in);
    if (status != WF_OK) {
        wf_notif_v2_lex_input_free(&in);
        return status;
    }

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    status = wf_lex_app_bsky_notification_put_preferences_v2_main_call(
        agent->client, &in, &res);
    wf_notif_v2_lex_input_free(&in);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    if (out) {
        wf_notification_v2_preferences parsed = {0};
        status = wf_notification_v2_preferences_parse(res.body, res.body_len,
                                                      &parsed);
        if (status == WF_OK) {
            *out = parsed;
        } else {
            wf_notification_v2_preferences_free(&parsed);
        }
    }

    wf_response_free(&res);
    return status;
}

wf_status wf_agent_list_activity_subscriptions(
    wf_agent *agent, int limit, const char *cursor,
    wf_notif_v2_subscription_view_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_notif_v2_subscription_view_list list = {0};
    wf_lex_app_bsky_notification_list_activity_subscriptions_main_params params =
        {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_notification_list_activity_subscriptions_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_notif_v2_parse_list_activity_subscriptions(res.body,
                                                          res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_put_activity_subscription(
    wf_agent *agent, const char *subscription_json,
    wf_notif_v2_activity_subscription_result *out) {
    if (!agent || !agent->client || !subscription_json ||
        !subscription_json[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(subscription_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *subject = cJSON_GetObjectItemCaseSensitive(root, "subject");
    cJSON *as = cJSON_GetObjectItemCaseSensitive(root, "activitySubscription");
    if (!cJSON_IsString(subject) || !cJSON_IsObject(as)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_lex_app_bsky_notification_defs_activity_subscription as_struct = {0};
    cJSON *post = cJSON_GetObjectItemCaseSensitive(as, "post");
    cJSON *reply = cJSON_GetObjectItemCaseSensitive(as, "reply");
    if (cJSON_IsBool(post)) {
        as_struct.post = cJSON_IsTrue(post);
    }
    if (cJSON_IsBool(reply)) {
        as_struct.reply = cJSON_IsTrue(reply);
    }

    wf_lex_app_bsky_notification_put_activity_subscription_main_input in = {0};
    in.subject = subject->valuestring;
    in.activity_subscription = &as_struct;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_notification_put_activity_subscription_main_call(
            agent->client, &in, &res);
    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_notif_v2_parse_put_activity_subscription(res.body, res.body_len,
                                                        out);
    wf_response_free(&res);
    return status;
}
