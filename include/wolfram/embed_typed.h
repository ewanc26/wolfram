#ifndef WOLFRAM_EMBED_TYPED_H
#define WOLFRAM_EMBED_TYPED_H

#include "wolfram/util.h"
#include "wolfram/blob.h"
#include "wolfram/agent.h"
#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>

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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_EMBED_TYPED_H */
