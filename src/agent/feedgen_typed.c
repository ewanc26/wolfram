/*
 * feedgen_typed.c — typed parser for feed-generator endpoints.
 *
 * See include/wolfram/feedgen_typed.h for the public API and ownership rules.
 * Follows the same conventions as notification.c / feed_typed.c / graph_typed.c:
 * static strdup/set_string/reset helpers, owned string copies, full cleanup on
 * the first error.
 */

#include "wolfram/feedgen_typed.h"

#include "wolfram/agent.h"
#include "wolfram/feed_typed.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_fg_strdup(const char *s) {
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

static wf_status wf_fg_set_string(char **dst, const char *src) {
    char *copy = wf_fg_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_fg_profile_view_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static void wf_fg_generator_view_reset(wf_agent_generator_view *g) {
    if (!g) {
        return;
    }
    free(g->uri);
    free(g->cid);
    free(g->did);
    wf_fg_profile_view_reset(&g->creator);
    free(g->display_name);
    free(g->description);
    free(g->avatar);
    free(g->indexed_at);
    memset(g, 0, sizeof(*g));
}

static void wf_fg_generator_list_reset(wf_agent_generator_view_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->generator_count; ++i) {
        wf_fg_generator_view_reset(&list->generators[i]);
    }
    free(list->generators);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

static void wf_fg_feed_view_reset(wf_agent_feed_view *f) {
    if (!f) {
        return;
    }
    free(f->uri);
    free(f->cid);
    free(f->did);
    free(f->display_name);
    free(f->description);
    free(f->avatar);
    free(f->indexed_at);
    memset(f, 0, sizeof(*f));
}

static void wf_fg_feed_view_list_reset(wf_agent_feed_view_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->feed_count; ++i) {
        wf_fg_feed_view_reset(&list->feeds[i]);
    }
    free(list->feeds);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* Parse a `likeCount`-style integer, tracking presence. */
static wf_status wf_fg_parse_int(cJSON *num, int *dst, int *has) {
    if (cJSON_IsNumber(num)) {
        double v = num->valuedouble;
        if (v > 2147483647.0) {
            *dst = 2147483647;
        } else if (v < -2147483648.0) {
            *dst = -2147483648;
        } else {
            *dst = (int)v;
        }
        *has = 1;
    }
    return WF_OK;
}

static wf_status wf_fg_parse_creator(wf_agent_profile_view *creator, cJSON *obj) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_OK;
    }
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_fg_set_string(&creator->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_fg_set_string(&creator->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_fg_set_string(&creator->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_fg_set_string(&creator->avatar, avatar->valuestring);
    }
    return status;
}

wf_status wf_agent_parse_generators(const char *json, size_t json_len,
                                    wf_agent_generator_view_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "feeds");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_generator_view *items = NULL;
    if (count > 0) {
        items = (wf_agent_generator_view *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_generator_view *g = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }

        cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
        cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
        cJSON *creator = cJSON_GetObjectItemCaseSensitive(obj, "creator");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
        cJSON *desc = cJSON_GetObjectItemCaseSensitive(obj, "description");
        cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
        cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");
        cJSON *like = cJSON_GetObjectItemCaseSensitive(obj, "likeCount");

        if (cJSON_IsString(uri) && uri->valuestring) {
            status = wf_fg_set_string(&g->uri, uri->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
            status = wf_fg_set_string(&g->cid, cid->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(did) && did->valuestring) {
            status = wf_fg_set_string(&g->did, did->valuestring);
        }
        if (status == WF_OK) {
            status = wf_fg_parse_creator(&g->creator, creator);
        }
        if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
            status = wf_fg_set_string(&g->display_name, name->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
            status = wf_fg_set_string(&g->description, desc->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
            status = wf_fg_set_string(&g->avatar, avatar->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
            status = wf_fg_set_string(&g->indexed_at, indexed->valuestring);
        }
        if (status == WF_OK) {
            status = wf_fg_parse_int(like, &g->like_count, &g->has_like_count);
        }

        if (status != WF_OK) {
            wf_fg_generator_view_reset(g);
        }
    }

    if (status == WF_OK) {
        out->generators = items;
        out->generator_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_fg_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_fg_generator_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_generator_view_list_free(wf_agent_generator_view_list *list) {
    wf_fg_generator_list_reset(list);
}

wf_status wf_agent_parse_feed_views(const char *json, size_t json_len,
                                    wf_agent_feed_view_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "feed");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_feed_view *items = NULL;
    if (count > 0) {
        items = (wf_agent_feed_view *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_feed_view *f = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }

        cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
        cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
        cJSON *desc = cJSON_GetObjectItemCaseSensitive(obj, "description");
        cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
        cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

        if (cJSON_IsString(uri) && uri->valuestring) {
            status = wf_fg_set_string(&f->uri, uri->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
            status = wf_fg_set_string(&f->cid, cid->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(did) && did->valuestring) {
            status = wf_fg_set_string(&f->did, did->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
            status = wf_fg_set_string(&f->display_name, name->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
            status = wf_fg_set_string(&f->description, desc->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
            status = wf_fg_set_string(&f->avatar, avatar->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
            status = wf_fg_set_string(&f->indexed_at, indexed->valuestring);
        }

        if (status != WF_OK) {
            wf_fg_feed_view_reset(f);
        }
    }

    if (status == WF_OK) {
        out->feeds = items;
        out->feed_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_fg_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_fg_feed_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_feed_view_list_free(wf_agent_feed_view_list *list) {
    wf_fg_feed_view_list_reset(list);
}

/* ---- Typed high-level wrappers -------------------------------------------- */

wf_status wf_agent_get_actor_feeds_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         wf_agent_generator_view_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_actor_feeds(agent, actor, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_generators(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_feed_typed(wf_agent *agent, const char *feed_uri,
                                  int limit, const char *cursor,
                                  wf_agent_feed_view_list *out) {
    if (!agent || !feed_uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_feed(agent, feed_uri, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_feed_views(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_actor_likes_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         wf_agent_feed_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_actor_likes(agent, actor, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    /* getActorLikes returns a `feed` array of feedViewPost; reuse the existing
     * feed parser (wf_agent_parse_feed). */
    status = wf_agent_parse_feed(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}
