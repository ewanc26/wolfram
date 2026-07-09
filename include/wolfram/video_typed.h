/*
 * video_typed.h — owned typed parsers + builders + agent wrappers for the
 * app.bsky.video namespace (getJobStatus, getUploadLimits, uploadVideo) and the
 * shared app.bsky.video.defs#jobStatus object. See src/video_typed.c for
 * ownership rules and the authoritative wire format.
 *
 * Conventions mirror labeler_typed.h / actor_prefs_typed.h: wf_status error
 * codes, static strdup/set_string/reset helpers, ownership via cJSON_DetachItem*
 * (unknown fields are preserved in the owned `extra` cJSON subtree), and a
 * matching `_free` for every owned struct (a freed/zeroed struct frees safely).
 * Every owned string is heap-allocated.
 *
 * Wire format (from the local lexicons):
 *   - app.bsky.video.defs#jobStatus:
 *       { jobId, did, state, progress?, blob?, error?, message? }
 *     `blob` is the canonical AT Protocol blob object:
 *       { "$type":"blob", "ref":{"$link":"<cid>"}, "mimeType", "size" }.
 *   - app.bsky.video.getJobStatus  output: { jobStatus: <defs#jobStatus> }
 *   - app.bsky.video.uploadVideo   output: { jobStatus: <defs#jobStatus> }
 *   - app.bsky.video.getUploadLimits output:
 *       { canUpload, remainingDailyVideos?, remainingDailyBytes?, message?, error? }
 */

#ifndef WOLFRAM_VIDEO_TYPED_H
#define WOLFRAM_VIDEO_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A blob (app.bsky.video.defs#jobStatus.blob / generic AT Proto blob object).
 * `cid` is the ref.$link CID string; unknown fields are preserved in `extra`. */
typedef struct wf_video_blob {
    char *type;          /* $type; normally "blob"; NULL when absent */
    char *cid;           /* ref.$link; NULL when absent */
    char *mime_type;     /* mimeType; NULL when absent */
    bool has_size;
    int64_t size;
    cJSON *extra;        /* owned detached subtree of unknown fields; NULL absent */
} wf_video_blob;

/* A video processing job status (app.bsky.video.defs#jobStatus). `job_id`,
 * `did`, and `state` are required on the wire; the rest are optional. Unknown
 * fields are preserved in owned `extra`. */
typedef struct wf_video_job_status_def {
    char *job_id;        /* required */
    char *did;           /* required (did format) */
    char *state;         /* required */
    bool has_progress;
    int64_t progress;    /* 0..100 */
    bool has_blob;
    wf_video_blob blob;
    char *error;
    char *message;
    cJSON *extra;        /* owned detached subtree of unknown fields; NULL absent */
} wf_video_job_status_def;

/* Envelope for app.bsky.video.getJobStatus / app.bsky.video.uploadVideo output
 * ({ "jobStatus": <defs#jobStatus> }). Unknown fields are preserved in `extra`. */
typedef struct wf_video_job_status {
    wf_video_job_status_def job_status;
    cJSON *extra;        /* owned detached subtree of unknown fields; NULL absent */
} wf_video_job_status;

/* The authenticated user's video upload limits
 * (app.bsky.video.getUploadLimits output). `can_upload` is required on the wire;
 * the rest are optional. Unknown fields are preserved in owned `extra`. */
typedef struct wf_video_upload_limits {
    bool can_upload;     /* required */
    bool has_remaining_daily_videos;
    int64_t remaining_daily_videos;
    bool has_remaining_daily_bytes;
    int64_t remaining_daily_bytes;
    char *message;
    char *error;
    cJSON *extra;        /* owned detached subtree of unknown fields; NULL absent */
} wf_video_upload_limits;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a standalone app.bsky.video.defs#jobStatus JSON object body. */
wf_status wf_video_job_status_def_parse(const char *json, size_t json_len,
                                        wf_video_job_status_def *out);

/* Parse a { "jobStatus": <defs#jobStatus> } envelope — the output of both
 * app.bsky.video.getJobStatus and app.bsky.video.uploadVideo. */
wf_status wf_video_job_status_parse(const char *json, size_t json_len,
                                    wf_video_job_status *out);

/* Parse an app.bsky.video.getUploadLimits JSON body. */
wf_status wf_video_upload_limits_parse(const char *json, size_t json_len,
                                       wf_video_upload_limits *out);

/* ---- Builders (emit the canonical JSON strings) ----
 * Each owned struct emits its canonical JSON object (required fields always;
 * optional fields only when present; unknown `extra` fields re-emitted
 * verbatim). On success `*out_json` is owned by the caller and must be released
 * with free(). Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_ALLOC on
 * allocation failure, WF_OK on success. */

/* Build a standalone app.bsky.video.defs#jobStatus JSON object string. */
wf_status wf_video_job_status_def_build(const wf_video_job_status_def *in,
                                        char **out_json);

/* Build an app.bsky.video.getUploadLimits JSON object string. */
wf_status wf_video_upload_limits_build(const wf_video_upload_limits *in,
                                       char **out_json);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_video_blob_free(wf_video_blob *b);
void wf_video_job_status_def_free(wf_video_job_status_def *d);
void wf_video_job_status_free(wf_video_job_status *s);
void wf_video_upload_limits_free(wf_video_upload_limits *l);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding video call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty. */

/* app.bsky.video.getJobStatus. */
wf_status wf_agent_video_get_job_status(wf_agent *agent, const char *job_id,
                                        wf_video_job_status *out);

/* app.bsky.video.getUploadLimits. */
wf_status wf_agent_video_get_upload_limits(wf_agent *agent,
                                           wf_video_upload_limits *out);

/* app.bsky.video.uploadVideo. Posts `data` (length `data_len`, advertised as
 * `content_type`) to the dedicated video endpoint and parses the returned
 * jobStatus envelope. */
wf_status wf_agent_video_upload(wf_agent *agent, const void *data,
                                size_t data_len, const char *content_type,
                                wf_video_job_status *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_VIDEO_TYPED_H */
