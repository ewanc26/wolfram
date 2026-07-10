/*
 * graph_typed.c — typed parsers for social-graph follow relationships and the
 * shared like-list parser.
 *
 * Actor-profile parsing and the actor/search/suggestions typed wrappers now
 * live in actor_typed.c; this file keeps only the graph-domain concerns and
 * reuses wf_agent_parse_actors / wf_agent_actor_list_free from actor_typed.h.
 *
 * Follows the same conventions as notification.c / feed_typed.c: static
 * strdup/set_string/reset helpers, owned strings, full cleanup on first error.
 */

#include "wolfram/graph_typed.h"
#include "wolfram/actor_typed.h"

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

static void wf_graph_like_reset(wf_agent_like_item *l) {
    if (!l) {
        return;
    }
    wf_graph_profile_view_reset(&l->actor);
    free(l->created_at);
    free(l->indexed_at);
    memset(l, 0, sizeof(*l));
}

static void wf_graph_like_list_reset(wf_agent_like_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->like_count; ++i) {
        wf_graph_like_reset(&list->likes[i]);
    }
    free(list->likes);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

wf_status wf_agent_parse_likes(const char *json, size_t json_len,
                               wf_agent_like_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *likes = cJSON_GetObjectItemCaseSensitive(root, "likes");
    if (!cJSON_IsArray(likes)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(likes);
    wf_agent_like_item *items = NULL;
    if (count > 0) {
        items = (wf_agent_like_item *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_like_item *l = &items[i];
        cJSON *obj = cJSON_GetArrayItem(likes, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }

        cJSON *actor = cJSON_GetObjectItemCaseSensitive(obj, "actor");
        cJSON *created = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
        cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

        if (cJSON_IsObject(actor)) {
            cJSON *did = cJSON_GetObjectItemCaseSensitive(actor, "did");
            cJSON *handle = cJSON_GetObjectItemCaseSensitive(actor, "handle");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(actor, "displayName");
            cJSON *avatar = cJSON_GetObjectItemCaseSensitive(actor, "avatar");
            if (cJSON_IsString(did) && did->valuestring) {
                status = wf_graph_set_string(&l->actor.did, did->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
                status = wf_graph_set_string(&l->actor.handle, handle->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
                status = wf_graph_set_string(&l->actor.display_name,
                                             name->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
                status = wf_graph_set_string(&l->actor.avatar, avatar->valuestring);
            }
        }
        if (status == WF_OK && cJSON_IsString(created) && created->valuestring) {
            status = wf_graph_set_string(&l->created_at, created->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
            status = wf_graph_set_string(&l->indexed_at, indexed->valuestring);
        }

        if (status != WF_OK) {
            wf_graph_like_reset(l);
        }
    }

    if (status == WF_OK) {
        out->likes = items;
        out->like_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_graph_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_like_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_like_list_free(wf_agent_like_list *list) {
    wf_graph_like_list_reset(list);
}

/* Typed high-level wrappers — call the raw agent endpoint, then parse. */

wf_status wf_agent_get_follows_typed(wf_agent *agent, const char *actor,
                                     int limit, const char *cursor,
                                     wf_agent_actor_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
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
    if (limit < 0 || limit > 100) {
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

/* ── getRelationships / muteThread / unmuteThread ───────────────────────── */

static void wf_graph_relationship_reset(wf_agent_relationship *r) {
    if (!r) {
        return;
    }
    free(r->did);
    free(r->following);
    free(r->followed_by);
    free(r->blocking);
    free(r->blocked_by);
    free(r->blocking_by_list);
    free(r->blocked_by_list);
    memset(r, 0, sizeof(*r));
}

static void wf_graph_relationship_list_reset(wf_agent_relationship_list *list) {
    if (!list) {
        return;
    }
    free(list->actor);
    for (size_t i = 0; i < list->rel_count; ++i) {
        wf_graph_relationship_reset(&list->rels[i]);
    }
    free(list->rels);
    memset(list, 0, sizeof(*list));
}

/* Copy `src` into `dst` when it is a non-empty string; leave `dst` alone
 * (no error) when the value is absent/null. */
static wf_status wf_graph_set_opt_aturi(char **dst, cJSON *src) {
    if (cJSON_IsString(src) && src->valuestring) {
        return wf_graph_set_string(dst, src->valuestring);
    }
    return WF_OK;
}

wf_status wf_agent_parse_relationships(const char *json, size_t json_len,
                                       wf_agent_relationship_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *actor = cJSON_GetObjectItemCaseSensitive(root, "actor");
    if (cJSON_IsString(actor) && actor->valuestring) {
        status = wf_graph_set_string(&out->actor, actor->valuestring);
    }

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "relationships");
    if (status == WF_OK && !cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_relationship *items = NULL;
    if (status == WF_OK && count > 0) {
        items = (wf_agent_relationship *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_relationship *r = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
        if (cJSON_IsString(did) && did->valuestring) {
            status = wf_graph_set_string(&r->did, did->valuestring);
        } else {
            status = WF_ERR_PARSE;
            break;
        }
        if (status == WF_OK) {
            status = wf_graph_set_opt_aturi(&r->following,
                cJSON_GetObjectItemCaseSensitive(obj, "following"));
        }
        if (status == WF_OK) {
            status = wf_graph_set_opt_aturi(&r->followed_by,
                cJSON_GetObjectItemCaseSensitive(obj, "followedBy"));
        }
        if (status == WF_OK) {
            status = wf_graph_set_opt_aturi(&r->blocking,
                cJSON_GetObjectItemCaseSensitive(obj, "blocking"));
        }
        if (status == WF_OK) {
            status = wf_graph_set_opt_aturi(&r->blocked_by,
                cJSON_GetObjectItemCaseSensitive(obj, "blockedBy"));
        }
        if (status == WF_OK) {
            status = wf_graph_set_opt_aturi(&r->blocking_by_list,
                cJSON_GetObjectItemCaseSensitive(obj, "blockingByList"));
        }
        if (status == WF_OK) {
            status = wf_graph_set_opt_aturi(&r->blocked_by_list,
                cJSON_GetObjectItemCaseSensitive(obj, "blockedByList"));
        }
        if (status != WF_OK) {
            wf_graph_relationship_reset(r);
        }
    }

    if (status == WF_OK) {
        out->rels = items;
        out->rel_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_relationship_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_relationship_list_free(wf_agent_relationship_list *list) {
    wf_graph_relationship_list_reset(list);
}

wf_status wf_agent_get_relationships_typed(wf_agent *agent, const char *actor,
                                            const char *const *others,
                                            size_t others_count,
                                            wf_agent_relationship_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_relationships(agent, actor, others,
                                                  others_count, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_relationships(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_mute_thread_typed(wf_agent *agent, const char *root_uri) {
    if (!agent || !root_uri || !root_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_mute_thread(agent, root_uri);
}

wf_status wf_agent_unmute_thread_typed(wf_agent *agent, const char *root_uri) {
    if (!agent || !root_uri || !root_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_unmute_thread(agent, root_uri);
}
