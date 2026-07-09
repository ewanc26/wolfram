#include "wolfram/embed.h"
#include "wolfram/embed_typed.h"
#include "wolfram/util.h"
#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"
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
    img->width = 0;
    img->height = 0;
    if (!img->cid || !img->mime_type || (alt && !img->alt)) {
        free(img->cid);
        free(img->mime_type);
        free(img->alt);
        return WF_ERR_ALLOC;
    }
    imgs->count++;
    return WF_OK;
}

wf_status wf_embed_images_add_with_aspect(wf_embed_images_t *imgs, const wf_uploaded_blob *blob, const char *alt, int width, int height) {
    wf_status s = wf_embed_images_add(imgs, blob, alt);
    if (s != WF_OK) return s;
    wf_embed_image_t *img = &imgs->items[imgs->count - 1];
    img->width = width;
    img->height = height;
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
        if (img->width > 0 && img->height > 0) {
            cJSON *images = cJSON_GetObjectItem(embed, "images");
            cJSON *image_obj = cJSON_GetArrayItem(images, (int)i);
            if (!image_obj) {
                cJSON_Delete(embed);
                return NULL;
            }
            cJSON *ar = cJSON_CreateObject();
            if (!ar) { cJSON_Delete(embed); return NULL; }
            if (!cJSON_AddNumberToObject(ar, "width", img->width)) {
                cJSON_Delete(ar); cJSON_Delete(embed); return NULL;
            }
            if (!cJSON_AddNumberToObject(ar, "height", img->height)) {
                cJSON_Delete(ar); cJSON_Delete(embed); return NULL;
            }
            if (!cJSON_AddItemToObject(image_obj, "aspectRatio", ar)) {
                cJSON_Delete(ar); cJSON_Delete(embed); return NULL;
            }
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

/* ----- getEmbedExternalView (owned typed parser + agent wrapper) ----- */

/* Small string helpers kept static per TU (mirrors actor_typed.c). */
static char *wf_embed_strdup(const char *s) {
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

static wf_status wf_embed_set_string(char **dst, const char *src) {
    char *copy = wf_embed_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

void wf_embed_external_view_free(wf_embed_external_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->title);
    free(v->description);
    free(v->thumb);
    if (v->view) {
        cJSON_Delete(v->view);
    }
    if (v->associated_refs) {
        cJSON_Delete(v->associated_refs);
    }
    if (v->associated_records) {
        cJSON_Delete(v->associated_records);
    }
    memset(v, 0, sizeof(*v));
}

wf_status wf_embed_parse_external_view(const char *json, size_t json_len,
                                       wf_embed_external_view *out) {
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

    wf_status status = WF_OK;
    cJSON *view = cJSON_GetObjectItemCaseSensitive(root, "view");
    if (cJSON_IsObject(view)) {
        out->has_view = true;
        cJSON *external = cJSON_GetObjectItemCaseSensitive(view, "external");
        if (cJSON_IsObject(external)) {
            cJSON *uri = cJSON_GetObjectItemCaseSensitive(external, "uri");
            cJSON *title = cJSON_GetObjectItemCaseSensitive(external, "title");
            cJSON *desc =
                cJSON_GetObjectItemCaseSensitive(external, "description");
            cJSON *thumb = cJSON_GetObjectItemCaseSensitive(external, "thumb");
            if (cJSON_IsString(uri) && uri->valuestring) {
                status = wf_embed_set_string(&out->uri, uri->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(title) && title->valuestring) {
                status = wf_embed_set_string(&out->title, title->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
                status = wf_embed_set_string(&out->description, desc->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(thumb) && thumb->valuestring) {
                status = wf_embed_set_string(&out->thumb, thumb->valuestring);
            }
        }
    }

    if (status == WF_OK) {
        out->view = cJSON_DetachItemFromObject(root, "view");
        out->associated_refs =
            cJSON_DetachItemFromObject(root, "associatedRefs");
        out->associated_records =
            cJSON_DetachItemFromObject(root, "associatedRecords");
    } else {
        wf_embed_external_view_free(out);
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_get_embed_external_view_typed(wf_agent *agent,
                                                 const char *url,
                                                 const char *const *uris,
                                                 size_t uri_count,
                                                 wf_embed_external_view *out) {
    if (!agent || !agent->client || !url || !url[0] || !uris ||
        uri_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < uri_count; ++i) {
        if (!uris[i] || !uris[i][0]) {
            return WF_ERR_INVALID_ARG;
        }
    }

    wf_lex_app_bsky_embed_get_embed_external_view_main_params params = {0};
    params.url = url;
    params.uris.items = uris;
    params.uris.count = uri_count;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_embed_get_embed_external_view_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_embed_external_view v = {0};
    status = wf_embed_parse_external_view(res.body, res.body_len, &v);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = v;
    }
    return status;
}
