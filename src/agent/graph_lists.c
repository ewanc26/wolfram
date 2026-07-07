/*
 * graph_lists.c — list management and moderation list helpers.
 *
 * Implements list CRUD, list item management, and mute/block wrappers for
 * app.bsky.graph.list / listitem / listblock plus muteActorList/unmuteActorList.
 */

#include "wolfram/agent.h"
#include "wolfram/syntax.h"
#include "_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WF_AGENT_GRAPH_LIST_COLLECTION        "app.bsky.graph.list"
#define WF_AGENT_GRAPH_LIST_ITEM_COLLECTION   "app.bsky.graph.listitem"
#define WF_AGENT_GRAPH_LIST_BLOCK_COLLECTION  "app.bsky.graph.listblock"
#define WF_AGENT_GRAPH_LIST_RECORD_TYPE       "app.bsky.graph.list"
#define WF_AGENT_GRAPH_LIST_ITEM_RECORD_TYPE  "app.bsky.graph.listitem"
#define WF_AGENT_GRAPH_LIST_BLOCK_RECORD_TYPE "app.bsky.graph.listblock"
#define WF_AGENT_GRAPH_MUTE_LIST_NSID         "app.bsky.graph.muteActorList"
#define WF_AGENT_GRAPH_UNMUTE_LIST_NSID       "app.bsky.graph.unmuteActorList"

static int wf_graph_make_rfc3339_timestamp(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm *tmp = gmtime(&now);
    if (!tmp) {
        return 0;
    }

    tm_utc = *tmp;
    return strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 0;
}

static int wf_graph_list_purpose_is_valid(const char *purpose) {
    if (!purpose || !purpose[0]) {
        return 0;
    }

    return strcmp(purpose, "app.bsky.graph.defs#modlist") == 0 ||
           strcmp(purpose, "app.bsky.graph.defs#curatelist") == 0 ||
           strcmp(purpose, "app.bsky.graph.defs#referencelist") == 0;
}

static wf_status wf_graph_set_item(cJSON *object, const char *key, cJSON *item) {
    if (!object || !key || !item) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON_DeleteItemFromObjectCaseSensitive(object, key);
    if (!cJSON_AddItemToObject(object, key, item)) {
        cJSON_Delete(item);
        return WF_ERR_ALLOC;
    }

    return WF_OK;
}

static wf_status wf_graph_set_string_item(cJSON *object, const char *key,
                                          const char *value) {
    cJSON *item = cJSON_CreateString(value);
    if (!item) {
        return WF_ERR_ALLOC;
    }

    return wf_graph_set_item(object, key, item);
}

static wf_status wf_graph_set_blob_item(cJSON *object, const char *key,
                                        const char *cid) {
    if (!cid || !cid[0]) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *blob = cJSON_CreateObject();
    if (!blob) {
        return WF_ERR_ALLOC;
    }

    cJSON *ref = cJSON_CreateObject();
    if (!ref) {
        cJSON_Delete(blob);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(ref, "$link", cid) ||
        !cJSON_AddItemToObject(blob, "ref", ref) ||
        !cJSON_AddStringToObject(blob, "$type", "blob")) {
        cJSON_Delete(ref);
        cJSON_Delete(blob);
        return WF_ERR_ALLOC;
    }

    return wf_graph_set_item(object, key, blob);
}

static wf_status wf_graph_set_array_item(cJSON *object, const char *key,
                                         const char *json) {
    cJSON *array = cJSON_Parse(json);
    if (!array) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsArray(array)) {
        cJSON_Delete(array);
        return WF_ERR_INVALID_ARG;
    }

    return wf_graph_set_item(object, key, array);
}

static wf_status wf_graph_print_and_create_record(wf_agent *agent,
                                                  const char *collection,
                                                  cJSON *record,
                                                  wf_agent_post_result *out) {
    if (!record) {
        return WF_ERR_INVALID_ARG;
    }

    char *json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_status status = wf_agent_create_record(agent, collection, json, out);
    free(json);
    return status;
}

static wf_status wf_graph_print_and_put_record(wf_agent *agent,
                                               const char *collection,
                                               const char *rkey,
                                               cJSON *record,
                                               wf_agent_post_result *out) {
    if (!record) {
        return WF_ERR_INVALID_ARG;
    }

    char *json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_status status = wf_agent_put_record(agent, collection, rkey, json, out);
    free(json);
    return status;
}

static wf_status wf_graph_delete_record(wf_agent *agent, const char *collection,
                                        const char *rkey) {
    if (!wf_agent_is_logged_in(agent) || !collection || !rkey) {
        return WF_ERR_INVALID_ARG;
    }

    if (wf_syntax_nsid_validate(collection) != WF_OK ||
        !wf_syntax_record_key_is_valid(rkey)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "repo", wf_agent_get_did(agent)) ||
        !cJSON_AddStringToObject(root, "collection", collection) ||
        !cJSON_AddStringToObject(root, "rkey", rkey)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(agent->client,
                                         "com.atproto.repo.deleteRecord",
                                         json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

static wf_status wf_graph_validate_list_uri(wf_agent *agent, const char *list_uri,
                                            const char *expected_collection,
                                            int require_local_repo,
                                            wf_syntax_aturi *parsed) {
    if (!agent || !list_uri || !expected_collection || !parsed) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_aturi_parse(list_uri, parsed)) {
        return WF_ERR_PARSE;
    }

    if (!parsed->authority || !parsed->collection || !parsed->record_key ||
        strcmp(parsed->collection, expected_collection) != 0) {
        wf_syntax_aturi_free(parsed);
        return WF_ERR_INVALID_ARG;
    }

    if (require_local_repo) {
        const char *did = wf_agent_get_did(agent);
        if (!did || strcmp(parsed->authority, did) != 0) {
            wf_syntax_aturi_free(parsed);
            return WF_ERR_INVALID_ARG;
        }
    }

    return WF_OK;
}

static wf_status wf_graph_validate_subject_did(const char *subject_did) {
    if (!subject_did || !subject_did[0] || !wf_syntax_did_is_valid(subject_did)) {
        return WF_ERR_INVALID_ARG;
    }
    return WF_OK;
}

static wf_status wf_graph_extract_blocked_uri(const char *json, size_t json_len,
                                              char **out_blocked_uri) {
    if (!json || !out_blocked_uri) {
        return WF_ERR_INVALID_ARG;
    }

    *out_blocked_uri = NULL;

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_ERR_PARSE;
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
    if (cJSON_IsObject(list)) {
        cJSON *viewer = cJSON_GetObjectItemCaseSensitive(list, "viewer");
        if (cJSON_IsObject(viewer)) {
            cJSON *blocked = cJSON_GetObjectItemCaseSensitive(viewer, "blocked");
            if (cJSON_IsString(blocked) && blocked->valuestring) {
                char *copy = strdup(blocked->valuestring);
                if (!copy) {
                    status = WF_ERR_ALLOC;
                } else {
                    *out_blocked_uri = copy;
                    status = WF_OK;
                }
            } else {
                status = WF_OK;
            }
        } else {
            status = WF_OK;
        }
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_create_list(wf_agent *agent,
                              const wf_agent_create_list_params *params,
                              wf_agent_post_result *out) {
    if (!agent || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!params->name || !params->name[0] ||
        !wf_graph_list_purpose_is_valid(params->purpose)) {
        return WF_ERR_INVALID_ARG;
    }

    char created_at[32];
    if (!wf_graph_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(record, "$type", WF_AGENT_GRAPH_LIST_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at) ||
        !cJSON_AddStringToObject(record, "purpose", params->purpose) ||
        !cJSON_AddStringToObject(record, "name", params->name)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (params->description &&
        !cJSON_AddStringToObject(record, "description", params->description)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (params->description_facets_json) {
        wf_status status = wf_graph_set_array_item(record, "descriptionFacets",
                                                   params->description_facets_json);
        if (status != WF_OK) {
            cJSON_Delete(record);
            return status;
        }
    }

    if (params->avatar_cid) {
        wf_status status = wf_graph_set_blob_item(record, "avatar", params->avatar_cid);
        if (status != WF_OK) {
            cJSON_Delete(record);
            return status;
        }
    }

    return wf_graph_print_and_create_record(agent, WF_AGENT_GRAPH_LIST_COLLECTION,
                                            record, out);
}

wf_status wf_agent_update_list(wf_agent *agent,
                              const wf_agent_update_list_params *params) {
    if (!agent || !params || !params->list_uri || !params->name || !params->name[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, params->list_uri,
                                                  WF_AGENT_GRAPH_LIST_COLLECTION,
                                                  1, &parsed);
    if (status != WF_OK) {
        return status;
    }

    wf_response res = {0};
    status = wf_agent_get_record(agent, WF_AGENT_GRAPH_LIST_COLLECTION,
                                 parsed.record_key, &res);
    if (status != WF_OK) {
        wf_syntax_aturi_free(&parsed);
        wf_response_free(&res);
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        wf_syntax_aturi_free(&parsed);
        return WF_ERR_PARSE;
    }

    cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (!cJSON_IsObject(value)) {
        cJSON_Delete(root);
        wf_syntax_aturi_free(&parsed);
        return WF_ERR_PARSE;
    }

    cJSON *record = cJSON_Duplicate(value, 1);
    cJSON_Delete(root);
    if (!record) {
        wf_syntax_aturi_free(&parsed);
        return WF_ERR_ALLOC;
    }

    status = wf_graph_set_string_item(record, "$type", WF_AGENT_GRAPH_LIST_RECORD_TYPE);
    if (status != WF_OK) {
        cJSON_Delete(record);
        wf_syntax_aturi_free(&parsed);
        return status;
    }

    status = wf_graph_set_string_item(record, "name", params->name);
    if (status != WF_OK) {
        cJSON_Delete(record);
        wf_syntax_aturi_free(&parsed);
        return status;
    }

    if (params->description) {
        status = wf_graph_set_string_item(record, "description", params->description);
        if (status != WF_OK) {
            cJSON_Delete(record);
            wf_syntax_aturi_free(&parsed);
            return status;
        }
    }

    if (params->description_facets_json) {
        status = wf_graph_set_array_item(record, "descriptionFacets",
                                         params->description_facets_json);
        if (status != WF_OK) {
            cJSON_Delete(record);
            wf_syntax_aturi_free(&parsed);
            return status;
        }
    }

    if (params->avatar_cid) {
        status = wf_graph_set_blob_item(record, "avatar", params->avatar_cid);
        if (status != WF_OK) {
            cJSON_Delete(record);
            wf_syntax_aturi_free(&parsed);
            return status;
        }
    }

    wf_agent_post_result result = {0};
    status = wf_graph_print_and_put_record(agent, WF_AGENT_GRAPH_LIST_COLLECTION,
                                           parsed.record_key, record, &result);
    wf_agent_post_result_free(&result);
    wf_syntax_aturi_free(&parsed);
    return status;
}

wf_status wf_agent_delete_list(wf_agent *agent, const char *list_uri) {
    if (!agent || !list_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, list_uri,
                                                  WF_AGENT_GRAPH_LIST_COLLECTION,
                                                  1, &parsed);
    if (status != WF_OK) {
        return status;
    }

    status = wf_graph_delete_record(agent, WF_AGENT_GRAPH_LIST_COLLECTION,
                                    parsed.record_key);
    wf_syntax_aturi_free(&parsed);
    return status;
}

wf_status wf_agent_add_list_item(wf_agent *agent,
                                 const char *list_uri,
                                 const char *subject_did,
                                 wf_agent_post_result *out) {
    if (!agent || !list_uri || !subject_did || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (wf_graph_validate_subject_did(subject_did) != WF_OK) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, list_uri,
                                                  WF_AGENT_GRAPH_LIST_COLLECTION,
                                                  1, &parsed);
    if (status != WF_OK) {
        return status;
    }

    char created_at[32];
    if (!wf_graph_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        wf_syntax_aturi_free(&parsed);
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        wf_syntax_aturi_free(&parsed);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(record, "$type", WF_AGENT_GRAPH_LIST_ITEM_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at) ||
        !cJSON_AddStringToObject(record, "subject", subject_did) ||
        !cJSON_AddStringToObject(record, "list", list_uri)) {
        cJSON_Delete(record);
        wf_syntax_aturi_free(&parsed);
        return WF_ERR_ALLOC;
    }

    status = wf_graph_print_and_create_record(agent, WF_AGENT_GRAPH_LIST_ITEM_COLLECTION,
                                              record, out);
    wf_syntax_aturi_free(&parsed);
    return status;
}

wf_status wf_agent_remove_list_item(wf_agent *agent,
                                    const char *list_item_uri) {
    if (!agent || !list_item_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, list_item_uri,
                                                  WF_AGENT_GRAPH_LIST_ITEM_COLLECTION,
                                                  1, &parsed);
    if (status != WF_OK) {
        return status;
    }

    status = wf_graph_delete_record(agent, WF_AGENT_GRAPH_LIST_ITEM_COLLECTION,
                                    parsed.record_key);
    wf_syntax_aturi_free(&parsed);
    return status;
}

wf_status wf_agent_mute_mod_list(wf_agent *agent, const char *list_uri) {
    if (!agent || !list_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, list_uri,
                                                  WF_AGENT_GRAPH_LIST_COLLECTION,
                                                  0, &parsed);
    if (status != WF_OK) {
        return status;
    }
    wf_syntax_aturi_free(&parsed);

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "list", list_uri)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    status = wf_xrpc_procedure(agent->client, WF_AGENT_GRAPH_MUTE_LIST_NSID,
                               json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_unmute_mod_list(wf_agent *agent, const char *list_uri) {
    if (!agent || !list_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, list_uri,
                                                  WF_AGENT_GRAPH_LIST_COLLECTION,
                                                  0, &parsed);
    if (status != WF_OK) {
        return status;
    }
    wf_syntax_aturi_free(&parsed);

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "list", list_uri)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    status = wf_xrpc_procedure(agent->client, WF_AGENT_GRAPH_UNMUTE_LIST_NSID,
                               json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_block_mod_list(wf_agent *agent, const char *list_uri,
                                  wf_agent_post_result *out) {
    if (!agent || !list_uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, list_uri,
                                                  WF_AGENT_GRAPH_LIST_COLLECTION,
                                                  0, &parsed);
    if (status != WF_OK) {
        return status;
    }
    wf_syntax_aturi_free(&parsed);

    char created_at[32];
    if (!wf_graph_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(record, "$type", WF_AGENT_GRAPH_LIST_BLOCK_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at) ||
        !cJSON_AddStringToObject(record, "subject", list_uri)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    return wf_graph_print_and_create_record(agent, WF_AGENT_GRAPH_LIST_BLOCK_COLLECTION,
                                            record, out);
}

wf_status wf_agent_unblock_mod_list(wf_agent *agent, const char *list_uri) {
    if (!agent || !list_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_validate_list_uri(agent, list_uri,
                                                  WF_AGENT_GRAPH_LIST_COLLECTION,
                                                  0, &parsed);
    if (status != WF_OK) {
        return status;
    }
    wf_syntax_aturi_free(&parsed);

    wf_response res = {0};
    status = wf_agent_get_list(agent, list_uri, 1, NULL, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    char *blocked_uri = NULL;
    status = wf_graph_extract_blocked_uri(res.body, res.body_len, &blocked_uri);
    wf_response_free(&res);
    if (status != WF_OK) {
        free(blocked_uri);
        return status;
    }
    if (!blocked_uri) {
        return WF_OK;
    }

    wf_syntax_aturi block_uri = {0};
    status = wf_graph_validate_list_uri(agent, blocked_uri,
                                        WF_AGENT_GRAPH_LIST_BLOCK_COLLECTION,
                                        1, &block_uri);
    free(blocked_uri);
    if (status != WF_OK) {
        return status;
    }

    status = wf_graph_delete_record(agent, WF_AGENT_GRAPH_LIST_BLOCK_COLLECTION,
                                    block_uri.record_key);
    wf_syntax_aturi_free(&block_uri);
    return status;
}
