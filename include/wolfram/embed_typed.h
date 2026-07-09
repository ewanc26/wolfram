#ifndef WOLFRAM_EMBED_TYPED_H
#define WOLFRAM_EMBED_TYPED_H

#include "wolfram/util.h"
#include "wolfram/blob.h"
#include "wolfram/agent.h"
#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Image (blob) embed element */
typedef struct {
    char *cid;
    char *mime_type;
    size_t size;
    char *alt; /* optional, may be NULL */
    int width;  /* aspect ratio width; 0 means unset */
    int height; /* aspect ratio height; 0 means unset */
} wf_embed_image_t;

typedef struct {
    wf_embed_image_t *items;
    size_t count;
    size_t capacity;
} wf_embed_images_t;

wf_status wf_embed_images_init(wf_embed_images_t *imgs);
wf_status wf_embed_images_add(wf_embed_images_t *imgs, const wf_uploaded_blob *blob, const char *alt);
/* Like wf_embed_images_add but also records the image aspectRatio. Prefer this
 * (or call wf_image_dimensions first) so the embed carries the required
 * aspectRatio field; wf_embed_images_add leaves the aspect unset. */
wf_status wf_embed_images_add_with_aspect(wf_embed_images_t *imgs, const wf_uploaded_blob *blob, const char *alt, int width, int height);
void wf_embed_images_free(wf_embed_images_t *imgs);
cJSON *wf_embed_images_build(const wf_embed_images_t *imgs);

/* Video embed typed representation */
typedef struct {
    char *cid;
    char *mime_type;
    size_t size;
    char *alt; /* optional */
} wf_embed_video_t;

wf_status wf_embed_video_init(wf_embed_video_t *vid, const wf_uploaded_blob *blob, const char *alt);
cJSON *wf_embed_video_build(const wf_embed_video_t *vid);
void wf_embed_video_free(wf_embed_video_t *vid);

/* Record embed typed representation */
typedef struct {
    char *uri;
    char *cid;
} wf_embed_record_t;

wf_status wf_embed_record_init(wf_embed_record_t *rec, const char *uri, const char *cid);
cJSON *wf_embed_record_build(const wf_embed_record_t *rec);
void wf_embed_record_free(wf_embed_record_t *rec);

/* Record with media builder – combines a record embed with any media embed JSON */
cJSON *wf_embed_record_with_media_build(const wf_embed_record_t *record, const cJSON *media);

/* ------------------------------------------------------------------ */
/* getEmbedExternalView (app.bsky.embed.getEmbedExternalView)         */
/* ------------------------------------------------------------------ */

/* Owned view of an app.bsky.embed.getEmbedExternalView response.
 *
 * The response is `{}` when nothing resolved (or when validation determined the
 * resolved records don't back the requested URL); in that case `has_view` is
 * false and all owned subtrees are NULL. When present, the hydrated external
 * card's core fields (uri/title/description/thumb) are copied out of
 * view.external for convenience, while the full `view`, `associatedRefs`, and
 * `associatedRecords` shapes are open/unbounded and kept as owned detached cJSON
 * subtrees. Free with wf_embed_external_view_free (safe on a reset struct). */
typedef struct wf_embed_external_view {
    bool has_view;              /* whether a hydrated "view" was returned */
    char *uri;                  /* view.external.uri (canonical URL) */
    char *title;                /* view.external.title */
    char *description;          /* view.external.description */
    char *thumb;                /* view.external.thumb; NULL when absent */
    cJSON *view;                /* owned detached "view" subtree; NULL absent */
    cJSON *associated_refs;     /* owned detached "associatedRefs" array; NULL absent */
    cJSON *associated_records;  /* owned detached "associatedRecords" array; NULL absent */
} wf_embed_external_view;

/* Parse a getEmbedExternalView JSON body into an owned struct. Returns
 * WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON,
 * WF_ERR_ALLOC on allocation failure, WF_OK on success (including the empty
 * `{}` response, which yields has_view=false). On any error `out` is reset. */
wf_status wf_embed_parse_external_view(const char *json, size_t json_len,
                                       wf_embed_external_view *out);

/* Free the owned contents (also safe on a reset/zeroed struct). */
void wf_embed_external_view_free(wf_embed_external_view *v);

/* app.bsky.embed.getEmbedExternalView — resolve one or more AT-URIs into the
 * data needed to render an enhanced external embed. `url` is the canonical web
 * URL the embed represents (required, non-empty); `uris`/`uri_count` are the
 * AT-URIs to resolve (required, non-empty; each entry must be non-empty). Syncs
 * auth, issues the generated query against the agent's primary XRPC client, and
 * parses the body into `out`. On success `out` is owned by the caller (free with
 * wf_embed_external_view_free); on error it is left reset. Returns
 * WF_ERR_INVALID_ARG when required inputs are NULL/empty. */
wf_status wf_agent_get_embed_external_view_typed(wf_agent *agent,
                                                  const char *url,
                                                  const char *const *uris,
                                                  size_t uri_count,
                                                  wf_embed_external_view *out);

/* ------------------------------------------------------------------ */
/* Inline embed view parsers (app.bsky.embed.*#view shapes)            */
/* ------------------------------------------------------------------ */
/*
 * These parse the `embed` member of a postView (or a record embed) into owned C
 * structs. Open/unbounded sub-objects (author profile, record `value`, labels,
 * nested embeds, source, associated refs/profiles) are kept as owned detached
 * cJSON subtrees, mirroring the feed-typed convention that keeps `record`/`embed`
 * as detached cJSON. Every owned string is heap-allocated and every struct has a
 * matching `_free` (safe on a reset/zeroed struct; `memset` after free).
 */

/* A single image in an images#view or gallery#view. The two lexicons use
 * different thumbnail keys ("thumb" vs "thumbnail"); the parser accepts either
 * and stores it in `thumb`. `width`/`height` are 0 when the optional
 * aspectRatio is absent (use `has_aspect` to disambiguate). */
typedef struct wf_embed_view_image {
    char *thumb;
    char *fullsize;
    char *alt;
    int width;
    int height;
    int has_aspect;
} wf_embed_view_image;

typedef struct wf_embed_view_images {
    wf_embed_view_image *items;
    size_t count;
} wf_embed_view_images;

/* Parse an images#view JSON body (root has an "images" array). */
wf_status wf_embed_parse_images_view(const char *json, size_t json_len,
                                     wf_embed_view_images *out);
void wf_embed_view_images_free(wf_embed_view_images *v);

/* A video#view (root is the view object). */
typedef struct wf_embed_view_video {
    char *cid;                   /* required */
    char *playlist;              /* required */
    char *thumbnail;             /* optional */
    char *alt;                   /* optional */
    int width;
    int height;
    int has_aspect;
    char *presentation;          /* optional ("default" | "gif") */
} wf_embed_view_video;

wf_status wf_embed_parse_video_view(const char *json, size_t json_len,
                                    wf_embed_view_video *out);
void wf_embed_view_video_free(wf_embed_view_video *v);

/* An external#view card (root has an "external" object). Open sub-objects are
 * kept as owned detached cJSON. */
typedef struct wf_embed_view_external {
    char *uri;                  /* required */
    char *title;                /* required */
    char *description;          /* required */
    char *thumb;                /* optional */
    char *created_at;           /* optional */
    char *updated_at;           /* optional */
    int64_t reading_time;
    int has_reading_time;
    cJSON *labels;              /* optional detached array */
    cJSON *source;              /* optional detached object */
    cJSON *associated_refs;    /* optional detached array */
    cJSON *associated_profiles; /* optional detached array */
} wf_embed_view_external;

wf_status wf_embed_parse_external_embed_view(const char *json, size_t json_len,
                                             wf_embed_view_external *out);
void wf_embed_view_external_free(wf_embed_view_external *v);

/* A record#view union (app.bsky.embed.record#view). The inner record is one of
 * several shapes; we discriminate on the inner object's keys / `$type` and parse
 * the common viewRecord form in full, detaching the others into `extra`. */
typedef enum {
    WF_EMBED_REC_VIEW_RECORD = 0,
    WF_EMBED_REC_VIEW_NOT_FOUND,
    WF_EMBED_REC_VIEW_BLOCKED,
    WF_EMBED_REC_VIEW_DETACHED,
    WF_EMBED_REC_VIEW_GENERATOR,
    WF_EMBED_REC_VIEW_LIST,
    WF_EMBED_REC_VIEW_LABELER,
    WF_EMBED_REC_VIEW_STARTERPACK,
    WF_EMBED_REC_VIEW_UNKNOWN
} wf_embed_rec_view_kind;

typedef struct wf_embed_view_record_record {
    char *uri;
    char *cid;
    char *indexed_at;
    int64_t reply_count;
    int has_reply_count;
    int64_t repost_count;
    int has_repost_count;
    int64_t like_count;
    int has_like_count;
    int64_t quote_count;
    int has_quote_count;
    cJSON *author;              /* profileViewBasic, detached */
    cJSON *value;               /* record value, detached */
    cJSON *labels;              /* detached array */
    cJSON *embeds;              /* detached array */
} wf_embed_view_record_record;

typedef struct wf_embed_record_embed_view {
    wf_embed_rec_view_kind kind;
    char *uri;                  /* present in every variant */
    wf_embed_view_record_record record; /* used when kind == RECORD */
    cJSON *extra;               /* whole inner object, detached for other kinds */
} wf_embed_record_embed_view;

wf_status wf_embed_parse_record_view(const char *json, size_t json_len,
                                      wf_embed_record_embed_view *out);
void wf_embed_record_embed_view_free(wf_embed_record_embed_view *v);

/* A recordWithMedia#view (root has "record" + "media"). */
typedef struct wf_embed_record_with_media_view {
    wf_embed_record_embed_view record; /* the inner record#view */
    cJSON *media;                      /* the media view, detached */
} wf_embed_record_with_media_view;

wf_status wf_embed_parse_record_with_media_view(const char *json, size_t json_len,
                                                wf_embed_record_with_media_view *out);
void wf_embed_record_with_media_view_free(wf_embed_record_with_media_view *v);

/* A gallery#view (root has an "items" array of viewImage). */
typedef struct wf_embed_view_gallery {
    wf_embed_view_image *items;
    size_t count;
} wf_embed_view_gallery;

wf_status wf_embed_parse_gallery_view(const char *json, size_t json_len,
                                      wf_embed_view_gallery *out);
void wf_embed_view_gallery_free(wf_embed_view_gallery *v);

/* A discriminated union over every embed view shape, for parsing an arbitrary
 * `embed` object by its `$type`. */
typedef enum {
    WF_EMBED_VIEW_UNKNOWN = 0,
    WF_EMBED_VIEW_IMAGES,
    WF_EMBED_VIEW_VIDEO,
    WF_EMBED_VIEW_EXTERNAL,
    WF_EMBED_VIEW_RECORD,
    WF_EMBED_VIEW_RECORD_WITH_MEDIA,
    WF_EMBED_VIEW_GALLERY
} wf_embed_view_kind;

typedef struct wf_embed_view {
    wf_embed_view_kind kind;
    union {
        wf_embed_view_images images;
        wf_embed_view_video video;
        wf_embed_view_external external;
        wf_embed_record_embed_view record;
        wf_embed_record_with_media_view record_with_media;
        wf_embed_view_gallery gallery;
    } u;
} wf_embed_view;

wf_status wf_embed_parse_view(const char *json, size_t json_len,
                               wf_embed_view *out);
void wf_embed_view_free(wf_embed_view *v);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_EMBED_TYPED_H */
