/*
 * actor_typed.c — typed parsers + agent wrappers for actor/profile/search/
 * suggestion responses. See include/wolfram/actor_typed.h for the public API
 * and ownership rules. Follows the conventions of notification.c / feed_typed.c
 * / graph_typed.c: static strdup/set_string/reset helpers, owned strings, full
 * cleanup on the first error.
 */

#include "wolfram/actor_typed.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_actor_strdup(const char *s) {
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

static wf_status wf_actor_set_string(char **dst, const char *src) {
    char *copy = wf_actor_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_actor_profile_view_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_profile_view_basic_reset(wf_agent_profile_view_basic *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    if (p->viewer) {
        cJSON_Delete(p->viewer);
    }
    memset(p, 0, sizeof(*p));
}

/* Parse the four core profileView fields (did/handle/displayName/avatar). */
static wf_status wf_actor_parse_profile_view(wf_agent_profile_view *p,
                                             cJSON *obj) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_actor_set_string(&p->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_actor_set_string(&p->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_actor_set_string(&p->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_actor_set_string(&p->avatar, avatar->valuestring);
    }
    return status;
}

/* Parse a profileViewBasic, taking ownership of an arbitrary viewer subtree. */
static wf_status wf_actor_parse_profile_view_basic(wf_agent_profile_view_basic *p,
                                                   cJSON *obj) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_actor_set_string(&p->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_actor_set_string(&p->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_actor_set_string(&p->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_actor_set_string(&p->avatar, avatar->valuestring);
    }
    if (status == WF_OK) {
        cJSON *viewer = cJSON_DetachItemFromObject(obj, "viewer");
        if (viewer) {
            if (cJSON_IsObject(viewer)) {
                p->viewer = viewer;
            } else {
                cJSON_Delete(viewer);
            }
        }
    }
    return status;
}

/* Parse a profileView array held under `key` into an owned actor list. */
static wf_status wf_actor_parse_actor_list(const char *json, size_t json_len,
                                           const char *key,
                                           wf_agent_actor_list *out) {
    if (!json || !out || !key) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_profile_view *items = NULL;
    if (count > 0) {
        items = (wf_agent_profile_view *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_profile_view *p = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_actor_parse_profile_view(p, obj);
        if (status != WF_OK) {
            wf_actor_profile_view_reset(p);
        }
    }

    if (status == WF_OK) {
        out->actors = items;
        out->actor_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_actor_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_actor_profile_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_parse_actors(const char *json, size_t json_len,
                                wf_agent_actor_list *out) {
    return wf_actor_parse_actor_list(json, json_len, "actors", out);
}

wf_status wf_agent_parse_profile_views(const char *json, size_t json_len,
                                       const char *key, wf_agent_actor_list *out) {
    return wf_actor_parse_actor_list(json, json_len, key, out);
}

wf_status wf_agent_parse_actor_search(const char *json, size_t json_len,
                                      wf_agent_actor_list *out) {
    return wf_actor_parse_actor_list(json, json_len, "actors", out);
}

wf_status wf_agent_parse_reposted_by(const char *json, size_t json_len,
                                     wf_agent_actor_list *out) {
    return wf_actor_parse_actor_list(json, json_len, "repostedBy", out);
}

wf_status wf_agent_parse_profiles(const char *json, size_t json_len,
                                  wf_agent_profile_view_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_profile_view *items = NULL;
    if (count > 0) {
        items = (wf_agent_profile_view *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_profile_view *p = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_actor_parse_profile_view(p, obj);
        if (status != WF_OK) {
            wf_actor_profile_view_reset(p);
        }
    }

    if (status == WF_OK) {
        out->profiles = items;
        out->profile_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_actor_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_actor_profile_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_parse_actor_typeahead(const char *json, size_t json_len,
                                         wf_agent_profile_view_basic_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "actors");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_profile_view_basic *items = NULL;
    if (count > 0) {
        items = (wf_agent_profile_view_basic *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_profile_view_basic *p = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_actor_parse_profile_view_basic(p, obj);
        if (status != WF_OK) {
            wf_actor_profile_view_basic_reset(p);
        }
    }

    if (status == WF_OK) {
        out->actors = items;
        out->actor_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_actor_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_actor_profile_view_basic_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_parse_actor_likes(const char *json, size_t json_len,
                                     wf_agent_actor_like_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "likes");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_actor_like_item *items = NULL;
    if (count > 0) {
        items = (wf_agent_actor_like_item *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_actor_like_item *l = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }

        cJSON *actor = cJSON_GetObjectItemCaseSensitive(obj, "actor");
        cJSON *created = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
        cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

        if (cJSON_IsObject(actor)) {
            status = wf_actor_parse_profile_view(&l->actor, actor);
        } else {
            status = WF_ERR_PARSE;
        }
        if (status == WF_OK && cJSON_IsString(created) && created->valuestring) {
            status = wf_actor_set_string(&l->created_at, created->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
            status = wf_actor_set_string(&l->indexed_at, indexed->valuestring);
        }

        if (status != WF_OK) {
            wf_actor_profile_view_reset(&l->actor);
            free(l->created_at);
            free(l->indexed_at);
            memset(l, 0, sizeof(*l));
        }
    }

    if (status == WF_OK) {
        out->likes = items;
        out->like_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_actor_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_agent_actor_like_item *l = &items[i];
            wf_actor_profile_view_reset(&l->actor);
            free(l->created_at);
            free(l->indexed_at);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_actor_list_free(wf_agent_actor_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->actor_count; ++i) {
        wf_actor_profile_view_reset(&list->actors[i]);
    }
    free(list->actors);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_agent_profile_view_list_free(wf_agent_profile_view_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->profile_count; ++i) {
        wf_actor_profile_view_reset(&list->profiles[i]);
    }
    free(list->profiles);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_agent_profile_view_basic_list_free(wf_agent_profile_view_basic_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->actor_count; ++i) {
        wf_actor_profile_view_basic_reset(&list->actors[i]);
    }
    free(list->actors);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_agent_actor_like_list_free(wf_agent_actor_like_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->like_count; ++i) {
        wf_agent_actor_like_item *l = &list->likes[i];
        wf_actor_profile_view_reset(&l->actor);
        free(l->created_at);
        free(l->indexed_at);
    }
    free(list->likes);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* Typed high-level wrappers — call the raw agent method (which performs auth
 * internally), then parse the body. */

wf_status wf_agent_get_profiles_typed(wf_agent *agent,
                                      const char *const *actors,
                                      size_t actors_count, int limit,
                                      const char *cursor,
                                      wf_agent_actor_list *out) {
    if (!agent || !actors || actors_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_profiles(agent, actors, actors_count,
                                             limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_profile_views(res.body, res.body_len, "profiles",
                                           out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_search_actors_typed(wf_agent *agent, const char *query,
                                       int limit, const char *cursor,
                                       wf_agent_actor_list *out) {
    if (!agent || !query || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_search_actors(agent, query, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_actor_search(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_search_actors_typeahead_typed(wf_agent *agent,
                                                 const char *query, int limit,
                                                 wf_agent_profile_view_basic_list *out) {
    if (!agent || !query || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_search_actors_typeahead(agent, query, limit, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_actor_typeahead(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_likes_typed(wf_agent *agent, const char *uri,
                                   int limit, const char *cursor,
                                   wf_agent_actor_like_list *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_likes(agent, uri, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_actor_likes(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_reposted_by_typed(wf_agent *agent, const char *uri,
                                         int limit, const char *cursor,
                                         wf_agent_actor_list *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_reposted_by(agent, uri, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_reposted_by(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}
