#include "wolfram/blob.h"
#include "wolfram/util.h"
#include "wolfram/atproto_lex.h"
#include "agent/_internal.h" // for internal agent fields
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

#define WF_UPLOAD_VIDEO_NSID "app.bsky.video.uploadVideo"

wf_status wf_agent_upload_blob_ex(wf_agent *agent, const void *data, size_t data_len,
                                 const char *content_type, wf_uploaded_blob *out) {
    if (!agent || !data || data_len == 0 || !content_type || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response resp = {0};
    wf_status status = wf_agent_upload_blob(agent, data, data_len, content_type, &resp);
    if (status != WF_OK) {
        return status;
    }

    // Parse response JSON
    cJSON *root = cJSON_ParseWithLength(resp.body, resp.body_len);
    if (!root) {
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }

    cJSON *blob_obj = cJSON_GetObjectItem(root, "blob");
    if (!blob_obj || !cJSON_IsObject(blob_obj)) {
        cJSON_Delete(root);
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }

    cJSON *type = cJSON_GetObjectItem(blob_obj, "$type");
    if (!type || !cJSON_IsString(type) || !wf_ascii_iequals(type->valuestring, "blob")) {
        cJSON_Delete(root);
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }

    cJSON *ref = cJSON_GetObjectItem(blob_obj, "ref");
    cJSON *link = NULL;
    if (ref && cJSON_IsObject(ref)) {
        link = cJSON_GetObjectItem(ref, "$link");
    }
    cJSON *mime = cJSON_GetObjectItem(blob_obj, "mimeType");
    cJSON *size = cJSON_GetObjectItem(blob_obj, "size");
    if (!link || !cJSON_IsString(link) || !mime || !cJSON_IsString(mime) || !size || !cJSON_IsNumber(size)) {
        cJSON_Delete(root);
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }

    out->cid = wf_dup_span(link->valuestring, strlen(link->valuestring));
    out->mime_type = wf_dup_span(mime->valuestring, strlen(mime->valuestring));
    out->size = (size_t)size->valuedouble;

    cJSON_Delete(root);
    wf_response_free(&resp);
    return WF_OK;
}

wf_status wf_agent_upload_video(wf_agent *agent, const void *data, size_t data_len,
                               const char *content_type, wf_uploaded_blob *out) {
    if (!agent || !data || data_len == 0 || !content_type || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response resp = {0};
    wf_status status = wf_xrpc_upload_blob(agent->client, WF_UPLOAD_VIDEO_NSID,
                                            data, data_len, content_type, &resp);
    if (status != WF_OK) {
        return status;
    }
    // Reuse the same parsing logic as upload_blob_ex
    cJSON *root = cJSON_ParseWithLength(resp.body, resp.body_len);
    if (!root) {
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }
    cJSON *blob_obj = cJSON_GetObjectItem(root, "blob");
    if (!blob_obj || !cJSON_IsObject(blob_obj)) {
        cJSON_Delete(root);
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }
    cJSON *type = cJSON_GetObjectItem(blob_obj, "$type");
    if (!type || !cJSON_IsString(type) || !wf_ascii_iequals(type->valuestring, "blob")) {
        cJSON_Delete(root);
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }
    cJSON *ref = cJSON_GetObjectItem(blob_obj, "ref");
    cJSON *link = NULL;
    if (ref && cJSON_IsObject(ref)) {
        link = cJSON_GetObjectItem(ref, "$link");
    }
    cJSON *mime = cJSON_GetObjectItem(blob_obj, "mimeType");
    cJSON *size = cJSON_GetObjectItem(blob_obj, "size");
    if (!link || !cJSON_IsString(link) || !mime || !cJSON_IsString(mime) || !size || !cJSON_IsNumber(size)) {
        cJSON_Delete(root);
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }
    out->cid = wf_dup_span(link->valuestring, strlen(link->valuestring));
    out->mime_type = wf_dup_span(mime->valuestring, strlen(mime->valuestring));
    out->size = (size_t)size->valuedouble;
    cJSON_Delete(root);
    wf_response_free(&resp);
    return WF_OK;
}

void wf_uploaded_blob_free(wf_uploaded_blob *blob) {
    if (!blob) return;
    free(blob->cid);
    free(blob->mime_type);
    blob->cid = NULL;
    blob->mime_type = NULL;
    blob->size = 0;
}
