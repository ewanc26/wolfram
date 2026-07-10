/*
 * sync_list_typed.c — owned typed parsers + agent convenience wrappers for the
 * com.atproto.sync LIST / HOST / CRAWL endpoints. See
 * include/wolfram/sync_list_typed.h for the public API, the authoritative wire
 * format, and ownership rules.
 *
 * Mirrors labeler_typed.c: static strdup/set_string/reset helpers, owned
 * strings, full cleanup on the first error. The agent wrappers call the
 * generated lex wrappers directly after syncing auth via wf_agent_sync_auth.
 * The binary CAR wrappers (getRepo/getCheckout) copy the raw response body out
 * of the wf_response and never parse the CAR (that is sync.h's job).
 */

#include "wolfram/sync_list_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_sync_strdup(const char *s) {
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

static wf_status wf_sync_set_string(char **dst, const char *src) {
    char *copy = wf_sync_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- repo ref (listRepos) ---- */

static void wf_sync_repo_ref_reset(wf_sync_repo_ref *r) {
    if (!r) {
        return;
    }
    free(r->did);
    free(r->head);
    free(r->rev);
    free(r->status);
    memset(r, 0, sizeof(*r));
}

static wf_status wf_sync_parse_repo_ref(cJSON *obj, wf_sync_repo_ref *r) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *head = cJSON_GetObjectItemCaseSensitive(obj, "head");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(obj, "rev");
    cJSON *active = cJSON_GetObjectItemCaseSensitive(obj, "active");
    cJSON *status_node = cJSON_GetObjectItemCaseSensitive(obj, "status");

    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_sync_set_string(&r->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(head) && head->valuestring) {
        status = wf_sync_set_string(&r->head, head->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring) {
        status = wf_sync_set_string(&r->rev, rev->valuestring);
    }
    if (status == WF_OK && cJSON_IsBool(active)) {
        r->has_active = true;
        r->active = cJSON_IsTrue(active);
    }
    if (status == WF_OK && cJSON_IsString(status_node) && status_node->valuestring) {
        r->has_status = true;
        status = wf_sync_set_string(&r->status, status_node->valuestring);
    }
    return status;
}

/* ---- repo by collection (listReposByCollection) ---- */

static void wf_sync_repo_by_collection_reset(wf_sync_repo_by_collection *r) {
    if (!r) {
        return;
    }
    free(r->did);
    memset(r, 0, sizeof(*r));
}

static wf_status wf_sync_parse_repo_by_collection(cJSON *obj,
                                                 wf_sync_repo_by_collection *r) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_sync_set_string(&r->did, did->valuestring);
    }
    return status;
}

/* ---- host (listHosts / getHostStatus) ---- */

static void wf_sync_host_reset(wf_sync_host *h) {
    if (!h) {
        return;
    }
    free(h->hostname);
    free(h->status);
    memset(h, 0, sizeof(*h));
}

static wf_status wf_sync_parse_host_obj(cJSON *obj, wf_sync_host *h) {
    wf_status status = WF_OK;
    cJSON *hostname = cJSON_GetObjectItemCaseSensitive(obj, "hostname");
    cJSON *seq = cJSON_GetObjectItemCaseSensitive(obj, "seq");
    cJSON *account_count = cJSON_GetObjectItemCaseSensitive(obj, "accountCount");
    cJSON *status_node = cJSON_GetObjectItemCaseSensitive(obj, "status");

    if (cJSON_IsString(hostname) && hostname->valuestring) {
        status = wf_sync_set_string(&h->hostname, hostname->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(seq)) {
        h->has_seq = true;
        h->seq = (int64_t)seq->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(account_count)) {
        h->has_account_count = true;
        h->account_count = (int64_t)account_count->valuedouble;
    }
    if (status == WF_OK && cJSON_IsString(status_node) && status_node->valuestring) {
        h->has_status = true;
        status = wf_sync_set_string(&h->status, status_node->valuestring);
    }
    return status;
}

/* ---- generic string array into owned char** ---- */

static wf_status wf_sync_parse_string_array(cJSON *arr, char ***out_items,
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
        status = wf_sync_set_string(&items[i], it->valuestring);
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

/* ---- top-level parse functions ---- */

wf_status wf_sync_parse_repo_list(const char *json, size_t json_len,
                                  wf_sync_repo_ref_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "repos");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_sync_repo_ref *items = NULL;
    if (count > 0) {
        items = (wf_sync_repo_ref *)calloc(count, sizeof(*items));
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
        status = wf_sync_parse_repo_ref(obj, &items[i]);
        if (status != WF_OK) {
            wf_sync_repo_ref_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_sync_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_sync_repo_ref_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_sync_parse_repo_by_collection_list(
    const char *json, size_t json_len, wf_sync_repo_by_collection_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "repos");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_sync_repo_by_collection *items = NULL;
    if (count > 0) {
        items = (wf_sync_repo_by_collection *)calloc(count, sizeof(*items));
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
        status = wf_sync_parse_repo_by_collection(obj, &items[i]);
        if (status != WF_OK) {
            wf_sync_repo_by_collection_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_sync_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_sync_repo_by_collection_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_sync_parse_blob_cid_list(const char *json, size_t json_len,
                                      wf_sync_blob_cid_list *out) {
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

    wf_status status = wf_sync_parse_string_array(
        cJSON_GetObjectItemCaseSensitive(root, "cids"), &out->cids, &out->count);
    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_sync_set_string(&out->cursor, cursor->valuestring);
        }
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < out->count; ++i) {
            free(out->cids[i]);
        }
        free(out->cids);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_sync_parse_host_list(const char *json, size_t json_len,
                                  wf_sync_host_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "hosts");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_sync_host *items = NULL;
    if (count > 0) {
        items = (wf_sync_host *)calloc(count, sizeof(*items));
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
        status = wf_sync_parse_host_obj(obj, &items[i]);
        if (status != WF_OK) {
            wf_sync_host_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_sync_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_sync_host_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_sync_parse_host(const char *json, size_t json_len,
                             wf_sync_host *out) {
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

    wf_status status = wf_sync_parse_host_obj(root, out);
    if (status != WF_OK) {
        wf_sync_host_reset(out);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ---- free functions ---- */

void wf_sync_repo_ref_list_free(wf_sync_repo_ref_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->count; ++i) {
        wf_sync_repo_ref_reset(&l->items[i]);
    }
    free(l->items);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

void wf_sync_repo_by_collection_list_free(wf_sync_repo_by_collection_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->count; ++i) {
        wf_sync_repo_by_collection_reset(&l->items[i]);
    }
    free(l->items);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

void wf_sync_blob_cid_list_free(wf_sync_blob_cid_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->count; ++i) {
        free(l->cids[i]);
    }
    free(l->cids);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

void wf_sync_host_list_free(wf_sync_host_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->count; ++i) {
        wf_sync_host_reset(&l->items[i]);
    }
    free(l->items);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

void wf_sync_host_free(wf_sync_host *h) {
    if (!h) {
        return;
    }
    wf_sync_host_reset(h);
    memset(h, 0, sizeof(*h));
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_list_repos_typed(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_sync_repo_ref_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_sync_repo_ref_list list = {0};
    wf_lex_com_atproto_sync_list_repos_main_params params = {0};
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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
    wf_status status = wf_lex_com_atproto_sync_list_repos_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_sync_parse_repo_list(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_list_repos_by_collection_typed(
    wf_agent *agent, const char *collection, int limit, const char *cursor,
    wf_sync_repo_by_collection_list *out) {
    if (!agent || !agent->client || !collection || !collection[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_sync_repo_by_collection_list list = {0};
    wf_lex_com_atproto_sync_list_repos_by_collection_main_params params = {0};
    if (limit < 0 || limit > 2000) {
        return WF_ERR_INVALID_ARG;
    }
    params.collection = collection;
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
    wf_status status = wf_lex_com_atproto_sync_list_repos_by_collection_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_sync_parse_repo_by_collection_list(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_list_blobs_typed(wf_agent *agent, const char *did,
                                    const char *since, int limit,
                                    const char *cursor,
                                    wf_sync_blob_cid_list *out) {
    if (!agent || !agent->client || !did || !did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_sync_blob_cid_list list = {0};
    wf_lex_com_atproto_sync_list_blobs_main_params params = {0};
    if (limit < 0 || limit > 1000) {
        return WF_ERR_INVALID_ARG;
    }
    params.did = did;
    if (since && since[0]) {
        params.has_since = true;
        params.since = since;
    }
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
    wf_status status = wf_lex_com_atproto_sync_list_blobs_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_sync_parse_blob_cid_list(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_list_hosts_typed(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_sync_host_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_sync_host_list list = {0};
    wf_lex_com_atproto_sync_list_hosts_main_params params = {0};
    if (limit < 0 || limit > 1000) {
        return WF_ERR_INVALID_ARG;
    }
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
    wf_status status = wf_lex_com_atproto_sync_list_hosts_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_sync_parse_host_list(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_host_status_typed(wf_agent *agent, const char *hostname,
                                         wf_sync_host *out) {
    if (!agent || !agent->client || !hostname || !hostname[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_sync_host host = {0};
    wf_lex_com_atproto_sync_get_host_status_main_params params = {0};
    params.hostname = hostname;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_sync_get_host_status_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_sync_parse_host(res.body, res.body_len, &host);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = host;
    }
    return status;
}

wf_status wf_agent_notify_of_update_typed(wf_agent *agent, const char *hostname) {
    if (!agent || !agent->client || !hostname || !hostname[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_sync_notify_of_update_main_input input = {0};
    input.hostname = hostname;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_sync_notify_of_update_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_request_crawl_typed(wf_agent *agent, const char *hostname) {
    if (!agent || !agent->client || !hostname || !hostname[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_sync_request_crawl_main_input input = {0};
    input.hostname = hostname;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_sync_request_crawl_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_repo_car(wf_agent *agent, const char *did,
                                const char *since_or_null,
                                unsigned char **out_bytes, size_t *out_len) {
    if (!agent || !agent->client || !did || !did[0] || !out_bytes || !out_len) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_sync_get_repo_main_params params = {0};
    params.did = did;
    if (since_or_null && since_or_null[0]) {
        params.has_since = true;
        params.since = since_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_sync_get_repo_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    unsigned char *bytes = NULL;
    if (res.body_len > 0) {
        bytes = (unsigned char *)malloc(res.body_len);
        if (!bytes) {
            wf_response_free(&res);
            return WF_ERR_ALLOC;
        }
        memcpy(bytes, res.body, res.body_len);
    }
    *out_bytes = bytes;
    *out_len = res.body_len;
    wf_response_free(&res);
    return WF_OK;
}

wf_status wf_agent_get_checkout_car(wf_agent *agent, const char *did,
                                    unsigned char **out_bytes,
                                    size_t *out_len) {
    if (!agent || !agent->client || !did || !did[0] || !out_bytes || !out_len) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_sync_get_checkout_main_params params = {0};
    params.did = did;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_sync_get_checkout_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    unsigned char *bytes = NULL;
    if (res.body_len > 0) {
        bytes = (unsigned char *)malloc(res.body_len);
        if (!bytes) {
            wf_response_free(&res);
            return WF_ERR_ALLOC;
        }
        memcpy(bytes, res.body, res.body_len);
    }
    *out_bytes = bytes;
    *out_len = res.body_len;
    wf_response_free(&res);
    return WF_OK;
}
