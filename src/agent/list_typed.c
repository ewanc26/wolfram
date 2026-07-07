/*
 * list_typed.c — typed parser for Bluesky list endpoints.
 *
 * See include/wolfram/list_typed.h for the public API and ownership rules.
 * Follows the same conventions as graph_typed.c: static strdup/set_string/
 * reset helpers, owned strings, full cleanup on first error.
 */

#include "wolfram/list_typed.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_list_strdup(const char *s) {
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

static wf_status wf_list_set_string(char **dst, const char *src) {
    char *copy = wf_list_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_list_profile_view_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static void wf_list_view_reset(wf_agent_list_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    free(v->name);
    free(v->purpose);
    free(v->description);
    free(v->avatar);
    free(v->indexed_at);
    memset(v, 0, sizeof(*v));
}

static void wf_list_view_list_reset(wf_agent_list_view_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->list_count; ++i) {
        wf_list_view_reset(&list->lists[i]);
    }
    free(list->lists);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* Parse a listView object into an owned wf_agent_list_view. */
static wf_status wf_list_parse_view(cJSON *obj, wf_agent_list_view *v) {
    if (!cJSON_IsObject(obj) || !v) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *purpose = cJSON_GetObjectItemCaseSensitive(obj, "purpose");
    cJSON *description = cJSON_GetObjectItemCaseSensitive(obj, "description");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_list_set_string(&v->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_list_set_string(&v->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_list_set_string(&v->name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(purpose) && purpose->valuestring) {
        status = wf_list_set_string(&v->purpose, purpose->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(description) && description->valuestring) {
        status = wf_list_set_string(&v->description, description->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_list_set_string(&v->avatar, avatar->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_list_set_string(&v->indexed_at, indexed->valuestring);
    }

    return status;
}

/* Parse a profileView object (the subject of a listItemView). */
static wf_status wf_list_parse_profile_view(cJSON *obj,
                                            wf_agent_profile_view *p) {
    if (!cJSON_IsObject(obj) || !p) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");

    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_list_set_string(&p->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_list_set_string(&p->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_list_set_string(&p->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_list_set_string(&p->avatar, avatar->valuestring);
    }

    return status;
}

wf_status wf_agent_parse_lists(const char *json, size_t json_len,
                               wf_agent_list_view_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *lists = cJSON_GetObjectItemCaseSensitive(root, "lists");
    if (!cJSON_IsArray(lists)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(lists);
    wf_agent_list_view *items = NULL;
    if (count > 0) {
        items = (wf_agent_list_view *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(lists, (int)i);
        status = wf_list_parse_view(obj, &items[i]);
        if (status != WF_OK) {
            wf_list_view_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        out->lists = items;
        out->list_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_list_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_list_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_list_view_list_free(wf_agent_list_view_list *list) {
    wf_list_view_list_reset(list);
}

wf_status wf_agent_parse_list_items(const char *json, size_t json_len,
                                    wf_agent_list_item_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;

    cJSON *list_obj = cJSON_GetObjectItemCaseSensitive(root, "list");
    if (!cJSON_IsObject(list_obj)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    status = wf_list_parse_view(list_obj, &out->list);
    if (status != WF_OK) {
        wf_list_view_reset(&out->list);
    }

    cJSON *items = NULL;
    size_t count = 0;
    wf_agent_list_item *elems = NULL;
    if (status == WF_OK) {
        items = cJSON_GetObjectItemCaseSensitive(root, "items");
        if (!cJSON_IsArray(items)) {
            status = WF_ERR_PARSE;
        }
    }

    if (status == WF_OK) {
        count = (size_t)cJSON_GetArraySize(items);
        if (count > 0) {
            elems = (wf_agent_list_item *)calloc(count, sizeof(*elems));
            if (!elems) {
                status = WF_ERR_ALLOC;
            }
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_list_item *it = &elems[i];
        cJSON *obj = cJSON_GetArrayItem(items, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }

        cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
        cJSON *created = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
        cJSON *subject = cJSON_GetObjectItemCaseSensitive(obj, "subject");

        if (cJSON_IsString(uri) && uri->valuestring) {
            status = wf_list_set_string(&it->uri, uri->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(created) && created->valuestring) {
            status = wf_list_set_string(&it->created_at, created->valuestring);
        }
        if (status == WF_OK) {
            status = wf_list_parse_profile_view(subject, &it->subject);
        }

        if (status != WF_OK) {
            free(it->uri);
            free(it->created_at);
            it->uri = NULL;
            it->created_at = NULL;
            wf_list_profile_view_reset(&it->subject);
        }
    }

    if (status == WF_OK) {
        out->items = elems;
        out->item_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_list_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            free(elems[i].uri);
            free(elems[i].created_at);
            wf_list_profile_view_reset(&elems[i].subject);
        }
        free(elems);
        wf_list_view_reset(&out->list);
        free(out->cursor);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_list_item_list_free(wf_agent_list_item_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->item_count; ++i) {
        free(list->items[i].uri);
        free(list->items[i].created_at);
        wf_list_profile_view_reset(&list->items[i].subject);
    }
    free(list->items);
    wf_list_view_reset(&list->list);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* Typed high-level wrappers — call the raw agent endpoint, then parse. */

wf_status wf_agent_get_lists_typed(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor,
                                   wf_agent_list_view_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_lists(agent, actor, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_lists(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_list_typed(wf_agent *agent, const char *list_uri,
                                  int limit, const char *cursor,
                                  wf_agent_list_item_list *out) {
    if (!agent || !list_uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_list(agent, list_uri, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_list_items(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}
