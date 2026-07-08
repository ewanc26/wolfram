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
