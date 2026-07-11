/*
 * repo_typed.c — owned typed agent wrappers for the com.atproto.repo XRPC
 * endpoints. See include/wolfram/repo_typed.h for the public API, the
 * authoritative wire format, and ownership rules.
 *
 * Mirrors labeler_typed.c: static strdup/set_string/reset helpers, owned
 * strings, detached `extra` cJSON subtrees where shapes are open/unbounded,
 * and full cleanup on the first error. The agent wrappers call the generated
 * lex wrappers directly after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/repo_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_repo_strdup(const char *s) {
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

static wf_status wf_repo_set_string(char **dst, const char *src) {
    char *copy = wf_repo_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- record ---- */

void wf_repo_record_free(wf_repo_record *r) {
    if (!r) {
        return;
    }
    free(r->uri);
    free(r->cid);
    if (r->value) {
        cJSON_Delete(r->value);
    }
    memset(r, 0, sizeof(*r));
}

static void wf_repo_record_reset(wf_repo_record *r) {
    if (!r) {
        return;
    }
    free(r->uri);
    free(r->cid);
    if (r->value) {
        cJSON_Delete(r->value);
    }
    memset(r, 0, sizeof(*r));
}

static wf_status wf_repo_parse_record(cJSON *obj, wf_repo_record *r) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, "value");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_repo_set_string(&r->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        r->has_cid = true;
        status = wf_repo_set_string(&r->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(value)) {
        r->value = cJSON_DetachItemFromObject(obj, "value");
    } else if (status == WF_OK) {
        status = WF_ERR_PARSE;
    }
    return status;
}

/* ---- record list ---- */

void wf_repo_record_list_free(wf_repo_record_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->count; ++i) {
        wf_repo_record_reset(&l->items[i]);
    }
    free(l->items);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

wf_status wf_repo_parse_get_record(const char *json, size_t json_len,
                                   wf_repo_record *out) {
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

    wf_status status = wf_repo_parse_record(root, out);
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "uri");
        cJSON_DetachItemFromObject(root, "cid");
        cJSON_DetachItemFromObject(root, "value");
        cJSON_Delete(root);
    } else {
        wf_repo_record_free(out);
        cJSON_Delete(root);
    }
    return status;
}

wf_status wf_repo_parse_list_records(const char *json, size_t json_len,
                                     wf_repo_record_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "records");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_repo_record *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_repo_record *)calloc(count, sizeof(*items));
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
        cJSON *item = ptrs[i];
        if (!cJSON_IsObject(item)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_repo_parse_record(item, &items[i]);
        if (status == WF_OK) {
            items[i].value = cJSON_DetachItemViaPointer(arr, item);
        }
        if (status != WF_OK) {
            wf_repo_record_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_repo_set_string(&out->cursor, cursor->valuestring);
        }
    }
    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_repo_record_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ---- description ---- */

void wf_repo_description_free(wf_repo_description *d) {
    if (!d) {
        return;
    }
    free(d->handle);
    free(d->did);
    if (d->did_doc) {
        cJSON_Delete(d->did_doc);
    }
    for (size_t i = 0; i < d->collection_count; ++i) {
        free(d->collections[i]);
    }
    free(d->collections);
    if (d->extra) {
        cJSON_Delete(d->extra);
    }
    memset(d, 0, sizeof(*d));
}

static wf_status wf_repo_parse_string_array(cJSON *arr, char ***out_items,
                                            size_t *out_count) {
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(arr);
    if (n == 0) {
        *out_items = NULL;
        *out_count = 0;
        return WF_OK;
    }
    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n; ++i) {
        cJSON *it = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsString(it) || !it->valuestring) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_repo_set_string(&items[i], it->valuestring);
    }
    if (status == WF_OK) {
        *out_items = items;
        *out_count = n;
    } else {
        for (size_t i = 0; i < n; ++i) {
            free(items[i]);
        }
        free(items);
        *out_items = NULL;
        *out_count = 0;
    }
    return status;
}

wf_status wf_repo_parse_describe_repo(const char *json, size_t json_len,
                                      wf_repo_description *out) {
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
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    cJSON *did_doc = cJSON_GetObjectItemCaseSensitive(root, "didDoc");
    cJSON *collections = cJSON_GetObjectItemCaseSensitive(root, "collections");
    cJSON *hic = cJSON_GetObjectItemCaseSensitive(root, "handleIsCorrect");

    if (cJSON_IsString(handle) && handle->valuestring) {
        status = wf_repo_set_string(&out->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(did) && did->valuestring) {
        status = wf_repo_set_string(&out->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(did_doc)) {
        out->did_doc = cJSON_DetachItemFromObject(root, "didDoc");
    } else if (status == WF_OK && did_doc != NULL) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && collections != NULL) {
        status = wf_repo_parse_string_array(collections, &out->collections,
                                            &out->collection_count);
    } else if (status == WF_OK && collections == NULL) {
        out->collections = NULL;
        out->collection_count = 0;
    }
    if (status == WF_OK && cJSON_IsBool(hic)) {
        out->has_handle_is_correct = true;
        out->handle_is_correct = cJSON_IsTrue(hic);
    } else if (status == WF_OK && hic != NULL) {
        status = WF_ERR_PARSE;
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "handle");
        cJSON_DetachItemFromObject(root, "did");
        cJSON_DetachItemFromObject(root, "didDoc");
        cJSON_DetachItemFromObject(root, "collections");
        cJSON_DetachItemFromObject(root, "handleIsCorrect");
        out->extra = root;
    } else {
        wf_repo_description_free(out);
        cJSON_Delete(root);
    }
    return status;
}

/* ---- missing blobs ---- */

void wf_repo_missing_blob_list_free(wf_repo_missing_blob_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->count; ++i) {
        free(l->items[i].cid);
        free(l->items[i].record_uri);
    }
    free(l->items);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

static wf_status wf_repo_parse_missing_blob(cJSON *obj,
                                            wf_repo_missing_blob *b) {
    wf_status status = WF_OK;
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *record_uri = cJSON_GetObjectItemCaseSensitive(obj, "recordUri");
    if (cJSON_IsString(cid) && cid->valuestring) {
        status = wf_repo_set_string(&b->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(record_uri) && record_uri->valuestring) {
        status = wf_repo_set_string(&b->record_uri, record_uri->valuestring);
    }
    return status;
}

wf_status wf_repo_parse_list_missing_blobs(const char *json, size_t json_len,
                                           wf_repo_missing_blob_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "blobs");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_repo_missing_blob *items = NULL;
    if (count > 0) {
        items = (wf_repo_missing_blob *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_repo_parse_missing_blob(obj, &items[i]);
        if (status != WF_OK) {
            free(items[i].cid);
            free(items[i].record_uri);
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_repo_set_string(&out->cursor, cursor->valuestring);
        }
    }
    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            free(items[i].cid);
            free(items[i].record_uri);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ---- applyWrites result ---- */

void wf_repo_apply_writes_result_free(wf_repo_apply_writes_result *r) {
    if (!r) {
        return;
    }
    if (r->commit) {
        cJSON_Delete(r->commit);
    }
    free(r->commit_cid);
    free(r->commit_rev);
    if (r->results) {
        cJSON_Delete(r->results);
    }
    memset(r, 0, sizeof(*r));
}

static wf_status wf_repo_parse_apply_commit(cJSON *obj,
                                            wf_repo_apply_writes_result *r) {
    wf_status status = WF_OK;
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(obj, "rev");
    if (cJSON_IsString(cid) && cid->valuestring) {
        r->has_commit_cid = true;
        status = wf_repo_set_string(&r->commit_cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring) {
        r->has_commit_rev = true;
        status = wf_repo_set_string(&r->commit_rev, rev->valuestring);
    }
    return status;
}

wf_status wf_repo_parse_apply_writes(const char *json, size_t json_len,
                                     wf_repo_apply_writes_result *out) {
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
    cJSON *commit = cJSON_GetObjectItemCaseSensitive(root, "commit");
    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");

    if (cJSON_IsObject(commit)) {
        status = wf_repo_parse_apply_commit(commit, out);
        if (status == WF_OK) {
            out->commit = cJSON_DetachItemFromObject(root, "commit");
        }
    } else if (commit != NULL) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && cJSON_IsArray(results)) {
        out->results = cJSON_DetachItemFromObject(root, "results");
    } else if (status == WF_OK && results != NULL) {
        status = WF_ERR_PARSE;
    }

    if (status != WF_OK) {
        wf_repo_apply_writes_result_free(out);
        cJSON_Delete(root);
    }
    return status;
}

/* ---- applyWrites input builder ---- */

struct wf_repo_writes_builder {
    cJSON *root;
    cJSON *writes;
};

wf_status wf_repo_writes_builder_init(wf_repo_writes_builder **out) {
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    wf_repo_writes_builder *b =
        (wf_repo_writes_builder *)calloc(1, sizeof(*b));
    if (!b) {
        return WF_ERR_ALLOC;
    }
    b->root = cJSON_CreateObject();
    if (!b->root) {
        free(b);
        return WF_ERR_ALLOC;
    }
    b->writes = cJSON_AddArrayToObject(b->root, "writes");
    if (!b->writes) {
        cJSON_Delete(b->root);
        free(b);
        return WF_ERR_ALLOC;
    }
    *out = b;
    return WF_OK;
}

static wf_status wf_repo_writes_add_impl(wf_repo_writes_builder *b,
                                         const char *type,
                                         const char *collection,
                                         const char *rkey, cJSON *value) {
    if (!b || !collection || !collection[0]) {
        if (value) {
            cJSON_Delete(value);
        }
        return WF_ERR_INVALID_ARG;
    }
    cJSON *item = cJSON_CreateObject();
    if (!item) {
        if (value) {
            cJSON_Delete(value);
        }
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(item, "$type", type) ||
        !cJSON_AddStringToObject(item, "collection", collection)) {
        cJSON_Delete(item);
        if (value) {
            cJSON_Delete(value);
        }
        return WF_ERR_ALLOC;
    }
    if (rkey && rkey[0]) {
        if (!cJSON_AddStringToObject(item, "rkey", rkey)) {
            cJSON_Delete(item);
            if (value) {
                cJSON_Delete(value);
            }
            return WF_ERR_ALLOC;
        }
    }
    if (value) {
        if (!cJSON_AddItemToObject(item, "value", value)) {
            cJSON_Delete(item);
            cJSON_Delete(value);
            return WF_ERR_ALLOC;
        }
    }
    if (!cJSON_AddItemToArray(b->writes, item)) {
        cJSON_Delete(item);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

wf_status wf_repo_writes_add_create(wf_repo_writes_builder *b,
                                    const char *collection,
                                    const char *rkey_or_null, cJSON *value) {
    return wf_repo_writes_add_impl(b, "com.atproto.repo.applyWrites#create",
                                   collection, rkey_or_null, value);
}

wf_status wf_repo_writes_add_update(wf_repo_writes_builder *b,
                                    const char *collection, const char *rkey,
                                    cJSON *value) {
    if (!rkey || !rkey[0]) {
        if (value) {
            cJSON_Delete(value);
        }
        return WF_ERR_INVALID_ARG;
    }
    return wf_repo_writes_add_impl(b, "com.atproto.repo.applyWrites#update",
                                   collection, rkey, value);
}

wf_status wf_repo_writes_add_delete(wf_repo_writes_builder *b,
                                    const char *collection, const char *rkey) {
    return wf_repo_writes_add_impl(b, "com.atproto.repo.applyWrites#delete",
                                   collection, rkey, NULL);
}

wf_status wf_repo_writes_build_json(wf_repo_writes_builder *b, const char *repo,
                                    int validate,
                                    const char *swap_commit_or_null,
                                    char **out_json) {
    if (!b || !repo || !repo[0] || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    if (!cJSON_AddStringToObject(b->root, "repo", repo)) {
        return WF_ERR_ALLOC;
    }
    if (validate >= 0) {
        cJSON *v = cJSON_CreateBool(validate != 0);
        if (!v || !cJSON_AddItemToObject(b->root, "validate", v)) {
            cJSON_Delete(v);
            cJSON_DetachItemFromObject(b->root, "repo");
            return WF_ERR_ALLOC;
        }
    }
    if (swap_commit_or_null && swap_commit_or_null[0]) {
        if (!cJSON_AddStringToObject(b->root, "swapCommit",
                                     swap_commit_or_null)) {
            cJSON_DetachItemFromObject(b->root, "repo");
            cJSON_DetachItemFromObject(b->root, "validate");
            return WF_ERR_ALLOC;
        }
    }

    char *json = cJSON_PrintUnformatted(b->root);
    cJSON_DetachItemFromObject(b->root, "repo");
    cJSON_DetachItemFromObject(b->root, "validate");
    cJSON_DetachItemFromObject(b->root, "swapCommit");
    if (!json) {
        return WF_ERR_ALLOC;
    }
    *out_json = json;
    return WF_OK;
}

void wf_repo_writes_builder_free(wf_repo_writes_builder *b) {
    if (!b) {
        return;
    }
    if (b->root) {
        cJSON_Delete(b->root);
    }
    free(b);
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_get_record_typed(wf_agent *agent, const char *repo,
                                    const char *collection, const char *rkey,
                                    const char *cid_or_null,
                                    wf_repo_record *out) {
    if (!agent || !agent->client || !repo || !repo[0] || !collection ||
        !collection[0] || !rkey || !rkey[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_repo_record rec = {0};
    wf_lex_com_atproto_repo_get_record_main_params params = {0};
    params.repo = repo;
    params.collection = collection;
    params.rkey = rkey;
    if (cid_or_null && cid_or_null[0]) {
        params.has_cid = true;
        params.cid = cid_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_repo_get_record_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_repo_parse_get_record(res.body, res.body_len, &rec);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = rec;
    }
    return status;
}

wf_status wf_agent_list_records_typed(wf_agent *agent, const char *repo,
                                      const char *collection, int limit,
                                      const char *cursor, int reverse,
                                      wf_repo_record_list *out) {
    if (!agent || !agent->client || !repo || !repo[0] || !collection ||
        !collection[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_repo_record_list list = {0};
    wf_lex_com_atproto_repo_list_records_main_params params = {0};
    params.repo = repo;
    params.collection = collection;
    params.has_limit = true;
    params.limit = limit;
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    if (reverse) {
        params.has_reverse = true;
        params.reverse = true;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_repo_list_records_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_repo_parse_list_records(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_describe_repo_typed(wf_agent *agent, const char *repo,
                                       wf_repo_description *out) {
    if (!agent || !agent->client || !repo || !repo[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_repo_description desc = {0};
    wf_lex_com_atproto_repo_describe_repo_main_params params = {0};
    params.repo = repo;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_repo_describe_repo_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_repo_parse_describe_repo(res.body, res.body_len, &desc);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = desc;
    }
    return status;
}

wf_status wf_agent_list_missing_blobs_typed(wf_agent *agent, int limit,
                                            const char *cursor,
                                            wf_repo_missing_blob_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 1000) {
        return WF_ERR_INVALID_ARG;
    }

    wf_repo_missing_blob_list list = {0};
    wf_lex_com_atproto_repo_list_missing_blobs_main_params params = {0};
    params.has_limit = true;
    params.limit = limit;
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_repo_list_missing_blobs_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_repo_parse_list_missing_blobs(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_apply_writes_typed(wf_agent *agent, const char *writes_json,
                                      wf_repo_apply_writes_result *out) {
    if (!agent || !agent->client || !writes_json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(writes_json);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    wf_lex_com_atproto_repo_apply_writes_main_input input = {0};
    wf_lex_com_atproto_repo_apply_writes_main_input_writes_item_union *writes = NULL;

    cJSON *repo = cJSON_GetObjectItemCaseSensitive(root, "repo");
    cJSON *validate = cJSON_GetObjectItemCaseSensitive(root, "validate");
    cJSON *swap = cJSON_GetObjectItemCaseSensitive(root, "swapCommit");
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "writes");

    if (!cJSON_IsString(repo) || !repo->valuestring) {
        status = WF_ERR_PARSE;
    } else {
        input.repo = repo->valuestring;
    }
    if (status == WF_OK && cJSON_IsBool(validate)) {
        input.has_validate = true;
        input.validate = cJSON_IsTrue(validate);
    } else if (status == WF_OK && validate != NULL) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && cJSON_IsString(swap) && swap->valuestring) {
        input.has_swap_commit = true;
        input.swap_commit = swap->valuestring;
    } else if (status == WF_OK && swap != NULL) {
        status = WF_ERR_PARSE;
    }

    if (status == WF_OK && cJSON_IsArray(arr)) {
        size_t n = (size_t)cJSON_GetArraySize(arr);
        if (n > 0) {
            writes = calloc(n, sizeof(*writes));
            if (!writes) {
                status = WF_ERR_ALLOC;
            }
            for (size_t i = 0; i < n && status == WF_OK; ++i) {
                cJSON *w = cJSON_GetArrayItem(arr, (int)i);
                if (!cJSON_IsObject(w)) {
                    status = WF_ERR_PARSE;
                    break;
                }
                char *s = cJSON_PrintUnformatted(w);
                if (!s) {
                    status = WF_ERR_ALLOC;
                    break;
                }
                writes[i].data = s;
                writes[i].length = strlen(s);
            }
            if (status == WF_OK) {
                input.writes.items = writes;
                input.writes.count = n;
            } else {
                for (size_t i = 0; i < n; ++i) {
                    free((void *)writes[i].data);
                }
                free(writes);
                writes = NULL;
            }
        }
    } else if (status == WF_OK && arr != NULL) {
        status = WF_ERR_PARSE;
    }

    wf_repo_apply_writes_result result = {0};
    if (status == WF_OK) {
        wf_agent_sync_auth(agent);
        wf_response res = {0};
        status = wf_lex_com_atproto_repo_apply_writes_main_call(
            agent->client, &input, &res);
        if (status == WF_OK) {
            status = wf_repo_parse_apply_writes(res.body, res.body_len,
                                                &result);
        }
        wf_response_free(&res);
    }

    if (writes) {
        for (size_t i = 0; i < input.writes.count; ++i) {
            free((void *)writes[i].data);
        }
        free(writes);
    }
    cJSON_Delete(root);

    if (status == WF_OK) {
        *out = result;
    } else {
        wf_repo_apply_writes_result_free(&result);
    }
    return status;
}

/* ---- write record result (createRecord / putRecord) ---- */

static void wf_repo_write_record_result_reset(wf_repo_write_record_result *r) {
    if (!r) {
        return;
    }
    free(r->uri);
    free(r->cid);
    if (r->extra) {
        cJSON_Delete(r->extra);
    }
    memset(r, 0, sizeof(*r));
}

wf_status wf_repo_parse_write_record_result(const char *json, size_t json_len,
                                            wf_repo_write_record_result *out) {
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
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(root, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_repo_set_string(&out->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_repo_set_string(&out->cid, cid->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "uri");
        cJSON_DetachItemFromObject(root, "cid");
        out->extra = root;
    } else {
        wf_repo_write_record_result_reset(out);
        cJSON_Delete(root);
    }
    return status;
}

void wf_repo_write_record_result_free(wf_repo_write_record_result *r) {
    if (!r) {
        return;
    }
    wf_repo_write_record_result_reset(r);
}

/* ---- upload blob result ---- */

static void wf_repo_upload_blob_result_reset(wf_repo_upload_blob_result *r) {
    if (!r) {
        return;
    }
    free(r->cid);
    free(r->mime_type);
    memset(r, 0, sizeof(*r));
}

wf_status wf_repo_parse_upload_blob_result(const char *json, size_t json_len,
                                           wf_repo_upload_blob_result *out) {
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
    cJSON *blob = cJSON_GetObjectItemCaseSensitive(root, "blob");
    if (!blob || !cJSON_IsObject(blob)) {
        wf_repo_upload_blob_result_reset(out);
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    /* A real PDS returns {"$type":"blob","ref":{"$link":"<cid>"},...}; read
     * the CID from ref.$link rather than the legacy top-level blob.cid. */
    cJSON *ref = cJSON_GetObjectItemCaseSensitive(blob, "ref");
    cJSON *cid = NULL;
    if (ref && cJSON_IsObject(ref)) {
        cid = cJSON_GetObjectItemCaseSensitive(ref, "$link");
    }
    cJSON *mime = cJSON_GetObjectItemCaseSensitive(blob, "mimeType");
    cJSON *size = cJSON_GetObjectItemCaseSensitive(blob, "size");
    if (cJSON_IsString(cid) && cid->valuestring) {
        status = wf_repo_set_string(&out->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(mime) && mime->valuestring) {
        status = wf_repo_set_string(&out->mime_type, mime->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(size)) {
        out->has_size = true;
        out->size = (int64_t)size->valuedouble;
    }

    if (status != WF_OK) {
        wf_repo_upload_blob_result_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_repo_upload_blob_result_free(wf_repo_upload_blob_result *r) {
    if (!r) {
        return;
    }
    wf_repo_upload_blob_result_reset(r);
}

/* ---- createRecord / putRecord / deleteRecord / uploadBlob / importRepo ---- */

wf_status wf_agent_create_record_typed(wf_agent *agent, const char *repo,
                                       const char *collection,
                                       const char *rkey_or_null, int validate,
                                       const char *record_json,
                                       const char *swap_commit_or_null,
                                       wf_repo_write_record_result *out) {
    if (!agent || !agent->client || !repo || !repo[0] || !collection ||
        !collection[0] || !record_json || !record_json[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_repo_write_record_result result = {0};
    wf_lex_com_atproto_repo_create_record_main_input input = {0};
    input.repo = repo;
    input.collection = collection;
    if (rkey_or_null && rkey_or_null[0]) {
        input.has_rkey = true;
        input.rkey = rkey_or_null;
    }
    if (validate >= 0) {
        input.has_validate = true;
        input.validate = (validate != 0);
    }
    input.record.data = record_json;
    input.record.length = strlen(record_json);
    if (swap_commit_or_null && swap_commit_or_null[0]) {
        input.has_swap_commit = true;
        input.swap_commit = swap_commit_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_repo_create_record_main_call(
        agent->client, &input, &res);
    if (status == WF_OK) {
        status = wf_repo_parse_write_record_result(res.body, res.body_len,
                                                   &result);
    }
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    } else {
        wf_repo_write_record_result_free(&result);
    }
    return status;
}

wf_status wf_agent_put_record_typed(wf_agent *agent, const char *repo,
                                    const char *collection, const char *rkey,
                                    int validate, const char *record_json,
                                    const char *swap_record_or_null,
                                    const char *swap_commit_or_null,
                                    wf_repo_write_record_result *out) {
    if (!agent || !agent->client || !repo || !repo[0] || !collection ||
        !collection[0] || !rkey || !rkey[0] || !record_json ||
        !record_json[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_repo_write_record_result result = {0};
    wf_lex_com_atproto_repo_put_record_main_input input = {0};
    input.repo = repo;
    input.collection = collection;
    input.rkey = rkey;
    if (validate >= 0) {
        input.has_validate = true;
        input.validate = (validate != 0);
    }
    input.record.data = record_json;
    input.record.length = strlen(record_json);
    if (swap_record_or_null && swap_record_or_null[0]) {
        input.has_swap_record = true;
        input.swap_record = swap_record_or_null;
    }
    if (swap_commit_or_null && swap_commit_or_null[0]) {
        input.has_swap_commit = true;
        input.swap_commit = swap_commit_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_repo_put_record_main_call(
        agent->client, &input, &res);
    if (status == WF_OK) {
        status = wf_repo_parse_write_record_result(res.body, res.body_len,
                                                   &result);
    }
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    } else {
        wf_repo_write_record_result_free(&result);
    }
    return status;
}

wf_status wf_agent_delete_record_typed(wf_agent *agent, const char *repo,
                                       const char *collection, const char *rkey,
                                       const char *swap_record_or_null,
                                       const char *swap_commit_or_null) {
    if (!agent || !agent->client || !repo || !repo[0] || !collection ||
        !collection[0] || !rkey || !rkey[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_repo_delete_record_main_input input = {0};
    input.repo = repo;
    input.collection = collection;
    input.rkey = rkey;
    if (swap_record_or_null && swap_record_or_null[0]) {
        input.has_swap_record = true;
        input.swap_record = swap_record_or_null;
    }
    if (swap_commit_or_null && swap_commit_or_null[0]) {
        input.has_swap_commit = true;
        input.swap_commit = swap_commit_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_repo_delete_record_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_upload_blob_typed(wf_agent *agent, const void *data,
                                     size_t data_len, const char *content_type,
                                     wf_repo_upload_blob_result *out) {
    if (!agent || !agent->client || !data || data_len == 0 || !content_type ||
        !content_type[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_repo_upload_blob_result result = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_upload_blob(
        agent->client, "com.atproto.repo.uploadBlob", data, data_len,
        content_type, &res);
    if (status == WF_OK) {
        status = wf_repo_parse_upload_blob_result(res.body, res.body_len,
                                                  &result);
    }
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    } else {
        wf_repo_upload_blob_result_free(&result);
    }
    return status;
}

wf_status wf_agent_import_repo_typed(wf_agent *agent, const void *car,
                                     size_t car_len) {
    if (!agent || !agent->client || !car || car_len == 0) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_upload_blob(
        agent->client, "com.atproto.repo.importRepo", car, car_len,
        "application/vnd.ipld.car", &res);
    wf_response_free(&res);
    return status;
}
