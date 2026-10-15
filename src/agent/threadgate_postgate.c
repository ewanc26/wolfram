/*
 * threadgate_postgate.c — create/delete reply controls (threadgate)
 * and embedding rules (postgate) records.
 *
 * Pattern follows graph_lists.c: build cJSON records, print to string,
 * and call wf_agent_create_record.
 */

#include "wolfram/agent.h"
#include "wolfram/syntax.h"
#include "_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WF_AGENT_THREADGATE_COLLECTION "app.bsky.feed.threadgate"
#define WF_AGENT_POSTGATE_COLLECTION "app.bsky.feed.postgate"
#define WF_AGENT_THREADGATE_RECORD_TYPE "app.bsky.feed.threadgate"
#define WF_AGENT_POSTGATE_RECORD_TYPE "app.bsky.feed.postgate"
#define WF_AGENT_DELETE_RECORD_NSID "com.atproto.repo.deleteRecord"

static int wf_tpg_make_rfc3339_timestamp(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm *tmp = gmtime(&now);
    if (!tmp) return 0;
    tm_utc = *tmp;
    return strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 0;
}

/*
 * A threadgate or postgate belongs to one post and shares its record key: the
 * AppView links the gate to the post by at://<did>/<collection>/<post rkey>,
 * so a gate written under a fresh TID is never attached to its post. Derive the
 * key from the post's URI and require the post to be in the signed-in repo,
 * since that is the repo the gate is written to.
 */
#define WF_TPG_RKEY_MAX 256

static wf_status wf_tpg_post_rkey(wf_agent *agent, const char *post_uri,
                                  char *rkey, size_t rkey_cap) {
    wf_syntax_aturi parsed = {0};
    wf_status status = WF_OK;

    if (!wf_syntax_aturi_parse(post_uri, &parsed)) return WF_ERR_INVALID_ARG;
    if (!parsed.authority || !parsed.record_key ||
        strcmp(parsed.authority, wf_agent_get_did(agent)) != 0 ||
        strlen(parsed.record_key) >= rkey_cap ||
        !wf_syntax_record_key_is_valid(parsed.record_key)) {
        status = WF_ERR_INVALID_ARG;
    } else {
        strcpy(rkey, parsed.record_key);
    }
    wf_syntax_aturi_free(&parsed);
    return status;
}

static wf_status wf_tpg_print_and_put(wf_agent *agent, const char *collection,
                                      const char *rkey, cJSON *record,
                                      wf_agent_post_result *out) {
    if (!record) return WF_ERR_INVALID_ARG;

    char *json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!json) return WF_ERR_ALLOC;

    wf_status status = wf_agent_put_record(agent, collection, rkey, json, out);
    free(json);
    return status;
}

static wf_status wf_tpg_add_json_array(cJSON *parent, const char *key,
                                       const char *json_str) {
    if (!json_str || !json_str[0]) return WF_OK;

    cJSON *arr = cJSON_Parse(json_str);
    if (!arr) return WF_ERR_PARSE;
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return WF_ERR_INVALID_ARG;
    }
    if (!cJSON_AddItemToObject(parent, key, arr)) {
        cJSON_Delete(arr);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

static wf_status wf_tpg_add_string_array(cJSON *parent, const char *key,
                                         const char *const *items,
                                         size_t count) {
    if (!items || count == 0) return WF_OK;

    cJSON *arr = cJSON_CreateArray();
    if (!arr) return WF_ERR_ALLOC;

    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateString(items[i]);
        if (!item || !cJSON_AddItemToArray(arr, item)) {
            cJSON_Delete(item);
            cJSON_Delete(arr);
            return WF_ERR_ALLOC;
        }
    }
    if (!cJSON_AddItemToObject(parent, key, arr)) {
        cJSON_Delete(arr);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

wf_status wf_agent_create_threadgate(wf_agent *agent, const char *post_uri,
                                     const char *allow_json,
                                     const char **hidden_replies,
                                     size_t hidden_count,
                                     wf_agent_post_result *out) {
    if (!agent || !post_uri || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    char created_at[32];
    if (!wf_tpg_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    char rkey[WF_TPG_RKEY_MAX];
    wf_status status = wf_tpg_post_rkey(agent, post_uri, rkey, sizeof rkey);
    if (status != WF_OK) return status;

    cJSON *record = cJSON_CreateObject();
    if (!record) return WF_ERR_ALLOC;

    if (!cJSON_AddStringToObject(record, "$type",
                                 WF_AGENT_THREADGATE_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "post", post_uri) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    status = wf_tpg_add_json_array(record, "allow", allow_json);
    if (status != WF_OK) {
        cJSON_Delete(record);
        return status;
    }

    status = wf_tpg_add_string_array(record, "hiddenReplies", hidden_replies,
                                     hidden_count);
    if (status != WF_OK) {
        cJSON_Delete(record);
        return status;
    }

    return wf_tpg_print_and_put(agent, WF_AGENT_THREADGATE_COLLECTION, rkey,
                                record, out);
}

wf_status wf_agent_create_postgate(wf_agent *agent, const char *post_uri,
                                   const char *embedding_rules_json,
                                   const char **detached_uris,
                                   size_t detached_count,
                                   wf_agent_post_result *out) {
    if (!agent || !post_uri || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    char created_at[32];
    if (!wf_tpg_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    char rkey[WF_TPG_RKEY_MAX];
    wf_status status = wf_tpg_post_rkey(agent, post_uri, rkey, sizeof rkey);
    if (status != WF_OK) return status;

    cJSON *record = cJSON_CreateObject();
    if (!record) return WF_ERR_ALLOC;

    if (!cJSON_AddStringToObject(record, "$type",
                                 WF_AGENT_POSTGATE_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "post", post_uri) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    status =
        wf_tpg_add_json_array(record, "embeddingRules", embedding_rules_json);
    if (status != WF_OK) {
        cJSON_Delete(record);
        return status;
    }

    status = wf_tpg_add_string_array(record, "detachedEmbeddingUris",
                                     detached_uris, detached_count);
    if (status != WF_OK) {
        cJSON_Delete(record);
        return status;
    }

    return wf_tpg_print_and_put(agent, WF_AGENT_POSTGATE_COLLECTION, rkey,
                                record, out);
}

wf_status wf_agent_delete_record_by_uri(wf_agent *agent,
                                        const char *record_uri) {
    if (!agent || !record_uri) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(record_uri, &parsed)) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    if (!parsed.authority || !parsed.collection || !parsed.record_key ||
        strcmp(parsed.authority, wf_agent_get_did(agent)) != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    if (!cJSON_AddStringToObject(root, "repo", parsed.authority) ||
        !cJSON_AddStringToObject(root, "collection", parsed.collection) ||
        !cJSON_AddStringToObject(root, "rkey", parsed.record_key)) {
        cJSON_Delete(root);
        status = WF_ERR_ALLOC;
        goto done;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    status = wf_xrpc_procedure(agent->client, WF_AGENT_DELETE_RECORD_NSID, json,
                               &res);
    free(json);
    wf_response_free(&res);

done:
    wf_syntax_aturi_free(&parsed);
    return status;
}

wf_status wf_agent_set_reply_gate(wf_agent *agent, const char *post_uri,
                                  wf_reply_gate gate) {
    static const char *const allow_following_mentions =
        "[{\"$type\":\"app.bsky.feed.threadgate#followingRule\"},"
        "{\"$type\":\"app.bsky.feed.threadgate#mentionRule\"}]";
    wf_agent_post_result out = {0};
    wf_status st;

    if (!agent || !post_uri || !post_uri[0]) return WF_ERR_INVALID_ARG;
    switch (gate) {
        case WF_REPLY_GATE_EVERYONE:
            return WF_OK;
        case WF_REPLY_GATE_FOLLOWED_MENTIONED:
            st = wf_agent_create_threadgate(
                agent, post_uri, allow_following_mentions, NULL, 0, &out);
            break;
        case WF_REPLY_GATE_NOBODY:
            st = wf_agent_create_threadgate(agent, post_uri, "[]", NULL, 0,
                                            &out);
            break;
        default:
            return WF_ERR_INVALID_ARG;
    }
    wf_agent_post_result_free(&out);
    return st;
}
