/*
 * blob_store_server.c — register blob persistence/serving routes on an
 * wf_xrpc_server so wolfram can act as a PDS for blobs.
 *
 * Routes:
 *   com.atproto.repo.uploadBlob (procedure): the request body IS the raw blob
 *     bytes with a Content-Type; the handler computes the raw multicodec CID,
 *     stores it, and returns { blob: { $type, mimeType, ref: {"/": cid}, size } }.
 *   com.atproto.sync.getBlob (query): reads `did` (ignored) and `cid` params,
 *     looks the blob up, and returns the raw bytes with the stored Content-Type.
 *
 * Compiled only when WOLFRAM_BUILD_SERVER=ON (depends on xrpc_server).
 */

#include "wolfram/blob_store.h"
#include "wolfram/xrpc_server.h"
#include "wolfram/repo/cid.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Escape a string for inclusion inside a JSON string (returns owned buffer). */
static char *wf_bs_json_escape(const char *s) {
    size_t need = 0;
    for (const char *p = s; p && *p; p++) {
        if (*p == '"' || *p == '\\') need += 2;
        else need += 1;
    }
    char *out = (char *)malloc(need + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (const char *p = s; p && *p; p++) {
        if (*p == '"' || *p == '\\') out[o++] = '\\';
        out[o++] = *p;
    }
    out[o] = '\0';
    return out;
}

static wf_status blob_upload_handler(void *ctx, const wf_xrpc_request *req,
                                      wf_xrpc_response *resp) {
    wf_blob_store *store = (wf_blob_store *)ctx;

    if (!req->body || req->body_len == 0) {
        wf_xrpc_response_set_error(resp, 400, "InvalidRequest",
                                    "blob body is empty");
        return WF_OK;
    }

    const char *mime = req->content_type && req->content_type[0]
                           ? req->content_type
                           : "application/octet-stream";

    /* Compute the blob CID (raw multicodec, sha2-256) — matches what a real
     * PDS returns and what wf_agent_sync_get_blob expects as the ref. */
    wf_cid cid;
    if (wf_cid_of_bytes(req->body, req->body_len, &cid) != WF_OK) {
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                    "failed to compute blob CID");
        return WF_OK;
    }
    char *cid_str = wf_cid_to_string(&cid);
    if (!cid_str) {
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                    "failed to encode blob CID");
        return WF_OK;
    }

    if (wf_blob_store_put(store, cid_str, mime, req->body,
                           req->body_len) != WF_OK) {
        free(cid_str);
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                    "failed to store blob");
        return WF_OK;
    }

    char *esc_mime = wf_bs_json_escape(mime);
    if (!esc_mime) {
        free(cid_str);
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                    "failed to build response");
        return WF_OK;
    }

    /* TypedBlobRef shape per the com.atproto.repo.uploadBlob output schema. */
    char *json = (char *)malloc(strlen(cid_str) + strlen(esc_mime) + 128);
    if (!json) {
        free(cid_str); free(esc_mime);
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                    "failed to build response");
        return WF_OK;
    }
    snprintf(json, strlen(cid_str) + strlen(esc_mime) + 128,
             "{\"blob\":{\"$type\":\"blob\",\"mimeType\":\"%s\","
             "\"ref\":{\"/\":\"%s\"},\"size\":%zu}}",
             esc_mime, cid_str, req->body_len);

    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
    free(cid_str);
    free(esc_mime);
    return WF_OK;
}

static wf_status blob_get_handler(void *ctx, const wf_xrpc_request *req,
                                   wf_xrpc_response *resp) {
    wf_blob_store *store = (wf_blob_store *)ctx;

    cJSON *cid = req->params
                     ? cJSON_GetObjectItemCaseSensitive(req->params, "cid")
                     : NULL;
    if (!cJSON_IsString(cid) || !cid->valuestring || !*cid->valuestring) {
        wf_xrpc_response_set_error(resp, 400, "InvalidRequest",
                                    "missing or invalid 'cid' parameter");
        return WF_OK;
    }

    unsigned char *data = NULL;
    size_t len = 0;
    char *mime = NULL;
    wf_status s = wf_blob_store_get(store, cid->valuestring, &data, &len, &mime);
    if (s == WF_ERR_NOT_FOUND) {
        wf_xrpc_response_set_error(resp, 404, "BlobNotFound",
                                    "no blob stored for the given CID");
        return WF_OK;
    } else if (s != WF_OK) {
        wf_xrpc_response_set_error(resp, 500, "InternalError",
                                    "failed to read blob store");
        return WF_OK;
    }

    /* Serve the raw bytes with the stored Content-Type. */
    wf_xrpc_response_set_content_type(resp, mime);
    wf_xrpc_response_set_body(resp, (const char *)data, len);
    free(data);
    free(mime);
    return WF_OK;
}

wf_status wf_xrpc_server_register_blob_store(wf_xrpc_server *server,
                                             wf_blob_store *store) {
    if (!server || !store) {
        return WF_ERR_INVALID_ARG;
    }
    wf_status s = wf_xrpc_server_register_procedure(
        server, "com.atproto.repo.uploadBlob", blob_upload_handler, store);
    if (s != WF_OK) return s;
    return wf_xrpc_server_register_query(
        server, "com.atproto.sync.getBlob", blob_get_handler, store);
}
