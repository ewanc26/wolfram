#include "wolfram/agent.h"
#include "wolfram/syntax.h"
#include "wolfram/util.h"
#include "wolfram/tid.h"
#include "_internal.h"
#include "wolfram/atproto_lex.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>

/* Internal helper replicating the logic of the original static function */
static wf_status wf_agent_create_record_internal(wf_agent *agent,
                                                const char *collection,
                                                cJSON *record,
                                                wf_agent_post_result *out) {
    if (!wf_agent_is_logged_in(agent) || !collection || !record || !out) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    if (wf_syntax_nsid_validate(collection) != WF_OK) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "repo", agent->session->data.did) ||
        !cJSON_AddStringToObject(root, "collection", collection)) {
        cJSON_Delete(record);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddItemToObject(root, "record", record)) {
        cJSON_Delete(record);
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
    wf_status status = wf_xrpc_procedure(agent->client, WF_LEX_COM_ATPROTO_REPO_CREATE_RECORD_NSID,
                                          json, &res);
    free(json);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    out->uri = NULL; out->cid = NULL;

    cJSON *resp_root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!resp_root) {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(resp_root, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(resp_root, "cid");
    if (!cJSON_IsString(uri) || !cJSON_IsString(cid) ||
        !uri->valuestring || !cid->valuestring) {
        cJSON_Delete(resp_root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    out->uri = strdup(uri->valuestring);
    out->cid = strdup(cid->valuestring);
    if (!out->uri || !out->cid) {
        free(out->uri);
        free(out->cid);
        out->uri = NULL;
        out->cid = NULL;
        status = WF_ERR_ALLOC;
    } else {
        status = WF_OK;
    }

    cJSON_Delete(resp_root);
    wf_response_free(&res);
    return status;
}

/* Public wrapper – parses JSON string and delegates to internal helper */
wf_status wf_agent_create_record(wf_agent *agent, const char *collection,
                               const char *record_json, wf_agent_post_result *out) {
    if (!agent || !collection || !record_json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    cJSON *record = cJSON_Parse(record_json);
    if (!record) {
        return WF_ERR_PARSE;
    }
    wf_status status = wf_agent_create_record_internal(agent, collection, record, out);
    if (status != WF_OK) {
        cJSON_Delete(record);
    }
    return status;
}

/*
 * Mint a fresh monotonic TID and use it as the record key via the
 * putRecord path. Keeps wf_agent_create_record (server-assigned key) and
 * wf_agent_put_record (caller-supplied key) unchanged.
 */
wf_status wf_agent_create_record_with_tid(wf_agent *agent,
                                         const char *collection,
                                         const char *record_json,
                                         wf_agent_post_result *out) {
    if (!agent || !collection || !record_json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    char rkey[15];
    wf_status status = wf_tid_now(rkey);
    if (status != WF_OK) return status;
    return wf_agent_put_record(agent, collection, rkey, record_json, out);
}
