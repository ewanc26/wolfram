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

/* ----- Inline embed view parsers (app.bsky.embed.*#view) ----- */

/* Parse an integer into int64_t, tracking presence. */
static wf_status wf_embed_parse_i64(cJSON *num, int64_t *dst, int *has) {
    if (cJSON_IsNumber(num)) {
        double v = num->valuedouble;
        if (v > 9.2233720368547758e18) {
            *dst = INT64_MAX;
        } else if (v < -9.2233720368547758e18) {
            *dst = INT64_MIN;
        } else {
            *dst = (int64_t)v;
        }
        *has = 1;
    }
    return WF_OK;
}

/* Parse the viewImage shape (shared by images#view and gallery#view). Accepts
 * either the "thumb" key (images) or "thumbnail" key (gallery). */
static wf_status wf_embed_parse_view_image(cJSON *obj,
                                           wf_embed_view_image *img) {
    wf_status status = WF_OK;
    cJSON *thumb = cJSON_GetObjectItemCaseSensitive(obj, "thumb");
    if (!cJSON_IsString(thumb) || !thumb->valuestring) {
        thumb = cJSON_GetObjectItemCaseSensitive(obj, "thumbnail");
    }
    cJSON *fullsize = cJSON_GetObjectItemCaseSensitive(obj, "fullsize");
    cJSON *alt = cJSON_GetObjectItemCaseSensitive(obj, "alt");

    if (cJSON_IsString(thumb) && thumb->valuestring) {
        status = wf_embed_set_string(&img->thumb, thumb->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(fullsize) && fullsize->valuestring) {
        status = wf_embed_set_string(&img->fullsize, fullsize->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(alt) && alt->valuestring) {
        status = wf_embed_set_string(&img->alt, alt->valuestring);
    }
    cJSON *ar = cJSON_GetObjectItemCaseSensitive(obj, "aspectRatio");
    if (status == WF_OK && cJSON_IsObject(ar)) {
        cJSON *w = cJSON_GetObjectItemCaseSensitive(ar, "width");
        cJSON *h = cJSON_GetObjectItemCaseSensitive(ar, "height");
        if (cJSON_IsNumber(w) && cJSON_IsNumber(h)) {
            img->width = (int)w->valuedouble;
            img->height = (int)h->valuedouble;
            img->has_aspect = 1;
        }
    }
    return status;
}

static void wf_embed_view_image_reset(wf_embed_view_image *img) {
    if (!img) {
        return;
    }
    free(img->thumb);
    free(img->fullsize);
    free(img->alt);
    memset(img, 0, sizeof(*img));
}

static void wf_embed_view_images_reset(wf_embed_view_images *v) {
    if (!v) {
        return;
    }
    for (size_t i = 0; i < v->count; ++i) {
        wf_embed_view_image_reset(&v->items[i]);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

wf_status wf_embed_parse_images_view(const char *json, size_t json_len,
                                     wf_embed_view_images *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "images");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_embed_view_image *items = NULL;
    if (count > 0) {
        items = (wf_embed_view_image *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_embed_parse_view_image(obj, &items[i]);
        if (status != WF_OK) {
            wf_embed_view_image_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_embed_view_image_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_embed_view_images_free(wf_embed_view_images *v) {
    wf_embed_view_images_reset(v);
}

/* ----- Video view ----- */

static void wf_embed_view_video_reset(wf_embed_view_video *v) {
    if (!v) {
        return;
    }
    free(v->cid);
    free(v->playlist);
    free(v->thumbnail);
    free(v->alt);
    free(v->presentation);
    memset(v, 0, sizeof(*v));
}

wf_status wf_embed_parse_video_view(const char *json, size_t json_len,
                                    wf_embed_view_video *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
    cJSON *playlist = cJSON_GetObjectItemCaseSensitive(root, "playlist");
    if (cJSON_IsString(cid) && cid->valuestring) {
        status = wf_embed_set_string(&out->cid, cid->valuestring);
    } else {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && cJSON_IsString(playlist) && playlist->valuestring) {
        status = wf_embed_set_string(&out->playlist, playlist->valuestring);
    } else if (status == WF_OK) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK) {
        cJSON *thumbnail = cJSON_GetObjectItemCaseSensitive(root, "thumbnail");
        if (cJSON_IsString(thumbnail) && thumbnail->valuestring) {
            status = wf_embed_set_string(&out->thumbnail, thumbnail->valuestring);
        }
    }
    if (status == WF_OK) {
        cJSON *alt = cJSON_GetObjectItemCaseSensitive(root, "alt");
        if (cJSON_IsString(alt) && alt->valuestring) {
            status = wf_embed_set_string(&out->alt, alt->valuestring);
        }
    }
    if (status == WF_OK) {
        cJSON *ar = cJSON_GetObjectItemCaseSensitive(root, "aspectRatio");
        if (cJSON_IsObject(ar)) {
            cJSON *w = cJSON_GetObjectItemCaseSensitive(ar, "width");
            cJSON *h = cJSON_GetObjectItemCaseSensitive(ar, "height");
            if (cJSON_IsNumber(w) && cJSON_IsNumber(h)) {
                out->width = (int)w->valuedouble;
                out->height = (int)h->valuedouble;
                out->has_aspect = 1;
            }
        }
    }
    if (status == WF_OK) {
        cJSON *presentation =
            cJSON_GetObjectItemCaseSensitive(root, "presentation");
        if (cJSON_IsString(presentation) && presentation->valuestring) {
            status = wf_embed_set_string(&out->presentation,
                                         presentation->valuestring);
        }
    }

    if (status != WF_OK) {
        wf_embed_view_video_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

void wf_embed_view_video_free(wf_embed_view_video *v) {
    wf_embed_view_video_reset(v);
}

/* ----- External (card) view ----- */

static void wf_embed_view_external_reset(wf_embed_view_external *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->title);
    free(v->description);
    free(v->thumb);
    free(v->created_at);
    free(v->updated_at);
    if (v->labels) {
        cJSON_Delete(v->labels);
    }
    if (v->source) {
        cJSON_Delete(v->source);
    }
    if (v->associated_refs) {
        cJSON_Delete(v->associated_refs);
    }
    if (v->associated_profiles) {
        cJSON_Delete(v->associated_profiles);
    }
    memset(v, 0, sizeof(*v));
}

wf_status wf_embed_parse_external_embed_view(const char *json, size_t json_len,
                                             wf_embed_view_external *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *external = cJSON_GetObjectItemCaseSensitive(root, "external");
    if (cJSON_IsObject(external)) {
        cJSON *uri = cJSON_GetObjectItemCaseSensitive(external, "uri");
        cJSON *title = cJSON_GetObjectItemCaseSensitive(external, "title");
        cJSON *desc =
            cJSON_GetObjectItemCaseSensitive(external, "description");
        if (cJSON_IsString(uri) && uri->valuestring) {
            status = wf_embed_set_string(&out->uri, uri->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(title) && title->valuestring) {
            status = wf_embed_set_string(&out->title, title->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
            status =
                wf_embed_set_string(&out->description, desc->valuestring);
        }
        if (status == WF_OK) {
            cJSON *thumb = cJSON_GetObjectItemCaseSensitive(external, "thumb");
            if (cJSON_IsString(thumb) && thumb->valuestring) {
                status = wf_embed_set_string(&out->thumb, thumb->valuestring);
            }
        }
        if (status == WF_OK) {
            cJSON *created =
                cJSON_GetObjectItemCaseSensitive(external, "createdAt");
            if (cJSON_IsString(created) && created->valuestring) {
                status =
                    wf_embed_set_string(&out->created_at, created->valuestring);
            }
        }
        if (status == WF_OK) {
            cJSON *updated =
                cJSON_GetObjectItemCaseSensitive(external, "updatedAt");
            if (cJSON_IsString(updated) && updated->valuestring) {
                status =
                    wf_embed_set_string(&out->updated_at, updated->valuestring);
            }
        }
        if (status == WF_OK) {
            cJSON *rt =
                cJSON_GetObjectItemCaseSensitive(external, "readingTime");
            wf_embed_parse_i64(rt, &out->reading_time, &out->has_reading_time);
        }
    }

    if (status == WF_OK) {
        /* These open sub-objects are children of `external`, not the root. */
        out->labels = cJSON_DetachItemFromObject(external, "labels");
        out->source = cJSON_DetachItemFromObject(external, "source");
        out->associated_refs =
            cJSON_DetachItemFromObject(external, "associatedRefs");
        out->associated_profiles =
            cJSON_DetachItemFromObject(external, "associatedProfiles");
    } else {
        wf_embed_view_external_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

void wf_embed_view_external_free(wf_embed_view_external *v) {
    wf_embed_view_external_reset(v);
}

/* ----- Record view (union) ----- */

static void wf_embed_view_record_record_reset(
    wf_embed_view_record_record *r) {
    if (!r) {
        return;
    }
    free(r->uri);
    free(r->cid);
    free(r->indexed_at);
    if (r->author) {
        cJSON_Delete(r->author);
    }
    if (r->value) {
        cJSON_Delete(r->value);
    }
    if (r->labels) {
        cJSON_Delete(r->labels);
    }
    if (r->embeds) {
        cJSON_Delete(r->embeds);
    }
    memset(r, 0, sizeof(*r));
}

static void wf_embed_record_embed_view_reset(wf_embed_record_embed_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    if (v->kind == WF_EMBED_REC_VIEW_RECORD) {
        wf_embed_view_record_record_reset(&v->record);
    } else if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

/* Determine which union variant the inner record object is. */
static wf_embed_rec_view_kind wf_embed_rec_view_kind_of(cJSON *rec) {
    cJSON *type = cJSON_GetObjectItemCaseSensitive(rec, "$type");
    if (cJSON_IsString(type) && type->valuestring) {
        const char *t = type->valuestring;
        if (strstr(t, "generatorView")) {
            return WF_EMBED_REC_VIEW_GENERATOR;
        }
        if (strstr(t, "listView")) {
            return WF_EMBED_REC_VIEW_LIST;
        }
        if (strstr(t, "labelerView")) {
            return WF_EMBED_REC_VIEW_LABELER;
        }
        if (strstr(t, "starterPackViewBasic")) {
            return WF_EMBED_REC_VIEW_STARTERPACK;
        }
    }
    if (cJSON_GetObjectItemCaseSensitive(rec, "value")) {
        return WF_EMBED_REC_VIEW_RECORD;
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rec, "notFound"))) {
        return WF_EMBED_REC_VIEW_NOT_FOUND;
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rec, "blocked"))) {
        return WF_EMBED_REC_VIEW_BLOCKED;
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rec, "detached"))) {
        return WF_EMBED_REC_VIEW_DETACHED;
    }
    return WF_EMBED_REC_VIEW_UNKNOWN;
}

/* Parse the inner record#view object `rec` (already validated as an object and
 * owned/detached) into `out`. Handles the uri + kind, then either parses the
 * viewRecord fields in full or keeps the whole object in `extra`. */
static wf_status wf_embed_parse_record_view_inner(
    cJSON *rec, wf_embed_record_embed_view *out) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(rec, "uri");
    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_embed_set_string(&out->uri, uri->valuestring);
    }

    wf_embed_rec_view_kind kind = wf_embed_rec_view_kind_of(rec);
    out->kind = kind;

    if (status == WF_OK && kind == WF_EMBED_REC_VIEW_RECORD) {
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(rec, "cid");
        cJSON *indexed =
            cJSON_GetObjectItemCaseSensitive(rec, "indexedAt");
        if (cJSON_IsString(cid) && cid->valuestring) {
            status =
                wf_embed_set_string(&out->record.cid, cid->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(indexed) &&
            indexed->valuestring) {
            status = wf_embed_set_string(&out->record.indexed_at,
                                         indexed->valuestring);
        }
        if (status == WF_OK) {
            cJSON *rc =
                cJSON_GetObjectItemCaseSensitive(rec, "replyCount");
            cJSON *rpc =
                cJSON_GetObjectItemCaseSensitive(rec, "repostCount");
            cJSON *lc = cJSON_GetObjectItemCaseSensitive(rec, "likeCount");
            cJSON *qc = cJSON_GetObjectItemCaseSensitive(rec, "quoteCount");
            wf_embed_parse_i64(rc, &out->record.reply_count,
                                &out->record.has_reply_count);
            wf_embed_parse_i64(rpc, &out->record.repost_count,
                                &out->record.has_repost_count);
            wf_embed_parse_i64(lc, &out->record.like_count,
                                &out->record.has_like_count);
            wf_embed_parse_i64(qc, &out->record.quote_count,
                                &out->record.has_quote_count);
        }
        if (status == WF_OK) {
            cJSON *author = cJSON_GetObjectItemCaseSensitive(rec, "author");
            if (cJSON_IsObject(author)) {
                out->record.author =
                    cJSON_DetachItemFromObject(rec, "author");
            }
            cJSON *value = cJSON_GetObjectItemCaseSensitive(rec, "value");
            if (cJSON_IsObject(value)) {
                out->record.value = cJSON_DetachItemFromObject(rec, "value");
            }
            out->record.labels =
                cJSON_DetachItemFromObject(rec, "labels");
            out->record.embeds = cJSON_DetachItemFromObject(rec, "embeds");
        }
    } else if (status == WF_OK) {
        /* Non-record variants: keep the whole inner object owned. `rec` is
         * already detached (no parent), so take ownership directly. */
        out->extra = rec;
    }

    return status;
}

wf_status wf_embed_parse_record_view(const char *json, size_t json_len,
                                      wf_embed_record_embed_view *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *rec = cJSON_GetObjectItemCaseSensitive(root, "record");
    if (!cJSON_IsObject(rec)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *detached = cJSON_DetachItemFromObject(root, "record");
    status = wf_embed_parse_record_view_inner(detached, out);
    if (status != WF_OK) {
        cJSON_Delete(detached);
        wf_embed_record_embed_view_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

void wf_embed_record_embed_view_free(wf_embed_record_embed_view *v) {
    wf_embed_record_embed_view_reset(v);
}

/* ----- Record with media view ----- */

static void wf_embed_record_with_media_reset(
    wf_embed_record_with_media_view *v) {
    if (!v) {
        return;
    }
    wf_embed_record_embed_view_reset(&v->record);
    if (v->media) {
        cJSON_Delete(v->media);
    }
    memset(v, 0, sizeof(*v));
}

wf_status wf_embed_parse_record_with_media_view(
    const char *json, size_t json_len, wf_embed_record_with_media_view *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *rec = cJSON_GetObjectItemCaseSensitive(root, "record");
    cJSON *media = cJSON_GetObjectItemCaseSensitive(root, "media");
    if (!cJSON_IsObject(rec)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    if (cJSON_IsObject(media)) {
        out->media = cJSON_DetachItemFromObject(root, "media");
    }

    /* Parse the inner record#view via the dedicated inner helper. */
    cJSON *detached = cJSON_DetachItemFromObject(root, "record");
    status = wf_embed_parse_record_view_inner(detached, &out->record);
    if (status != WF_OK) {
        cJSON_Delete(detached);
        wf_embed_record_with_media_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

void wf_embed_record_with_media_view_free(
    wf_embed_record_with_media_view *v) {
    wf_embed_record_with_media_reset(v);
}

/* ----- Gallery view ----- */

wf_status wf_embed_parse_gallery_view(const char *json, size_t json_len,
                                      wf_embed_view_gallery *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_embed_view_image *items = NULL;
    if (count > 0) {
        items = (wf_embed_view_image *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_embed_parse_view_image(obj, &items[i]);
        if (status != WF_OK) {
            wf_embed_view_image_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_embed_view_image_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_embed_view_gallery_free(wf_embed_view_gallery *v) {
    if (!v) {
        return;
    }
    for (size_t i = 0; i < v->count; ++i) {
        wf_embed_view_image_reset(&v->items[i]);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

/* ----- Dispatcher over every embed view shape ----- */

wf_status wf_embed_parse_view(const char *json, size_t json_len,
                               wf_embed_view *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_embed_view_kind kind = WF_EMBED_VIEW_UNKNOWN;
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "$type");
    if (cJSON_IsString(type) && type->valuestring) {
        const char *t = type->valuestring;
        if (strstr(t, "images#view")) {
            kind = WF_EMBED_VIEW_IMAGES;
        } else if (strstr(t, "video#view")) {
            kind = WF_EMBED_VIEW_VIDEO;
        } else if (strstr(t, "external#view")) {
            kind = WF_EMBED_VIEW_EXTERNAL;
        } else if (strstr(t, "record#view")) {
            kind = WF_EMBED_VIEW_RECORD;
        } else if (strstr(t, "recordWithMedia#view")) {
            kind = WF_EMBED_VIEW_RECORD_WITH_MEDIA;
        } else if (strstr(t, "gallery#view")) {
            kind = WF_EMBED_VIEW_GALLERY;
        }
    }

    if (kind == WF_EMBED_VIEW_UNKNOWN) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    switch (kind) {
    case WF_EMBED_VIEW_IMAGES:
        status = wf_embed_parse_images_view(json, json_len, &out->u.images);
        break;
    case WF_EMBED_VIEW_VIDEO:
        status = wf_embed_parse_video_view(json, json_len, &out->u.video);
        break;
    case WF_EMBED_VIEW_EXTERNAL:
        status =
            wf_embed_parse_external_embed_view(json, json_len, &out->u.external);
        break;
    case WF_EMBED_VIEW_RECORD:
        status = wf_embed_parse_record_view(json, json_len, &out->u.record);
        break;
    case WF_EMBED_VIEW_RECORD_WITH_MEDIA:
        status = wf_embed_parse_record_with_media_view(
            json, json_len, &out->u.record_with_media);
        break;
    case WF_EMBED_VIEW_GALLERY:
        status = wf_embed_parse_gallery_view(json, json_len, &out->u.gallery);
        break;
    default:
        status = WF_ERR_PARSE;
        break;
    }

    if (status == WF_OK) {
        out->kind = kind;
    }

    cJSON_Delete(root);
    return status;
}

void wf_embed_view_free(wf_embed_view *v) {
    if (!v) {
        return;
    }
    switch (v->kind) {
    case WF_EMBED_VIEW_IMAGES:
        wf_embed_view_images_free(&v->u.images);
        break;
    case WF_EMBED_VIEW_VIDEO:
        wf_embed_view_video_free(&v->u.video);
        break;
    case WF_EMBED_VIEW_EXTERNAL:
        wf_embed_view_external_free(&v->u.external);
        break;
    case WF_EMBED_VIEW_RECORD:
        wf_embed_record_embed_view_free(&v->u.record);
        break;
    case WF_EMBED_VIEW_RECORD_WITH_MEDIA:
        wf_embed_record_with_media_view_free(&v->u.record_with_media);
        break;
    case WF_EMBED_VIEW_GALLERY:
        wf_embed_view_gallery_free(&v->u.gallery);
        break;
    default:
        break;
    }
    memset(v, 0, sizeof(*v));
}
