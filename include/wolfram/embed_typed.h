#ifndef WOLFRAM_EMBED_TYPED_H
#define WOLFRAM_EMBED_TYPED_H

#include "wolfram/util.h"
#include "wolfram/blob.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Image (blob) embed element */
typedef struct {
    char *cid;
    char *mime_type;
    size_t size;
    char *alt; /* optional, may be NULL */
} wf_embed_image_t;

typedef struct {
    wf_embed_image_t *items;
    size_t count;
    size_t capacity;
} wf_embed_images_t;

wf_status wf_embed_images_init(wf_embed_images_t *imgs);
wf_status wf_embed_images_add(wf_embed_images_t *imgs, const wf_uploaded_blob *blob, const char *alt);
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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_EMBED_TYPED_H */
