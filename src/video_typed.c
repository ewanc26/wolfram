/*
 * video_typed.c — owned typed parsers + builders + agent wrappers for the
 * app.bsky.video namespace. See include/wolfram/video_typed.h for the public
 * API, the authoritative wire format, and ownership rules.
 *
 * Mirrors labeler_typed.c / actor_prefs_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra` cJSON subtrees that preserve unknown
 * fields, and full cleanup on the first error. The agent wrappers call the
 * existing raw-response video helpers (or the shared binary-blob transport for
 * uploadVideo) after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/video_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_video_strdup(const char *s) {
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

static wf_status wf_video_set_string(char **dst, const char *src) {
    char *copy = wf_video_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- blob ---- */

static void wf_video_blob_reset(wf_video_blob *b) {
    if (!b) {
        return;
    }
    free(b->type);
    free(b->cid);
    free(b->mime_type);
    if (b->extra) {
        cJSON_Delete(b->extra);
    }
    memset(b, 0, sizeof(*b));
}

static wf_status wf_video_parse_blob(cJSON *obj, wf_video_blob *b) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "$type");
    cJSON *ref = cJSON_GetObjectItemCaseSensitive(obj, "ref");
    cJSON *link = (ref && cJSON_IsObject(ref))
                      ? cJSON_GetObjectItemCaseSensitive(ref, "$link")
                      : NULL;
    cJSON *mime = cJSON_GetObjectItemCaseSensitive(obj, "mimeType");
    cJSON *size = cJSON_GetObjectItemCaseSensitive(obj, "size");

    if (cJSON_IsString(type) && type->valuestring) {
        status = wf_video_set_string(&b->type, type->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(link) && link->valuestring) {
        status = wf_video_set_string(&b->cid, link->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(mime) && mime->valuestring) {
        status = wf_video_set_string(&b->mime_type, mime->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(size)) {
        b->has_size = true;
        b->size = (int64_t)size->valuedouble;
    }
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "$type");
        cJSON_DetachItemFromObject(obj, "ref");
        cJSON_DetachItemFromObject(obj, "mimeType");
        cJSON_DetachItemFromObject(obj, "size");
        b->extra = obj;
    } else {
        wf_video_blob_reset(b);
    }
    return status;
}

static wf_status wf_video_build_blob(const wf_video_blob *b, cJSON *out) {
    if (!cJSON_AddItemToObject(
            out, "$type",
            cJSON_CreateString(b->type ? b->type : "blob"))) {
        return WF_ERR_ALLOC;
    }
    cJSON *ref = cJSON_CreateObject();
    if (!ref) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddItemToObject(ref, "$link",
                               cJSON_CreateString(b->cid ? b->cid : ""))) {
        cJSON_Delete(ref);
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddItemToObject(out, "ref", ref)) {
        cJSON_Delete(ref);
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddItemToObject(out, "mimeType",
                               cJSON_CreateString(b->mime_type ? b->mime_type
                                                              : ""))) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddNumberToObject(out, "size",
                                 b->has_size ? (double)b->size : 0.0)) {
        return WF_ERR_ALLOC;
    }
    if (b->extra) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, b->extra) {
            cJSON *dup = cJSON_Duplicate(it, true);
            if (!dup) {
                return WF_ERR_ALLOC;
            }
            if (!cJSON_AddItemToObject(out, it->string, dup)) {
                cJSON_Delete(dup);
                return WF_ERR_ALLOC;
            }
        }
    }
    return WF_OK;
}

/* ---- jobStatus def ---- */

static void wf_video_job_status_def_reset(wf_video_job_status_def *d) {
    if (!d) {
        return;
    }
    free(d->job_id);
    free(d->did);
    free(d->state);
    free(d->error);
    free(d->message);
    wf_video_blob_reset(&d->blob);
    if (d->extra) {
        cJSON_Delete(d->extra);
    }
    memset(d, 0, sizeof(*d));
}

static wf_status wf_video_parse_job_status_def(cJSON *obj,
                                               wf_video_job_status_def *d) {
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    wf_status status = WF_OK;
    cJSON *job_id = cJSON_GetObjectItemCaseSensitive(obj, "jobId");
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(obj, "state");
    cJSON *progress = cJSON_GetObjectItemCaseSensitive(obj, "progress");
    cJSON *blob = cJSON_GetObjectItemCaseSensitive(obj, "blob");
    cJSON *error = cJSON_GetObjectItemCaseSensitive(obj, "error");
    cJSON *message = cJSON_GetObjectItemCaseSensitive(obj, "message");

    if (!cJSON_IsString(job_id) || !job_id->valuestring) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsString(did) || !did->valuestring) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsString(state) || !state->valuestring) {
        return WF_ERR_PARSE;
    }
    status = wf_video_set_string(&d->job_id, job_id->valuestring);
    if (status == WF_OK) {
        status = wf_video_set_string(&d->did, did->valuestring);
    }
    if (status == WF_OK) {
        status = wf_video_set_string(&d->state, state->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(progress)) {
        d->has_progress = true;
        d->progress = (int64_t)progress->valuedouble;
    }
    if (status == WF_OK && blob != NULL) {
        if (!cJSON_IsObject(blob)) {
            status = WF_ERR_PARSE;
        } else {
            d->has_blob = true;
            status = wf_video_parse_blob(blob, &d->blob);
        }
    }
    if (status == WF_OK && cJSON_IsString(error) && error->valuestring) {
        status = wf_video_set_string(&d->error, error->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(message) && message->valuestring) {
        status = wf_video_set_string(&d->message, message->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "jobId");
        cJSON_DetachItemFromObject(obj, "did");
        cJSON_DetachItemFromObject(obj, "state");
        cJSON_DetachItemFromObject(obj, "progress");
        cJSON_DetachItemFromObject(obj, "blob");
        cJSON_DetachItemFromObject(obj, "error");
        cJSON_DetachItemFromObject(obj, "message");
        d->extra = obj;
    } else {
        wf_video_job_status_def_reset(d);
    }
    return status;
}

wf_status wf_video_job_status_def_parse(const char *json, size_t json_len,
                                        wf_video_job_status_def *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    wf_status status = wf_video_parse_job_status_def(root, out);
    if (status != WF_OK) {
        cJSON_Delete(root);
    }
    return status;
}

wf_status wf_video_job_status_parse(const char *json, size_t json_len,
                                    wf_video_job_status *out) {
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
    cJSON *job_status = cJSON_GetObjectItemCaseSensitive(root, "jobStatus");
    if (!cJSON_IsObject(job_status)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    wf_status status = wf_video_parse_job_status_def(job_status, &out->job_status);
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "jobStatus");
        out->extra = root;
    } else {
        cJSON_Delete(root);
    }
    return status;
}

wf_status wf_video_job_status_def_build(const wf_video_job_status_def *in,
                                        char **out_json) {
    if (!in || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    if (!cJSON_AddItemToObject(obj, "jobId",
                               cJSON_CreateString(in->job_id ? in->job_id
                                                            : ""))) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK &&
        !cJSON_AddItemToObject(obj, "did",
                               cJSON_CreateString(in->did ? in->did : ""))) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK &&
        !cJSON_AddItemToObject(obj, "state",
                               cJSON_CreateString(in->state ? in->state
                                                           : ""))) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK && in->has_progress) {
        if (!cJSON_AddNumberToObject(obj, "progress", (double)in->progress)) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK && in->has_blob) {
        cJSON *blob = cJSON_CreateObject();
        if (!blob) {
            status = WF_ERR_ALLOC;
        } else {
            status = wf_video_build_blob(&in->blob, blob);
            if (status == WF_OK) {
                if (!cJSON_AddItemToObject(obj, "blob", blob)) {
                    cJSON_Delete(blob);
                    status = WF_ERR_ALLOC;
                }
            } else {
                cJSON_Delete(blob);
            }
        }
    }
    if (status == WF_OK && in->error &&
        !cJSON_AddItemToObject(obj, "error",
                               cJSON_CreateString(in->error))) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK && in->message &&
        !cJSON_AddItemToObject(obj, "message",
                               cJSON_CreateString(in->message))) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK && in->extra) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, in->extra) {
            cJSON *dup = cJSON_Duplicate(it, true);
            if (!dup) {
                status = WF_ERR_ALLOC;
                break;
            }
            if (!cJSON_AddItemToObject(obj, it->string, dup)) {
                cJSON_Delete(dup);
                status = WF_ERR_ALLOC;
                break;
            }
        }
    }
    if (status == WF_OK) {
        char *json_str = cJSON_PrintUnformatted(obj);
        if (!json_str) {
            status = WF_ERR_ALLOC;
        } else {
            *out_json = json_str;
        }
    }
    cJSON_Delete(obj);
    return status;
}

/* ---- uploadLimits ---- */

static void wf_video_upload_limits_reset(wf_video_upload_limits *l) {
    if (!l) {
        return;
    }
    free(l->message);
    free(l->error);
    if (l->extra) {
        cJSON_Delete(l->extra);
    }
    memset(l, 0, sizeof(*l));
}

static wf_status wf_video_parse_upload_limits(cJSON *obj,
                                              wf_video_upload_limits *l) {
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    cJSON *can = cJSON_GetObjectItemCaseSensitive(obj, "canUpload");
    cJSON *vids = cJSON_GetObjectItemCaseSensitive(obj, "remainingDailyVideos");
    cJSON *bytes = cJSON_GetObjectItemCaseSensitive(obj, "remainingDailyBytes");
    cJSON *message = cJSON_GetObjectItemCaseSensitive(obj, "message");
    cJSON *error = cJSON_GetObjectItemCaseSensitive(obj, "error");

    if (!cJSON_IsBool(can)) {
        return WF_ERR_PARSE;
    }
    wf_status status = WF_OK;
    l->can_upload = cJSON_IsTrue(can);
    if (cJSON_IsNumber(vids)) {
        l->has_remaining_daily_videos = true;
        l->remaining_daily_videos = (int64_t)vids->valuedouble;
    } else if (vids != NULL) {
        return WF_ERR_PARSE;
    }
    if (status == WF_OK && cJSON_IsNumber(bytes)) {
        l->has_remaining_daily_bytes = true;
        l->remaining_daily_bytes = (int64_t)bytes->valuedouble;
    } else if (bytes != NULL) {
        return WF_ERR_PARSE;
    }
    if (status == WF_OK && cJSON_IsString(message) && message->valuestring) {
        status = wf_video_set_string(&l->message, message->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(error) && error->valuestring) {
        status = wf_video_set_string(&l->error, error->valuestring);
    }
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "canUpload");
        cJSON_DetachItemFromObject(obj, "remainingDailyVideos");
        cJSON_DetachItemFromObject(obj, "remainingDailyBytes");
        cJSON_DetachItemFromObject(obj, "message");
        cJSON_DetachItemFromObject(obj, "error");
        l->extra = obj;
    } else {
        wf_video_upload_limits_reset(l);
    }
    return status;
}

wf_status wf_video_upload_limits_parse(const char *json, size_t json_len,
                                       wf_video_upload_limits *out) {
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
    wf_status status = wf_video_parse_upload_limits(root, out);
    if (status != WF_OK) {
        cJSON_Delete(root);
    }
    return status;
}

wf_status wf_video_upload_limits_build(const wf_video_upload_limits *in,
                                       char **out_json) {
    if (!in || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    if (!cJSON_AddBoolToObject(obj, "canUpload", in->can_upload)) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK && in->has_remaining_daily_videos) {
        if (!cJSON_AddNumberToObject(obj, "remainingDailyVideos",
                                     (double)in->remaining_daily_videos)) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK && in->has_remaining_daily_bytes) {
        if (!cJSON_AddNumberToObject(obj, "remainingDailyBytes",
                                     (double)in->remaining_daily_bytes)) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK && in->message &&
        !cJSON_AddItemToObject(obj, "message",
                               cJSON_CreateString(in->message))) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK && in->error &&
        !cJSON_AddItemToObject(obj, "error",
                               cJSON_CreateString(in->error))) {
        status = WF_ERR_ALLOC;
    }
    if (status == WF_OK && in->extra) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, in->extra) {
            cJSON *dup = cJSON_Duplicate(it, true);
            if (!dup) {
                status = WF_ERR_ALLOC;
                break;
            }
            if (!cJSON_AddItemToObject(obj, it->string, dup)) {
                cJSON_Delete(dup);
                status = WF_ERR_ALLOC;
                break;
            }
        }
    }
    if (status == WF_OK) {
        char *json_str = cJSON_PrintUnformatted(obj);
        if (!json_str) {
            status = WF_ERR_ALLOC;
        } else {
            *out_json = json_str;
        }
    }
    cJSON_Delete(obj);
    return status;
}

/* ---- free functions ---- */

void wf_video_blob_free(wf_video_blob *b) {
    wf_video_blob_reset(b);
}

void wf_video_job_status_def_free(wf_video_job_status_def *d) {
    wf_video_job_status_def_reset(d);
}

void wf_video_job_status_free(wf_video_job_status *s) {
    if (!s) {
        return;
    }
    wf_video_job_status_def_reset(&s->job_status);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

void wf_video_upload_limits_free(wf_video_upload_limits *l) {
    wf_video_upload_limits_reset(l);
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_video_get_job_status(wf_agent *agent, const char *job_id,
                                        wf_video_job_status *out) {
    if (!agent || !agent->client || !job_id || !job_id[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_video_job_status result = {0};

    wf_response res = {0};
    wf_status status = wf_agent_get_video_job_status(agent, job_id, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_video_job_status_parse(res.body, res.body_len, &result);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    }
    return status;
}

wf_status wf_agent_video_get_upload_limits(wf_agent *agent,
                                           wf_video_upload_limits *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_video_upload_limits result = {0};

    wf_response res = {0};
    wf_status status = wf_agent_get_video_upload_limits(agent, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_video_upload_limits_parse(res.body, res.body_len, &result);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    }
    return status;
}

wf_status wf_agent_video_upload(wf_agent *agent, const void *data,
                                size_t data_len, const char *content_type,
                                wf_video_job_status *out) {
    if (!agent || !agent->client || !data || data_len == 0 || !content_type ||
        !content_type[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_video_job_status result = {0};

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_upload_blob(
        agent->client, WF_LEX_APP_BSKY_VIDEO_UPLOAD_VIDEO_NSID, data, data_len,
        content_type, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_video_job_status_parse(res.body, res.body_len, &result);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    }
    return status;
}
