/*
 * graph_social_typed.c — owned typed parsers + agent convenience wrappers for
 * the graph social namespaces (mutes / blocks / lists / starter packs). See
 * include/wolfram/graph_social_typed.h for the public API, the authoritative
 * wire format, and ownership rules.
 *
 * Mirrors labeler_typed.c / actor_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra` cJSON subtrees where shapes are
 * open/unbounded, and full cleanup on the first error. The agent wrappers call
 * the generated lex wrappers directly after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/graph_social_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/syntax.h"

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

/* ------------------------------------------------------------------ */
/* profileView (reused wf_agent_profile_view)                         */
/* ------------------------------------------------------------------ */

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

static wf_status wf_graph_parse_profile_view(wf_agent_profile_view *p,
                                             cJSON *obj) {
    wf_status status = WF_OK;
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
    return status;
}

/* ------------------------------------------------------------------ */
/* listView                                                           */
/* ------------------------------------------------------------------ */

static void wf_graph_list_view_reset(wf_graph_list_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    wf_graph_profile_view_reset(&v->creator);
    free(v->name);
    free(v->purpose);
    free(v->description);
    if (v->description_facets) {
        cJSON_Delete(v->description_facets);
    }
    free(v->avatar);
    if (v->labels) {
        cJSON_Delete(v->labels);
    }
    if (v->viewer) {
        cJSON_Delete(v->viewer);
    }
    free(v->indexed_at);
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

static wf_status wf_graph_parse_list_view(cJSON *obj, wf_graph_list_view *v) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *creator = cJSON_GetObjectItemCaseSensitive(obj, "creator");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *purpose = cJSON_GetObjectItemCaseSensitive(obj, "purpose");
    cJSON *description = cJSON_GetObjectItemCaseSensitive(obj, "description");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    cJSON *lic = cJSON_GetObjectItemCaseSensitive(obj, "listItemCount");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_graph_set_string(&v->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_graph_set_string(&v->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(creator)) {
        status = wf_graph_parse_profile_view(&v->creator, creator);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_graph_set_string(&v->name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(purpose) && purpose->valuestring) {
        status = wf_graph_set_string(&v->purpose, purpose->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(description) &&
        description->valuestring) {
        status = wf_graph_set_string(&v->description, description->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_graph_set_string(&v->avatar, avatar->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(lic)) {
        v->has_list_item_count = true;
        v->list_item_count = (int64_t)lic->valuedouble;
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_graph_set_string(&v->indexed_at, indexed->valuestring);
    }

    if (status == WF_OK) {
        v->description_facets =
            cJSON_DetachItemFromObject(obj, "descriptionFacets");
        v->labels = cJSON_DetachItemFromObject(obj, "labels");
        v->viewer = cJSON_DetachItemFromObject(obj, "viewer");
        v->extra = obj; /* remaining fields owned by caller */
    } else {
        wf_graph_list_view_reset(v);
    }
    return status;
}

wf_status wf_graph_parse_list_views(const char *json, size_t json_len,
                                    wf_graph_list_view_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "lists");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_graph_list_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_graph_list_view *)calloc(count, sizeof(*items));
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
        status = wf_graph_parse_list_view(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_graph_list_view_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_graph_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->lists = items;
        out->list_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_list_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ------------------------------------------------------------------ */
/* listItemView                                                      */
/* ------------------------------------------------------------------ */

static void wf_graph_list_item_view_reset(wf_graph_list_item_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    wf_graph_profile_view_reset(&v->subject);
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

/* Parse a single listItemView object into an owned wf_graph_list_item_view. */
static wf_status wf_graph_parse_list_item_view(cJSON *obj,
                                               wf_graph_list_item_view *v) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *subject = cJSON_GetObjectItemCaseSensitive(obj, "subject");
    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_graph_set_string(&v->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(subject)) {
        status = wf_graph_parse_profile_view(&v->subject, subject);
    }
    if (status != WF_OK) {
        wf_graph_list_item_view_reset(v);
    }
    return status;
}

wf_status wf_graph_parse_list_item_views(const char *json, size_t json_len,
                                          wf_graph_list_item_view_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_graph_list_item_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_graph_list_item_view *)calloc(count, sizeof(*items));
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
        wf_graph_list_item_view *it = &items[i];
        status = wf_graph_parse_list_item_view(obj, it);
        if (status == WF_OK) {
            it->extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_graph_list_item_view_reset(it);
        }
    }
    free(ptrs);


    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_graph_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->item_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_list_item_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ------------------------------------------------------------------ */
/* starterPackViewBasic / starterPackView                            */
/* ------------------------------------------------------------------ */

static void wf_graph_starter_pack_view_reset(wf_graph_starter_pack_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    free(v->name);
    free(v->description);
    wf_graph_profile_view_reset(&v->creator);
    free(v->created_at);
    if (v->labels) {
        cJSON_Delete(v->labels);
    }
    free(v->indexed_at);
    if (v->record) {
        cJSON_Delete(v->record);
    }
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

static wf_status wf_graph_parse_starter_pack_view(cJSON *obj,
                                                  wf_graph_starter_pack_view *v) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *creator = cJSON_GetObjectItemCaseSensitive(obj, "creator");
    cJSON *lic = cJSON_GetObjectItemCaseSensitive(obj, "listItemCount");
    cJSON *jwc = cJSON_GetObjectItemCaseSensitive(obj, "joinedWeekCount");
    cJSON *jac = cJSON_GetObjectItemCaseSensitive(obj, "joinedAllTimeCount");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");
    cJSON *record = cJSON_GetObjectItemCaseSensitive(obj, "record");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_graph_set_string(&v->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_graph_set_string(&v->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(creator)) {
        status = wf_graph_parse_profile_view(&v->creator, creator);
    }
    if (status == WF_OK && cJSON_IsNumber(lic)) {
        v->has_list_item_count = true;
        v->list_item_count = (int64_t)lic->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(jwc)) {
        v->has_joined_week_count = true;
        v->joined_week_count = (int64_t)jwc->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(jac)) {
        v->has_joined_all_time_count = true;
        v->joined_all_time_count = (int64_t)jac->valuedouble;
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_graph_set_string(&v->indexed_at, indexed->valuestring);
    }

    if (status == WF_OK && cJSON_IsObject(record)) {
        /* Pull a small typed subset out of the record. */
        cJSON *rname = cJSON_GetObjectItemCaseSensitive(record, "name");
        cJSON *rdesc = cJSON_GetObjectItemCaseSensitive(record, "description");
        cJSON *rcreated = cJSON_GetObjectItemCaseSensitive(record, "createdAt");
        if (cJSON_IsString(rname) && rname->valuestring) {
            status = wf_graph_set_string(&v->name, rname->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(rdesc) && rdesc->valuestring) {
            status = wf_graph_set_string(&v->description, rdesc->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(rcreated) &&
            rcreated->valuestring) {
            status = wf_graph_set_string(&v->created_at, rcreated->valuestring);
        }
        if (status == WF_OK) {
            v->record = cJSON_DetachItemFromObject(obj, "record");
        }
    }

    if (status == WF_OK) {
        v->labels = cJSON_DetachItemFromObject(obj, "labels");
        v->extra = obj; /* remaining fields (list, listItemsSample, feeds, ...) */
    } else {
        wf_graph_starter_pack_view_reset(v);
    }
    return status;
}

wf_status wf_graph_parse_starter_pack_views(const char *json, size_t json_len,
                                            wf_graph_starter_pack_view_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "starterPacks");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_graph_starter_pack_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_graph_starter_pack_view *)calloc(count, sizeof(*items));
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
        status = wf_graph_parse_starter_pack_view(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_graph_starter_pack_view_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_graph_set_string(&out->s_cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->packs = items;
        out->pack_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_starter_pack_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ------------------------------------------------------------------ */
/* listWithMembership / starterPackWithMembership                     */
/* ------------------------------------------------------------------ */

static void wf_graph_list_membership_reset(wf_graph_list_membership *m) {
    if (!m) {
        return;
    }
    wf_graph_list_view_reset(&m->list);
    wf_graph_list_item_view_reset(&m->list_item);
    memset(m, 0, sizeof(*m));
}

static void wf_graph_starter_pack_membership_reset(
    wf_graph_starter_pack_membership *m) {
    if (!m) {
        return;
    }
    wf_graph_starter_pack_view_reset(&m->starter_pack);
    wf_graph_list_item_view_reset(&m->list_item);
    memset(m, 0, sizeof(*m));
}

/* Parse `listsWithMembership` into owned list-with-membership entries. */
wf_status wf_graph_parse_list_memberships(const char *json, size_t json_len,
                                          wf_graph_list_membership_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "listsWithMembership");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_graph_list_membership *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_graph_list_membership *)calloc(count, sizeof(*items));
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
        wf_graph_list_membership *m = &items[i];
        cJSON *list = cJSON_GetObjectItemCaseSensitive(obj, "list");
        cJSON *list_item =
            cJSON_GetObjectItemCaseSensitive(obj, "listItem");
        if (!cJSON_IsObject(list)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_graph_parse_list_view(list, &m->list);
        if (status == WF_OK && cJSON_IsObject(list_item)) {
            status = wf_graph_parse_list_item_view(list_item, &m->list_item);
            if (status == WF_OK) {
                m->has_list_item = true;
            }
        }
        if (status == WF_OK) {
            m->list.extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_graph_list_membership_reset(m);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_graph_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->memberships = items;
        out->membership_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_list_membership_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* Parse `starterPacksWithMembership` into owned starter-pack-with-membership
 * entries. */
wf_status wf_graph_parse_starter_pack_memberships(
    const char *json, size_t json_len,
    wf_graph_starter_pack_membership_list *out) {
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
    cJSON *arr =
        cJSON_GetObjectItemCaseSensitive(root, "starterPacksWithMembership");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_graph_starter_pack_membership *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_graph_starter_pack_membership *)calloc(
            count, sizeof(*items));
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
        wf_graph_starter_pack_membership *m = &items[i];
        cJSON *pack =
            cJSON_GetObjectItemCaseSensitive(obj, "starterPack");
        cJSON *list_item =
            cJSON_GetObjectItemCaseSensitive(obj, "listItem");
        if (!cJSON_IsObject(pack)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_graph_parse_starter_pack_view(pack, &m->starter_pack);
        if (status == WF_OK && cJSON_IsObject(list_item)) {
            status = wf_graph_parse_list_item_view(list_item, &m->list_item);
            if (status == WF_OK) {
                m->has_list_item = true;
            }
        }
        if (status == WF_OK) {
            m->starter_pack.extra =
                cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_graph_starter_pack_membership_reset(m);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_graph_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->memberships = items;
        out->membership_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_graph_starter_pack_membership_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ------------------------------------------------------------------ */
/* Free functions                                                     */
/* ------------------------------------------------------------------ */

void wf_graph_list_view_free(wf_graph_list_view *v) {
    wf_graph_list_view_reset(v);
}

void wf_graph_list_view_list_free(wf_graph_list_view_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->list_count; ++i) {
        wf_graph_list_view_reset(&list->lists[i]);
    }
    free(list->lists);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_graph_list_item_view_list_free(wf_graph_list_item_view_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->item_count; ++i) {
        wf_graph_list_item_view_reset(&list->items[i]);
    }
    free(list->items);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_graph_starter_pack_view_free(wf_graph_starter_pack_view *v) {
    wf_graph_starter_pack_view_reset(v);
}

void wf_graph_starter_pack_view_list_free(wf_graph_starter_pack_view_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->pack_count; ++i) {
        wf_graph_starter_pack_view_reset(&list->packs[i]);
    }
    free(list->packs);
    free(list->s_cursor);
    memset(list, 0, sizeof(*list));
}

void wf_graph_list_membership_free(wf_graph_list_membership *m) {
    wf_graph_list_membership_reset(m);
}

void wf_graph_list_membership_list_free(wf_graph_list_membership_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->membership_count; ++i) {
        wf_graph_list_membership_reset(&list->memberships[i]);
    }
    free(list->memberships);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_graph_starter_pack_membership_free(
    wf_graph_starter_pack_membership *m) {
    wf_graph_starter_pack_membership_reset(m);
}

void wf_graph_starter_pack_membership_list_free(
    wf_graph_starter_pack_membership_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->membership_count; ++i) {
        wf_graph_starter_pack_membership_reset(&list->memberships[i]);
    }
    free(list->memberships);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* ------------------------------------------------------------------ */
/* Agent convenience wrappers                                         */
/* ------------------------------------------------------------------ */

/* Helper: issue a generated query call, parse the body array under `key` into
 * an owned actor list. Returns the parser status; on error `out` is reset. */
static wf_status wf_graph_call_actor_list(
    wf_agent *agent, wf_status (*call)(wf_xrpc_client *,
                                       const void *, wf_response *),
    const void *params, const char *key, wf_agent_actor_list *out) {
    wf_agent_actor_list list = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = call(agent->client, params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_profile_views(res.body, res.body_len, key, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_suggested_follows_by_actor_typed(wf_agent *agent,
                                                        const char *actor,
                                                        wf_agent_actor_list *out) {
    if (!agent || !agent->client || !actor || !actor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_get_suggested_follows_by_actor_main_params params = {0};
    params.actor = actor;
    return wf_graph_call_actor_list(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_get_suggested_follows_by_actor_main_call,
        &params, "suggestions", out);
}

/* Helper: issue a generated query call, parse the body "lists" array into an
 * owned list-view list. */
static wf_status wf_graph_call_list_views(
    wf_agent *agent, wf_status (*call)(wf_xrpc_client *,
                                       const void *, wf_response *),
    const void *params, wf_graph_list_view_list *out) {
    wf_graph_list_view_list list = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = call(agent->client, params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_graph_parse_list_views(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

static void wf_graph_fill_limit_cursor(int *limit, const char *cursor,
                                      bool *has_limit, int64_t *out_limit,
                                      bool *has_cursor, const char **out_cursor) {
    if (*limit > 0) {
        *has_limit = true;
        *out_limit = *limit;
    }
    if (cursor && cursor[0]) {
        *has_cursor = true;
        *out_cursor = cursor;
    }
}

wf_status wf_agent_get_list_mutes_typed(wf_agent *agent, int limit,
                                        const char *cursor,
                                        wf_graph_list_view_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_get_list_mutes_main_params params = {0};
    wf_graph_fill_limit_cursor(&limit, cursor, &params.has_limit, &params.limit,
                               &params.has_cursor, &params.cursor);
    return wf_graph_call_list_views(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_get_list_mutes_main_call,
        &params, out);
}

wf_status wf_agent_get_list_blocks_typed(wf_agent *agent, int limit,
                                         const char *cursor,
                                         wf_graph_list_view_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_get_list_blocks_main_params params = {0};
    wf_graph_fill_limit_cursor(&limit, cursor, &params.has_limit, &params.limit,
                               &params.has_cursor, &params.cursor);
    return wf_graph_call_list_views(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_get_list_blocks_main_call,
        &params, out);
}

wf_status wf_agent_get_actor_lists_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         wf_graph_list_view_list *out) {
    /* TODO: app.bsky.graph.getActorLists has no generated lex wrapper in
     * atproto_lex.h (the NSID is absent from the local lexicon set). The
     * response is a "lists" array of listView identical to getListMutes; wire
     * this up once the generated binding exists. */
    (void)agent;
    (void)actor;
    (void)limit;
    (void)cursor;
    (void)out;
    return WF_ERR_INVALID_ARG;
}

/* Helper: issue a generated query call, parse the body "starterPacks" array
 * into an owned starter-pack list. */
static wf_status wf_graph_call_starter_packs(
    wf_agent *agent, wf_status (*call)(wf_xrpc_client *,
                                       const void *, wf_response *),
    const void *params, wf_graph_starter_pack_view_list *out) {
    wf_graph_starter_pack_view_list list = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = call(agent->client, params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_graph_parse_starter_pack_views(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_starter_packs_typed(wf_agent *agent,
                                           const char *const *uris,
                                           size_t uri_count,
                                           wf_graph_starter_pack_view_list *out) {
    if (!agent || !agent->client || !uris || uri_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < uri_count; ++i) {
        if (!uris[i] || !uris[i][0]) {
            return WF_ERR_INVALID_ARG;
        }
    }
    wf_lex_app_bsky_graph_get_starter_packs_main_params params = {0};
    params.uris.items = uris;
    params.uris.count = uri_count;
    return wf_graph_call_starter_packs(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_get_starter_packs_main_call,
        &params, out);
}

wf_status wf_agent_get_actor_starter_packs_typed(wf_agent *agent,
                                                 const char *actor, int limit,
                                                 const char *cursor,
                                                 wf_graph_starter_pack_view_list *out) {
    if (!agent || !agent->client || !actor || !actor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_get_actor_starter_packs_main_params params = {0};
    params.actor = actor;
    wf_graph_fill_limit_cursor(&limit, cursor, &params.has_limit, &params.limit,
                               &params.has_cursor, &params.cursor);
    return wf_graph_call_starter_packs(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_get_actor_starter_packs_main_call,
        &params, out);
}

wf_status wf_agent_get_starter_pack_typed(wf_agent *agent,
                                          const char *starter_pack_uri,
                                          wf_graph_starter_pack_view *out) {
    if (!agent || !agent->client || !starter_pack_uri || !starter_pack_uri[0] ||
        !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_lex_app_bsky_graph_get_starter_pack_main_params params = {0};
    params.starter_pack = starter_pack_uri;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_graph_get_starter_pack_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_graph_starter_pack_view_list list = {0};
    status = wf_graph_parse_starter_pack_views(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK && list.pack_count >= 1) {
        *out = list.packs[0];
        /* Transfer ownership: zero the slot so the list free is a no-op. */
        memset(&list.packs[0], 0, sizeof(list.packs[0]));
    }
    wf_graph_starter_pack_view_list_free(&list);
    return status;
}

wf_status wf_agent_get_lists_with_membership_typed(
    wf_agent *agent, const char *actor, int limit, const char *cursor,
    wf_graph_list_membership_list *out) {
    if (!agent || !agent->client || !actor || !actor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_get_lists_with_membership_main_params params = {0};
    params.actor = actor;
    wf_graph_fill_limit_cursor(&limit, cursor, &params.has_limit, &params.limit,
                               &params.has_cursor, &params.cursor);
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_graph_get_lists_with_membership_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_graph_parse_list_memberships(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_starter_packs_with_membership_typed(
    wf_agent *agent, const char *actor, int limit, const char *cursor,
    wf_graph_starter_pack_membership_list *out) {
    if (!agent || !agent->client || !actor || !actor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_get_starter_packs_with_membership_main_params params =
        {0};
    params.actor = actor;
    wf_graph_fill_limit_cursor(&limit, cursor, &params.has_limit, &params.limit,
                               &params.has_cursor, &params.cursor);
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_graph_get_starter_packs_with_membership_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_graph_parse_starter_pack_memberships(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

/* ------------------------------------------------------------------ */
/* WRITE (procedure) wrappers                                         */
/* ------------------------------------------------------------------ */

static wf_status wf_graph_call_procedure(
    wf_agent *agent, wf_status (*call)(wf_xrpc_client *,
                                       const void *, wf_response *),
    const void *params) {
    if (!agent || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = call(agent->client, params, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_mute_actor_typed(wf_agent *agent, const char *actor) {
    if (!agent || !actor || !actor[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_mute_actor_main_input input = {0};
    input.actor = actor;
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_mute_actor_main_call,
        &input);
}

wf_status wf_agent_unmute_actor_typed(wf_agent *agent, const char *actor) {
    if (!agent || !actor || !actor[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_unmute_actor_main_input input = {0};
    input.actor = actor;
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_unmute_actor_main_call,
        &input);
}

wf_status wf_agent_mute_actor_list_typed(wf_agent *agent, const char *list_uri) {
    if (!agent || !list_uri || !list_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_mute_actor_list_main_input input = {0};
    input.list = list_uri;
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_mute_actor_list_main_call,
        &input);
}

wf_status wf_agent_unmute_actor_list_typed(wf_agent *agent,
                                           const char *list_uri) {
    if (!agent || !list_uri || !list_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_graph_unmute_actor_list_main_input input = {0};
    input.list = list_uri;
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_unmute_actor_list_main_call,
        &input);
}

wf_status wf_agent_block_actor_typed(wf_agent *agent, const char *actor) {
    /* TODO: app.bsky.graph.blockActor has no generated lex wrapper in
     * atproto_lex.h. Wire up (procedure; input {actor}) once generated. */
    (void)agent;
    (void)actor;
    return WF_ERR_INVALID_ARG;
}

wf_status wf_agent_unblock_actor_typed(wf_agent *agent, const char *actor) {
    /* TODO: app.bsky.graph.unblockActor has no generated lex wrapper in
     * atproto_lex.h. Wire up (procedure; input {actor}) once generated. */
    (void)agent;
    (void)actor;
    return WF_ERR_INVALID_ARG;
}

wf_status wf_agent_block_actor_list_typed(wf_agent *agent,
                                          const char *list_uri) {
    /* TODO: app.bsky.graph.blockActorList has no generated lex wrapper in
     * atproto_lex.h. Wire up (procedure; input {list}) once generated. */
    (void)agent;
    (void)list_uri;
    return WF_ERR_INVALID_ARG;
}

wf_status wf_agent_unblock_actor_list_typed(wf_agent *agent,
                                            const char *list_uri) {
    /* TODO: app.bsky.graph.unblockActorList has no generated lex wrapper in
     * atproto_lex.h. Wire up (procedure; input {list}) once generated. */
    (void)agent;
    (void)list_uri;
    return WF_ERR_INVALID_ARG;
}
