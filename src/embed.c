#include "wolfram/embed.h"
#include "wolfram/util.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include "wolfram/embed_typed.h"

cJSON *wf_embed_images_new(void) {
    cJSON *embed = cJSON_CreateObject();
    if (!embed) return NULL;
    if (!cJSON_AddStringToObject(embed, "$type", wf_embed_type_str(WF_EMBED_IMAGES))) {
        cJSON_Delete(embed);
        return NULL;
    }
    cJSON *images = cJSON_CreateArray();
    if (!images) {
        cJSON_Delete(embed);
        return NULL;
    }
    if (!cJSON_AddItemToObject(embed, "images", images)) {
        cJSON_Delete(images);
        cJSON_Delete(embed);
        return NULL;
    }
    return embed;
}

wf_status wf_embed_images_add_image(cJSON *embed, const wf_uploaded_blob *blob, const char *alt) {
    if (!embed || !blob) return WF_ERR_INVALID_ARG;
    cJSON *images = cJSON_GetObjectItem(embed, "images");
    if (!images || !cJSON_IsArray(images)) return WF_ERR_INVALID_ARG;

    // Build blob JSON
    cJSON *blob_obj = cJSON_CreateObject();
    if (!blob_obj) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(blob_obj, "$type", "blob")) {
        cJSON_Delete(blob_obj);
        return WF_ERR_ALLOC;
    }
    cJSON *ref = cJSON_CreateObject();
    if (!ref) { cJSON_Delete(blob_obj); return WF_ERR_ALLOC; }
    if (!cJSON_AddStringToObject(ref, "$link", blob->cid)) {
        cJSON_Delete(ref); cJSON_Delete(blob_obj); return WF_ERR_ALLOC;
    }
    if (!cJSON_AddItemToObject(blob_obj, "ref", ref)) {
        cJSON_Delete(ref); cJSON_Delete(blob_obj); return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(blob_obj, "mimeType", blob->mime_type)) {
        cJSON_Delete(blob_obj); return WF_ERR_ALLOC;
    }
    if (!cJSON_AddNumberToObject(blob_obj, "size", (double)blob->size)) {
        cJSON_Delete(blob_obj); return WF_ERR_ALLOC;
    }

    // Image object
    cJSON *image_obj = cJSON_CreateObject();
    if (!image_obj) { cJSON_Delete(blob_obj); return WF_ERR_ALLOC; }
    if (!cJSON_AddItemToObject(image_obj, "image", blob_obj)) {
        cJSON_Delete(image_obj); cJSON_Delete(blob_obj); return WF_ERR_ALLOC;
    }
    if (alt && alt[0]) {
        if (!cJSON_AddStringToObject(image_obj, "alt", alt)) {
            cJSON_Delete(image_obj);
            return WF_ERR_ALLOC;
        }
    } else {
        // optional empty alt
        if (!cJSON_AddStringToObject(image_obj, "alt", "")) {
            cJSON_Delete(image_obj);
            return WF_ERR_ALLOC;
        }
    }

    if (!cJSON_AddItemToArray(images, image_obj)) {
        cJSON_Delete(image_obj);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

cJSON *wf_embed_video_new(const wf_uploaded_blob *blob, const char *alt) {
    if (!blob) return NULL;
    cJSON *embed = cJSON_CreateObject();
    if (!embed) return NULL;
    if (!cJSON_AddStringToObject(embed, "$type", wf_embed_type_str(WF_EMBED_VIDEO))) {
        cJSON_Delete(embed);
        return NULL;
    }
    // Build blob object (same as images)
    cJSON *blob_obj = cJSON_CreateObject();
    if (!blob_obj) { cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddStringToObject(blob_obj, "$type", "blob")) { cJSON_Delete(blob_obj); cJSON_Delete(embed); return NULL; }
    cJSON *ref = cJSON_CreateObject();
    if (!ref) { cJSON_Delete(blob_obj); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddStringToObject(ref, "$link", blob->cid)) { cJSON_Delete(ref); cJSON_Delete(blob_obj); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddItemToObject(blob_obj, "ref", ref)) { cJSON_Delete(blob_obj); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddStringToObject(blob_obj, "mimeType", blob->mime_type)) { cJSON_Delete(blob_obj); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddNumberToObject(blob_obj, "size", (double)blob->size)) { cJSON_Delete(blob_obj); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddItemToObject(embed, "video", blob_obj)) { cJSON_Delete(embed); return NULL; }
    if (alt && alt[0]) {
        if (!cJSON_AddStringToObject(embed, "alt", alt)) { cJSON_Delete(embed); return NULL; }
    }
    return embed;
}

cJSON *wf_embed_external_new(const char *uri, const char *thumb, const char *title, const char *description) {
    if (!uri) return NULL;
    cJSON *embed = cJSON_CreateObject();
    if (!embed) return NULL;
    if (!cJSON_AddStringToObject(embed, "$type", wf_embed_type_str(WF_EMBED_EXTERNAL))) { cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddStringToObject(embed, "uri", uri)) { cJSON_Delete(embed); return NULL; }
    if (thumb && thumb[0]) {
        if (!cJSON_AddStringToObject(embed, "thumb", thumb)) { cJSON_Delete(embed); return NULL; }
    }
    if (title && title[0]) {
        if (!cJSON_AddStringToObject(embed, "title", title)) { cJSON_Delete(embed); return NULL; }
    }
    if (description && description[0]) {
        if (!cJSON_AddStringToObject(embed, "description", description)) { cJSON_Delete(embed); return NULL; }
    }
    return embed;
}

cJSON *wf_embed_record_new(const char *uri, const char *cid) {
    if (!uri || !cid) return NULL;
    cJSON *embed = cJSON_CreateObject();
    if (!embed) return NULL;
    if (!cJSON_AddStringToObject(embed, "$type", wf_embed_type_str(WF_EMBED_RECORD))) { cJSON_Delete(embed); return NULL; }
    cJSON *record = cJSON_CreateObject();
    if (!record) { cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddStringToObject(record, "uri", uri)) { cJSON_Delete(record); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddStringToObject(record, "cid", cid)) { cJSON_Delete(record); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddItemToObject(embed, "record", record)) { cJSON_Delete(record); cJSON_Delete(embed); return NULL; }
    return embed;
}

cJSON *wf_embed_record_with_media_new(cJSON *record_embed, cJSON *media_embed) {
    if (!record_embed || !media_embed) return NULL;
    cJSON *embed = cJSON_CreateObject();
    if (!embed) return NULL;
    if (!cJSON_AddStringToObject(embed, "$type", wf_embed_type_str(WF_EMBED_RECORD_WITH_MEDIA))) { cJSON_Delete(embed); return NULL; }
    // Both inputs are objects; attach them and transfer ownership.
    if (!cJSON_AddItemToObject(embed, "record", record_embed)) { cJSON_Delete(record_embed); cJSON_Delete(embed); return NULL; }
    if (!cJSON_AddItemToObject(embed, "media", media_embed)) { cJSON_Delete(media_embed); cJSON_Delete(embed); return NULL; }
    return embed;
}
