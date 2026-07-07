#ifndef WOLFRAM_EMBED_H
#define WOLFRAM_EMBED_H

#include "wolfram/util.h"
#include "wolfram/blob.h"
#include <cJSON.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WF_EMBED_IMAGES,
    WF_EMBED_VIDEO,
    WF_EMBED_EXTERNAL,
    WF_EMBED_RECORD,
    WF_EMBED_RECORD_WITH_MEDIA
} wf_embed_type;

/* Convert embed type enum to its AT‑Protocol string */
static inline const char *wf_embed_type_str(wf_embed_type type) {
    switch (type) {
        case WF_EMBED_IMAGES: return "app.bsky.embed.images";
        case WF_EMBED_VIDEO: return "app.bsky.embed.video";
        case WF_EMBED_EXTERNAL: return "app.bsky.embed.external";
        case WF_EMBED_RECORD: return "app.bsky.embed.record";
        case WF_EMBED_RECORD_WITH_MEDIA: return "app.bsky.embed.recordWithMedia";
    }
    return NULL;
}

/* Image embed helpers */
cJSON *wf_embed_images_new(void);
wf_status wf_embed_images_add_image(cJSON *embed, const wf_uploaded_blob *blob, const char *alt);

/* Video embed helpers (basic) */
cJSON *wf_embed_video_new(const wf_uploaded_blob *blob, const char *alt);

/* External link embed */
cJSON *wf_embed_external_new(const char *uri, const char *thumb, const char *title, const char *description);

/* Record embed (quote) */
cJSON *wf_embed_record_new(const char *uri, const char *cid);

/* Record with media embed – combines a record embed and a media embed */
cJSON *wf_embed_record_with_media_new(cJSON *record_embed, cJSON *media_embed);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_EMBED_H */
