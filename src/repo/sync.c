#include "wolfram/sync.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *wf_sync_strdup(const char *value) {
    size_t len;
    char *copy;

    if (!value) {
        return NULL;
    }

    len = strlen(value);
    copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, len + 1);
    return copy;
}

static void wf_sync_free_strings(char **items, size_t count) {
    size_t i;

    if (!items) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

static void wf_sync_repo_entries_free(wf_sync_repo_entry *repos, size_t count) {
    size_t i;

    if (!repos) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(repos[i].did);
        free(repos[i].rev);
    }
    free(repos);
}

static wf_status wf_sync_json_string(const cJSON *object, const char *name,
                                     int required, char **out) {
    const cJSON *item;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;

    item = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (!item) {
        return required ? WF_ERR_PARSE : WF_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
        return WF_ERR_PARSE;
    }

    *out = wf_sync_strdup(item->valuestring);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status wf_sync_json_bool_string(const cJSON *object, const char *name,
                                          int required, char **out) {
    const cJSON *item;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;

    item = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (!item) {
        return required ? WF_ERR_PARSE : WF_OK;
    }
    if (!cJSON_IsBool(item)) {
        return WF_ERR_PARSE;
    }

    *out = wf_sync_strdup(cJSON_IsTrue(item) ? "true" : "false");
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status wf_sync_json_string_array(const cJSON *object, const char *name,
                                           char ***items_out, size_t *count_out) {
    const cJSON *array;
    const cJSON *item;
    char **items = NULL;
    size_t count;
    size_t index = 0;

    if (!items_out || !count_out) {
        return WF_ERR_INVALID_ARG;
    }
    *items_out = NULL;
    *count_out = 0;

    array = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (!array || !cJSON_IsArray(array)) {
        return WF_ERR_PARSE;
    }

    count = (size_t)cJSON_GetArraySize(array);
    if (count == 0) {
        return WF_OK;
    }

    items = calloc(count, sizeof(*items));
    if (!items) {
        return WF_ERR_ALLOC;
    }

    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
            wf_sync_free_strings(items, index);
            return WF_ERR_PARSE;
        }
        items[index] = wf_sync_strdup(item->valuestring);
        if (!items[index]) {
            wf_sync_free_strings(items, index);
            return WF_ERR_ALLOC;
        }
        index++;
    }

    *items_out = items;
    *count_out = count;
    return WF_OK;
}

static wf_status wf_sync_json_repo_entries(const cJSON *object, const char *name,
                                           wf_sync_repo_entry **repos_out,
                                           size_t *count_out) {
    const cJSON *array;
    const cJSON *item;
    wf_sync_repo_entry *repos = NULL;
    size_t count;
    size_t index = 0;

    if (!repos_out || !count_out) {
        return WF_ERR_INVALID_ARG;
    }
    *repos_out = NULL;
    *count_out = 0;

    array = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (!array || !cJSON_IsArray(array)) {
        return WF_ERR_PARSE;
    }

    count = (size_t)cJSON_GetArraySize(array);
    if (count == 0) {
        return WF_OK;
    }

    repos = calloc(count, sizeof(*repos));
    if (!repos) {
        return WF_ERR_ALLOC;
    }

    cJSON_ArrayForEach(item, array) {
        char *did = NULL;
        char *head = NULL;
        char *rev = NULL;
        wf_status status;

        if (!cJSON_IsObject(item)) {
            wf_sync_repo_entries_free(repos, index);
            return WF_ERR_PARSE;
        }

        status = wf_sync_json_string(item, "did", 1, &did);
        if (status == WF_OK) {
            status = wf_sync_json_string(item, "head", 1, &head);
        }
        if (status == WF_OK) {
            status = wf_sync_json_string(item, "rev", 1, &rev);
        }
        if (status != WF_OK) {
            free(did);
            free(head);
            free(rev);
            wf_sync_repo_entries_free(repos, index);
            return status;
        }

        free(head);
        repos[index].did = did;
        repos[index].rev = rev;
        index++;
    }

    *repos_out = repos;
    *count_out = count;
    return WF_OK;
}

static wf_status wf_sync_car_from_response(const wf_response *response, wf_car *out) {
    if (!response || !out || !response->body || response->body_len == 0) {
        return WF_ERR_PARSE;
    }
    return wf_car_parse((const unsigned char *)response->body, response->body_len, out);
}

static wf_status wf_sync_blob_from_response(const wf_response *response,
                                            unsigned char **out_data,
                                            size_t *out_len) {
    unsigned char *data;

    if (!response || !out_data || !out_len) {
        return WF_ERR_INVALID_ARG;
    }
    *out_data = NULL;
    *out_len = 0;

    if (response->body_len == 0) {
        return WF_OK;
    }
    if (!response->body) {
        return WF_ERR_PARSE;
    }

    data = malloc(response->body_len);
    if (!data) {
        return WF_ERR_ALLOC;
    }

    memcpy(data, response->body, response->body_len);
    *out_data = data;
    *out_len = response->body_len;
    return WF_OK;
}

void wf_sync_blob_list_free(wf_sync_blob_list *list) {
    if (!list) {
        return;
    }

    wf_sync_free_strings(list->cids, list->cid_count);
    free(list->cursor);
    list->cids = NULL;
    list->cid_count = 0;
    list->cursor = NULL;
}

void wf_sync_head_free(wf_sync_head *head) {
    if (!head) {
        return;
    }

    free(head->root);
    free(head->rev);
    head->root = NULL;
    head->rev = NULL;
}

void wf_sync_commit_info_free(wf_sync_commit_info *info) {
    if (!info) {
        return;
    }

    free(info->cid);
    free(info->rev);
    info->cid = NULL;
    info->rev = NULL;
}

void wf_sync_repo_status_free(wf_sync_repo_status *status) {
    if (!status) {
        return;
    }

    free(status->did);
    free(status->active);
    free(status->rev);
    status->did = NULL;
    status->active = NULL;
    status->rev = NULL;
}

void wf_sync_repo_list_free(wf_sync_repo_list *list) {
    if (!list) {
        return;
    }

    wf_sync_repo_entries_free(list->repos, list->repo_count);
    free(list->cursor);
    list->repos = NULL;
    list->repo_count = 0;
    list->cursor = NULL;
}

wf_status wf_sync_get_blob(wf_xrpc_client *client,
                           const char *did, const char *cid,
                           unsigned char **out_data, size_t *out_len) {
    wf_response response = {0};
    wf_xrpc_param params[2];
    wf_status status;

    if (!client || !did || did[0] == '\0' || !cid || cid[0] == '\0' ||
        !out_data || !out_len) {
        return WF_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_len = 0;

    params[0] = (wf_xrpc_param){"did", did};
    params[1] = (wf_xrpc_param){"cid", cid};
    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.getBlob",
                                  params,
                                  2,
                                  &response);
    if (status != WF_OK) {
        return status;
    }

    status = wf_sync_blob_from_response(&response, out_data, out_len);
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_get_blocks(wf_xrpc_client *client,
                             const char *did, const char *const *cids, size_t cid_count,
                             wf_car *out) {
    wf_response response = {0};
    wf_xrpc_param *params = NULL;
    size_t i;
    wf_status status;

    if (!client || !did || did[0] == '\0' || !cids || cid_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (cid_count > SIZE_MAX - 1) {
        return WF_ERR_INVALID_ARG;
    }

    for (i = 0; i < cid_count; i++) {
        if (!cids[i] || cids[i][0] == '\0') {
            return WF_ERR_INVALID_ARG;
        }
    }

    params = calloc(cid_count + 1, sizeof(*params));
    if (!params) {
        return WF_ERR_ALLOC;
    }

    params[0] = (wf_xrpc_param){"did", did};
    for (i = 0; i < cid_count; i++) {
        params[i + 1] = (wf_xrpc_param){"cids", cids[i]};
    }

    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.getBlocks",
                                  params,
                                  cid_count + 1,
                                  &response);
    free(params);
    if (status != WF_OK) {
        return status;
    }

    memset(out, 0, sizeof(*out));
    status = wf_sync_car_from_response(&response, out);
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_get_record(wf_xrpc_client *client,
                             const char *did, const char *collection, const char *rkey,
                             wf_car *out) {
    wf_response response = {0};
    wf_xrpc_param params[3];
    wf_status status;

    if (!client || !did || did[0] == '\0' || !collection || collection[0] == '\0' ||
        !rkey || rkey[0] == '\0' || !out) {
        return WF_ERR_INVALID_ARG;
    }

    params[0] = (wf_xrpc_param){"did", did};
    params[1] = (wf_xrpc_param){"collection", collection};
    params[2] = (wf_xrpc_param){"rkey", rkey};
    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.getRecord",
                                  params,
                                  3,
                                  &response);
    if (status != WF_OK) {
        return status;
    }

    memset(out, 0, sizeof(*out));
    status = wf_sync_car_from_response(&response, out);
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_list_blobs(wf_xrpc_client *client,
                             const char *did, const char *since,
                             int limit, const char *cursor,
                             wf_sync_blob_list *out) {
    wf_response response = {0};
    wf_xrpc_param params[4];
    char limit_buf[32];
    size_t param_count = 0;
    wf_status status;

    if (!client || !did || did[0] == '\0' || !out ||
        (since && since[0] == '\0') || (cursor && cursor[0] == '\0') ||
        limit < 0 || limit > 1000) {
        return WF_ERR_INVALID_ARG;
    }

    params[param_count++] = (wf_xrpc_param){"did", did};
    if (since) {
        params[param_count++] = (wf_xrpc_param){"since", since};
    }
    if (limit > 0) {
        int written = snprintf(limit_buf, sizeof(limit_buf), "%d", limit);
        if (written < 0 || (size_t)written >= sizeof(limit_buf)) {
            return WF_ERR_ALLOC;
        }
        params[param_count++] = (wf_xrpc_param){"limit", limit_buf};
    }
    if (cursor) {
        params[param_count++] = (wf_xrpc_param){"cursor", cursor};
    }

    memset(out, 0, sizeof(*out));
    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.listBlobs",
                                  params,
                                  param_count,
                                  &response);
    if (status != WF_OK) {
        wf_response_free(&response);
        return status;
    }

    {
        cJSON *parsed = cJSON_ParseWithLength(response.body, response.body_len);
        if (!parsed || !cJSON_IsObject(parsed)) {
            cJSON_Delete(parsed);
            wf_response_free(&response);
            return WF_ERR_PARSE;
        }
        status = wf_sync_json_string_array(parsed, "cids", &out->cids, &out->cid_count);
        if (status == WF_OK) {
            status = wf_sync_json_string(parsed, "cursor", 0, &out->cursor);
        }
        cJSON_Delete(parsed);
        if (status != WF_OK) {
            wf_sync_blob_list_free(out);
        }
        wf_response_free(&response);
        return status;
    }
}

wf_status wf_sync_get_head(wf_xrpc_client *client,
                           const char *did, wf_sync_head *out) {
    wf_response response = {0};
    wf_xrpc_param params[1];
    cJSON *root = NULL;
    wf_status status;

    if (!client || !did || did[0] == '\0' || !out) {
        return WF_ERR_INVALID_ARG;
    }

    params[0] = (wf_xrpc_param){"did", did};
    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.getHead",
                                  params,
                                  1,
                                  &response);
    if (status != WF_OK) {
        return status;
    }

    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    status = wf_sync_json_string(root, "root", 1, &out->root);
    if (status == WF_OK) {
        status = wf_sync_json_string(root, "rev", 0, &out->rev);
    }
    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_sync_head_free(out);
    }
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_get_latest_commit(wf_xrpc_client *client,
                                    const char *did, wf_sync_commit_info *out) {
    wf_response response = {0};
    wf_xrpc_param params[1];
    cJSON *root = NULL;
    wf_status status;

    if (!client || !did || did[0] == '\0' || !out) {
        return WF_ERR_INVALID_ARG;
    }

    params[0] = (wf_xrpc_param){"did", did};
    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.getLatestCommit",
                                  params,
                                  1,
                                  &response);
    if (status != WF_OK) {
        return status;
    }

    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    status = wf_sync_json_string(root, "cid", 1, &out->cid);
    if (status == WF_OK) {
        status = wf_sync_json_string(root, "rev", 1, &out->rev);
    }
    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_sync_commit_info_free(out);
    }
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_get_repo_status(wf_xrpc_client *client,
                                  const char *did, wf_sync_repo_status *out) {
    wf_response response = {0};
    wf_xrpc_param params[1];
    cJSON *root = NULL;
    wf_status status;

    if (!client || !did || did[0] == '\0' || !out) {
        return WF_ERR_INVALID_ARG;
    }

    params[0] = (wf_xrpc_param){"did", did};
    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.getRepoStatus",
                                  params,
                                  1,
                                  &response);
    if (status != WF_OK) {
        return status;
    }

    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    status = wf_sync_json_string(root, "did", 1, &out->did);
    if (status == WF_OK) {
        status = wf_sync_json_bool_string(root, "active", 1, &out->active);
    }
    if (status == WF_OK) {
        status = wf_sync_json_string(root, "rev", 0, &out->rev);
    }
    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_sync_repo_status_free(out);
    }
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_list_repos(wf_xrpc_client *client,
                             const char *cursor, int limit,
                             wf_sync_repo_list *out) {
    wf_response response = {0};
    wf_xrpc_param params[2];
    char limit_buf[32];
    size_t param_count = 0;
    wf_status status;
    cJSON *root = NULL;

    if (!client || !out || (cursor && cursor[0] == '\0') ||
        limit < 0 || limit > 1000) {
        return WF_ERR_INVALID_ARG;
    }

    if (limit > 0) {
        int written = snprintf(limit_buf, sizeof(limit_buf), "%d", limit);
        if (written < 0 || (size_t)written >= sizeof(limit_buf)) {
            return WF_ERR_ALLOC;
        }
        params[param_count++] = (wf_xrpc_param){"limit", limit_buf};
    }
    if (cursor) {
        params[param_count++] = (wf_xrpc_param){"cursor", cursor};
    }

    status = wf_xrpc_query_params(client,
                                  "com.atproto.sync.listRepos",
                                  params,
                                  param_count,
                                  &response);
    if (status != WF_OK) {
        return status;
    }

    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    status = wf_sync_json_repo_entries(root, "repos", &out->repos, &out->repo_count);
    if (status == WF_OK) {
        status = wf_sync_json_string(root, "cursor", 0, &out->cursor);
    }
    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_sync_repo_list_free(out);
    }
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_get_repo(wf_xrpc_client *client,
                           const char *did,
                           const char *since,
                           wf_car *out) {
    if (!client || !did || did[0] == '\0' || !out ||
        (since && since[0] == '\0')) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    wf_xrpc_param params[2] = {
        {"did", did},
        {"since", since},
    };
    size_t param_count = since ? 2 : 1;
    wf_response response = {0};
    wf_status status = wf_xrpc_query_params(client,
                                             "com.atproto.sync.getRepo",
                                             params,
                                             param_count,
                                             &response);
    if (status != WF_OK) {
        return status;
    }

    status = wf_car_parse((const unsigned char *)response.body,
                          response.body_len,
                          out);
    wf_response_free(&response);
    return status;
}

wf_status wf_sync_verify_diff_car(const wf_car *base,
                                  const wf_cid *base_commit,
                                  const unsigned char *bytes,
                                  size_t len,
                                  const wf_repo_verify_options *options,
                                  wf_repo_diff *out) {
    if (!base || !base_commit || !bytes || len == 0 || !options || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_car update = {0};
    wf_status status = wf_car_parse(bytes, len, &update);
    if (status != WF_OK) return status;
    status = wf_repo_diff_verify(base, base_commit, &update, options, out);
    wf_car_free(&update);
    return status;
}
