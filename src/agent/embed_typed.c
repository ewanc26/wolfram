#include "wolfram/embed.h"
#include "wolfram/embed_typed.h"
#include "wolfram/util.h"
#include <stdlib.h>
#include <string.h>

/* ----- Images ----- */
wf_status wf_embed_images_init(wf_embed_images_t *imgs) {
    if (!imgs) return WF_ERR_INVALID_ARG;
    imgs->items = NULL;
    imgs->count = 0;
    imgs->capacity = 0;
    return WF_OK;
}

wf_status wf_embed_images_add(wf_embed_images_t *imgs, const wf_uploaded_blob *blob, const char *alt) {
    if (!imgs || !blob) return WF_ERR_INVALID_ARG;
    if (imgs->capacity == 0) {
        imgs->capacity = 4;
        imgs->items = calloc(imgs->capacity, sizeof(wf_embed_image_t));
        if (!imgs->items) return WF_ERR_ALLOC;
    } else if (imgs->count == imgs->capacity) {
        size_t new_cap = imgs->capacity * 2;
        wf_embed_image_t *new_items = realloc(imgs->items, new_cap * sizeof(wf_embed_image_t));
        if (!new_items) return WF_ERR_ALLOC;
        imgs->items = new_items;
        imgs->capacity = new_cap;
    }
    wf_embed_image_t *img = &imgs->items[imgs->count];
    img->cid = wf_dup_span(blob->cid, strlen(blob->cid));
    img->mime_type = wf_dup_span(blob->mime_type, strlen(blob->mime_type));
    img->size = blob->size;
    img->alt = alt ? wf_dup_span(alt, strlen(alt)) : NULL;
    if (!img->cid || !img->mime_type || (alt && !img->alt)) {
        free(img->cid);
        free(img->mime_type);
        free(img->alt);
        return WF_ERR_ALLOC;
    }
    imgs->count++;
    return WF_OK;
}

void wf_embed_images_free(wf_embed_images_t *imgs) {
    if (!imgs) return;
    for (size_t i = 0; i < imgs->count; ++i) {
        free(imgs->items[i].cid);
        free(imgs->items[i].mime_type);
        free(imgs->items[i].alt);
    }
    free(imgs->items);
    imgs->items = NULL;
    imgs->count = imgs->capacity = 0;
}

cJSON *wf_embed_images_build(const wf_embed_images_t *imgs) {
    if (!imgs) return NULL;
    cJSON *embed = wf_embed_images_new();
    if (!embed) return NULL;
    for (size_t i = 0; i < imgs->count; ++i) {
        const wf_embed_image_t *img = &imgs->items[i];
        wf_uploaded_blob tmp = { .cid = img->cid, .mime_type = img->mime_type, .size = img->size };
        wf_status s = wf_embed_images_add_image(embed, &tmp, img->alt);
        if (s != WF_OK) {
            cJSON_Delete(embed);
            return NULL;
        }
    }
    return embed;
}

/* ----- Video ----- */
wf_status wf_embed_video_init(wf_embed_video_t *vid, const wf_uploaded_blob *blob, const char *alt) {
    if (!vid || !blob) return WF_ERR_INVALID_ARG;
    vid->cid = wf_dup_span(blob->cid, strlen(blob->cid));
    vid->mime_type = wf_dup_span(blob->mime_type, strlen(blob->mime_type));
    vid->size = blob->size;
    vid->alt = alt ? wf_dup_span(alt, strlen(alt)) : NULL;
    if (!vid->cid || !vid->mime_type || (alt && !vid->alt)) {
        free(vid->cid);
        free(vid->mime_type);
        free(vid->alt);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

cJSON *wf_embed_video_build(const wf_embed_video_t *vid) {
    if (!vid) return NULL;
    wf_uploaded_blob tmp = { .cid = vid->cid, .mime_type = vid->mime_type, .size = vid->size };
    return wf_embed_video_new(&tmp, vid->alt);
}

void wf_embed_video_free(wf_embed_video_t *vid) {
    if (!vid) return;
    free(vid->cid);
    free(vid->mime_type);
    free(vid->alt);
    vid->cid = vid->mime_type = vid->alt = NULL;
    vid->size = 0;
}

/* ----- Record ----- */
wf_status wf_embed_record_init(wf_embed_record_t *rec, const char *uri, const char *cid) {
    if (!rec || !uri || !cid) return WF_ERR_INVALID_ARG;
    rec->uri = wf_dup_span(uri, strlen(uri));
    rec->cid = wf_dup_span(cid, strlen(cid));
    if (!rec->uri || !rec->cid) {
        free(rec->uri);
        free(rec->cid);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

cJSON *wf_embed_record_build(const wf_embed_record_t *rec) {
    if (!rec) return NULL;
    return wf_embed_record_new(rec->uri, rec->cid);
}

void wf_embed_record_free(wf_embed_record_t *rec) {
    if (!rec) return;
    free(rec->uri);
    free(rec->cid);
    rec->uri = rec->cid = NULL;
}

/* ----- Record with Media ----- */
cJSON *wf_embed_record_with_media_build(const wf_embed_record_t *record, const cJSON *media) {
    if (!record || !media) return NULL;
    cJSON *rec_json = wf_embed_record_new(record->uri, record->cid);
    if (!rec_json) return NULL;
    cJSON *out = wf_embed_record_with_media_new(rec_json, (cJSON *)media);
    if (!out) {
        cJSON_Delete(rec_json);
    }
    return out;
}
