/*
 * unspecced_trends_typed.c — owned typed parsers + agent wrappers for the
 * `app.bsky.unspecced` TRENDS / SUGGESTED USERS / THREAD V2 endpoints.
 *
 * See include/wolfram/unspecced_trends_typed.h for the public API, the
 * authoritative wire format, and ownership rules. Mirrors labeler_typed.c /
 * actor_typed.c: static strdup/set_string/reset helpers, owned strings, detached
 * `extra` cJSON subtrees where shapes are open/unbounded, and full cleanup on
 * the first error.
 */

#include "wolfram/unspecced_trends_typed.h"

#include "wolfram/atproto_lex.h"
#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_ut_strdup(const char *s) {
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

static wf_status wf_ut_set_string(char **dst, const char *src) {
    char *copy = wf_ut_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- trend view ---- */

static void wf_ut_trend_view_reset(wf_unspecced_trend_view *t) {
    if (!t) {
        return;
    }
    free(t->topic);
    free(t->display_name);
    free(t->link);
    free(t->started_at);
    free(t->status);
    free(t->category);
    wf_agent_actor_list_free(&t->actors);
    if (t->extra) {
        cJSON_Delete(t->extra);
    }
    memset(t, 0, sizeof(*t));
}

static wf_status wf_ut_parse_trend_view(cJSON *obj, wf_unspecced_trend_view *t) {
    wf_status status = WF_OK;
    cJSON *topic = cJSON_GetObjectItemCaseSensitive(obj, "topic");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *link = cJSON_GetObjectItemCaseSensitive(obj, "link");
    cJSON *started = cJSON_GetObjectItemCaseSensitive(obj, "startedAt");
    cJSON *count = cJSON_GetObjectItemCaseSensitive(obj, "postCount");
    cJSON *st_json = cJSON_GetObjectItemCaseSensitive(obj, "status");
    cJSON *category = cJSON_GetObjectItemCaseSensitive(obj, "category");
    cJSON *actors = cJSON_GetObjectItemCaseSensitive(obj, "actors");

    if (cJSON_IsString(topic) && topic->valuestring) {
        status = wf_ut_set_string(&t->topic, topic->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_ut_set_string(&t->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(link) && link->valuestring) {
        status = wf_ut_set_string(&t->link, link->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(started) && started->valuestring) {
        status = wf_ut_set_string(&t->started_at, started->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(count)) {
        t->has_post_count = 1;
        t->post_count = (int64_t)count->valuedouble;
    }
    if (status == WF_OK && cJSON_IsString(st_json) && st_json->valuestring) {
        status = wf_ut_set_string(&t->status, st_json->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(category) && category->valuestring) {
        status = wf_ut_set_string(&t->category, category->valuestring);
    }
    int actors_detached = 0;
    if (status == WF_OK && cJSON_IsArray(actors) &&
        cJSON_GetArraySize(actors) > 0) {
        /* Wrap the actors array in an {"actors":[...]} object so we can reuse
         * the shared wf_agent_parse_actors ownership logic, then discard it. */
        cJSON *wrapper = cJSON_CreateObject();
        if (!wrapper) {
            status = WF_ERR_ALLOC;
        } else {
            cJSON *arr = cJSON_DetachItemFromObject(obj, "actors");
            cJSON_AddItemToObject(wrapper, "actors", arr);
            char *body = cJSON_PrintUnformatted(wrapper);
            if (!body) {
                status = WF_ERR_ALLOC;
            } else {
                status = wf_agent_parse_actors(body, strlen(body), &t->actors);
                free(body);
            }
            cJSON_Delete(wrapper);
            actors_detached = 1;
        }
    }
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "topic");
        cJSON_DetachItemFromObject(obj, "displayName");
        cJSON_DetachItemFromObject(obj, "link");
        cJSON_DetachItemFromObject(obj, "startedAt");
        cJSON_DetachItemFromObject(obj, "postCount");
        cJSON_DetachItemFromObject(obj, "status");
        cJSON_DetachItemFromObject(obj, "category");
        if (!actors_detached) {
            cJSON_DetachItemFromObject(obj, "actors");
        }
        t->extra = obj;
    } else {
        wf_ut_trend_view_reset(t);
    }
    return status;
}

wf_status wf_unspecced_parse_trends(const char *json, size_t json_len,
                                    wf_unspecced_trend_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "trends");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_unspecced_trend_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_unspecced_trend_view *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
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
        status = wf_ut_parse_trend_view(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_ut_trend_view_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->trends = items;
        out->trend_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_ut_trend_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_unspecced_trend_list_free(wf_unspecced_trend_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->trend_count; ++i) {
        wf_ut_trend_view_reset(&list->trends[i]);
    }
    free(list->trends);
    memset(list, 0, sizeof(*list));
}

wf_status wf_unspecced_parse_suggested_users(const char *json, size_t json_len,
                                             wf_agent_actor_list *out) {
    return wf_agent_parse_actors(json, json_len, out);
}

wf_status wf_unspecced_parse_suggested_feeds(const char *json, size_t json_len,
                                             wf_agent_generator_view_list *out) {
    return wf_agent_parse_generators(json, json_len, out);
}

/* ---- thread v2 ---- */

static void wf_ut_thread_item_reset(wf_unspecced_thread_item_v2 *it) {
    if (!it) {
        return;
    }
    free(it->uri);
    if (it->value) {
        cJSON_Delete(it->value);
    }
    memset(it, 0, sizeof(*it));
}

static wf_status wf_ut_parse_thread_item(cJSON *obj,
                                         wf_unspecced_thread_item_v2 *it) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *depth = cJSON_GetObjectItemCaseSensitive(obj, "depth");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, "value");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_ut_set_string(&it->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(depth)) {
        it->has_depth = 1;
        it->depth = (int64_t)depth->valuedouble;
    }
    if (status == WF_OK) {
        if (value != NULL) {
            cJSON_DetachItemFromObject(obj, "uri");
            cJSON_DetachItemFromObject(obj, "depth");
            cJSON_DetachItemFromObject(obj, "value");
            it->value = value;
        } else {
            cJSON_DetachItemFromObject(obj, "uri");
            cJSON_DetachItemFromObject(obj, "depth");
            it->value = NULL;
        }
    } else {
        wf_ut_thread_item_reset(it);
    }
    return status;
}

wf_status wf_unspecced_parse_thread_v2(const char *json, size_t json_len,
                                       wf_unspecced_thread_v2 *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "thread");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *threadgate = cJSON_GetObjectItemCaseSensitive(root, "threadgate");
    if (threadgate != NULL) {
        cJSON_DetachItemFromObject(root, "threadgate");
        out->threadgate = threadgate;
    }
    cJSON *other = cJSON_GetObjectItemCaseSensitive(root, "hasOtherReplies");
    if (cJSON_IsBool(other)) {
        out->has_other_replies = cJSON_IsTrue(other);
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_unspecced_thread_item_v2 *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_unspecced_thread_item_v2 *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
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
        status = wf_ut_parse_thread_item(obj, &items[i]);
        /* The thread item container is no longer needed: items[i] owns its
         * extracted uri + value. Detach and free the leftover wrapper. */
        cJSON *container = cJSON_DetachItemViaPointer(arr, obj);
        cJSON_Delete(container);
        if (status != WF_OK) {
            wf_ut_thread_item_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->items = items;
        out->item_count = count;
    } else {
        if (out->threadgate) {
            cJSON_Delete(out->threadgate);
            out->threadgate = NULL;
        }
        for (size_t i = 0; i < count; ++i) {
            wf_ut_thread_item_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_unspecced_thread_v2_free(wf_unspecced_thread_v2 *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->item_count; ++i) {
        wf_ut_thread_item_reset(&list->items[i]);
    }
    free(list->items);
    if (list->threadgate) {
        cJSON_Delete(list->threadgate);
    }
    memset(list, 0, sizeof(*list));
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_get_trends_typed(wf_agent *agent, int limit,
                                    wf_unspecced_trend_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 25) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_unspecced_trend_list list = {0};
    wf_lex_app_bsky_unspecced_get_trends_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_unspecced_get_trends_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_trends(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_suggested_users_typed(wf_agent *agent,
                                             const char *category_or_null,
                                             int limit, wf_agent_actor_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 50) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_actor_list list = {0};
    wf_lex_app_bsky_unspecced_get_suggested_users_main_params params = {0};
    if (category_or_null && category_or_null[0]) {
        params.has_category = true;
        params.category = category_or_null;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_unspecced_get_suggested_users_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_suggested_users(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_suggested_users_for_discover_typed(
    wf_agent *agent, int limit, wf_agent_actor_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 50) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_actor_list list = {0};
    wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_params params =
        {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_suggested_users(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_suggested_users_for_explore_typed(
    wf_agent *agent, const char *category_or_null, int limit,
    wf_agent_actor_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 50) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_actor_list list = {0};
    wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_params params =
        {0};
    if (category_or_null && category_or_null[0]) {
        params.has_category = true;
        params.category = category_or_null;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_suggested_users(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_suggested_users_for_see_more_typed(
    wf_agent *agent, const char *category_or_null, int limit,
    wf_agent_actor_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 50) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_actor_list list = {0};
    wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_params params =
        {0};
    if (category_or_null && category_or_null[0]) {
        params.has_category = true;
        params.category = category_or_null;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_suggested_users(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_suggested_onboarding_users_typed(
    wf_agent *agent, const char *category_or_null, int limit,
    wf_agent_actor_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 50) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_actor_list list = {0};
    wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_params params =
        {0};
    if (category_or_null && category_or_null[0]) {
        params.has_category = true;
        params.category = category_or_null;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_suggested_users(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_suggested_feeds_typed(wf_agent *agent, int limit,
                                             wf_agent_generator_view_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 25) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_generator_view_list list = {0};
    wf_lex_app_bsky_unspecced_get_suggested_feeds_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_unspecced_get_suggested_feeds_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_suggested_feeds(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_post_thread_v2_typed(wf_agent *agent, const char *anchor,
                                            int above, int below,
                                            int branching_factor,
                                            const char *sort_or_null,
                                            wf_unspecced_thread_v2 *out) {
    if (!agent || !anchor || !anchor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_unspecced_thread_v2 list = {0};
    wf_lex_app_bsky_unspecced_get_post_thread_v2_main_params params = {0};
    params.anchor = anchor;
    params.has_above = true;
    params.above = above != 0;
    if (below >= 0) {
        params.has_below = true;
        params.below = below;
    }
    if (branching_factor >= 0) {
        params.has_branching_factor = true;
        params.branching_factor = branching_factor;
    }
    if (sort_or_null && sort_or_null[0]) {
        params.has_sort = true;
        params.sort = sort_or_null;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_unspecced_get_post_thread_v2_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_thread_v2(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_post_thread_other_v2_typed(wf_agent *agent,
                                                  const char *anchor,
                                                  wf_unspecced_thread_v2 *out) {
    if (!agent || !anchor || !anchor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_unspecced_thread_v2 list = {0};
    wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_params params = {0};
    params.anchor = anchor;
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_unspecced_parse_thread_v2(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}
