/*
 * graph_typed.c — typed parser for actor-list responses.
 *
 * See include/wolfram/graph_typed.h for the public API and ownership rules.
 * Follows the same conventions as notification.c / feed_typed.c: static
 * strdup/set_string/reset helpers, owned strings, full cleanup on first error.
 */

#include "wolfram/graph_typed.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_graph_strdup(const char *s) {
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

static wf_status wf_graph_set_string(char **dst, const char *src) {
    char *copy = wf_graph_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_graph_profile_view_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static void wf_graph_list_reset(wf_agent_actor_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->actor_count; ++i) {
        wf_graph_profile_view_reset(&list->actors[i]);
    }
    free(list->actors);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

wf_status wf_agent_parse_actors(const char *json, size_t json_len,
                                wf_agent_actor_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *actors = cJSON_GetObjectItemCaseSensitive(root, "actors");
    if (!cJSON_IsArray(actors)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(actors);
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
        cJSON *obj = cJSON_GetArrayItem(actors, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }

        cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
        cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
        cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");

        if (cJSON_IsString(did) && did->valuestring) {
            status = wf_graph_set_string(&p->did, did->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
            status = wf_graph_set_string(&p->handle, handle->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
            status = wf_graph_set_string(&p->display_name, name->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
            status = wf_graph_set_string(&p->avatar, avatar->valuestring);
        }

        if (status != WF_OK) {
            wf_graph_profile_view_reset(p);
        }
    }

    if (status == WF_OK) {
        out->actors = items;
        out->actor_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_graph_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_profile_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_actor_list_free(wf_agent_actor_list *list) {
    wf_graph_list_reset(list);
}

/* Typed high-level wrappers — call the raw agent endpoint, then parse. */

wf_status wf_agent_get_follows_typed(wf_agent *agent, const char *actor,
                                     int limit, const char *cursor,
                                     wf_agent_actor_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_follows(agent, actor, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_actors(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_followers_typed(wf_agent *agent, const char *actor,
                                       int limit, const char *cursor,
                                       wf_agent_actor_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_followers(agent, actor, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_actors(res.body, res.body_len, out);
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

    status = wf_agent_parse_actors(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}
