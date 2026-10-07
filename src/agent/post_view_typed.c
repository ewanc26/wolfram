/*
 * post_view_typed.c — flattened readers for a post's record, embed, reason and
 * reply ref. See include/wolfram/post_view_typed.h.
 */

#include "wolfram/post_view_typed.h"

#include <stdlib.h>
#include <string.h>

static const char *pv_string(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring
                                                       : NULL;
}

static int pv_int(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : 0;
}

static int pv_has_prefix(const char *s, const char *prefix) {
    return s && strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Duplicate into *dst. NULL source leaves *dst NULL and succeeds. */
static wf_status pv_dup(char **dst, const char *src) {
    *dst = NULL;
    if (!src) {
        return WF_OK;
    }
    size_t len = strlen(src);
    *dst = (char *)malloc(len + 1);
    if (!*dst) {
        return WF_ERR_ALLOC;
    }
    memcpy(*dst, src, len + 1);
    return WF_OK;
}

/* ---- reply root ---- */

void wf_post_reply_root_free(wf_post_reply_root *root) {
    if (!root) {
        return;
    }
    free(root->uri);
    free(root->cid);
    memset(root, 0, sizeof(*root));
}

wf_status wf_post_reply_root_from_json(const cJSON *reply_ref,
                                       wf_post_reply_root *out) {
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!reply_ref) {
        return WF_OK;
    }
    const cJSON *root = cJSON_GetObjectItemCaseSensitive(reply_ref, "root");
    const char *uri = pv_string(root, "uri");
    const char *cid = pv_string(root, "cid");
    if (!uri || !cid) {
        return WF_OK;
    }
    if (pv_dup(&out->uri, uri) != WF_OK || pv_dup(&out->cid, cid) != WF_OK) {
        wf_post_reply_root_free(out);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/* ---- record ---- */

void wf_post_record_free(wf_post_record *rec) {
    if (!rec) {
        return;
    }
    free(rec->text);
    free(rec->created_at);
    for (size_t i = 0; i < rec->facet_count; i++) {
        free(rec->facets[i].target);
    }
    free(rec->facets);
    wf_post_reply_root_free(&rec->reply_root);
    memset(rec, 0, sizeof(*rec));
}

static int pv_facet_kind(const char *type, wf_post_facet_kind *kind) {
    if (!type) {
        return 0;
    }
    if (strstr(type, "richtext.facet#link")) {
        *kind = WF_POST_FACET_LINK;
    } else if (strstr(type, "richtext.facet#mention")) {
        *kind = WF_POST_FACET_MENTION;
    } else if (strstr(type, "richtext.facet#tag")) {
        *kind = WF_POST_FACET_TAG;
    } else {
        return 0;
    }
    return 1;
}

static wf_status pv_read_facets(const cJSON *facets, wf_post_record *out) {
    if (!cJSON_IsArray(facets)) {
        return WF_OK;
    }
    int n = cJSON_GetArraySize(facets);
    if (n <= 0) {
        return WF_OK;
    }
    out->facets = (wf_post_facet *)calloc((size_t)n, sizeof(wf_post_facet));
    if (!out->facets) {
        return WF_ERR_ALLOC;
    }

    const cJSON *facet;
    cJSON_ArrayForEach(facet, facets) {
        const cJSON *index = cJSON_GetObjectItemCaseSensitive(facet, "index");
        const cJSON *features =
            cJSON_GetObjectItemCaseSensitive(facet, "features");
        if (!index || !cJSON_IsArray(features)) {
            continue;
        }
        const cJSON *feature;
        cJSON_ArrayForEach(feature, features) {
            wf_post_facet_kind kind;
            if (!pv_facet_kind(pv_string(feature, "$type"), &kind)) {
                continue;
            }
            const char *target =
                kind == WF_POST_FACET_LINK      ? pv_string(feature, "uri")
                : kind == WF_POST_FACET_MENTION ? pv_string(feature, "did")
                                                : pv_string(feature, "tag");
            wf_post_facet *f = &out->facets[out->facet_count];
            f->byte_start = pv_int(index, "byteStart");
            f->byte_end = pv_int(index, "byteEnd");
            f->kind = kind;
            if (pv_dup(&f->target, target ? target : "") != WF_OK) {
                return WF_ERR_ALLOC;
            }
            out->facet_count++;
            break; /* the first known feature decides */
        }
    }
    return WF_OK;
}

wf_status wf_post_record_from_json(const cJSON *record, wf_post_record *out) {
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!record) {
        return WF_OK;
    }
    wf_status st = pv_dup(&out->text, pv_string(record, "text"));
    if (st == WF_OK) {
        st = pv_dup(&out->created_at, pv_string(record, "createdAt"));
    }
    if (st == WF_OK) {
        st = pv_read_facets(cJSON_GetObjectItemCaseSensitive(record, "facets"),
                            out);
    }
    if (st == WF_OK) {
        st = wf_post_reply_root_from_json(
            cJSON_GetObjectItemCaseSensitive(record, "reply"),
            &out->reply_root);
    }
    if (st != WF_OK) {
        wf_post_record_free(out);
    }
    return st;
}

/* ---- reason ---- */

void wf_post_reason_free(wf_post_reason *reason) {
    if (!reason) {
        return;
    }
    free(reason->by_display_name);
    free(reason->by_handle);
    memset(reason, 0, sizeof(*reason));
}

wf_status wf_post_reason_from_json(const cJSON *reason, wf_post_reason *out) {
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!reason || !pv_has_prefix(pv_string(reason, "$type"),
                                  "app.bsky.feed.defs#reasonRepost")) {
        return WF_OK;
    }
    const cJSON *by = cJSON_GetObjectItemCaseSensitive(reason, "by");
    if (!by) {
        return WF_OK;
    }
    out->is_repost = 1;
    if (pv_dup(&out->by_display_name, pv_string(by, "displayName")) != WF_OK ||
        pv_dup(&out->by_handle, pv_string(by, "handle")) != WF_OK) {
        wf_post_reason_free(out);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/* ---- embed ---- */

void wf_post_embed_free(wf_post_embed *embed) {
    if (!embed) {
        return;
    }
    free(embed->type);
    free(embed->media_type);
    for (size_t i = 0; i < embed->image_count; i++) {
        free(embed->images[i].thumb);
        free(embed->images[i].alt);
    }
    free(embed->images);
    free(embed->external_uri);
    free(embed->external_title);
    free(embed->external_description);
    free(embed->external_thumb);
    free(embed->video_thumb);
    free(embed->video_alt);
    free(embed->quote_author_display_name);
    free(embed->quote_author_handle);
    free(embed->quote_text);
    memset(embed, 0, sizeof(*embed));
}

static wf_status pv_read_images(const cJSON *images_view, wf_post_embed *out) {
    const cJSON *images =
        cJSON_GetObjectItemCaseSensitive(images_view, "images");
    if (!cJSON_IsArray(images)) {
        return WF_OK;
    }
    int n = cJSON_GetArraySize(images);
    if (n <= 0) {
        return WF_OK;
    }
    out->images =
        (wf_post_embed_image *)calloc((size_t)n, sizeof(wf_post_embed_image));
    if (!out->images) {
        return WF_ERR_ALLOC;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, images) {
        const char *thumb = pv_string(item, "thumb");
        if (!thumb || !thumb[0]) {
            continue;
        }
        wf_post_embed_image *img = &out->images[out->image_count];
        const cJSON *ratio =
            cJSON_GetObjectItemCaseSensitive(item, "aspectRatio");
        img->width = pv_int(ratio, "width");
        img->height = pv_int(ratio, "height");
        if (pv_dup(&img->thumb, thumb) != WF_OK ||
            pv_dup(&img->alt, pv_string(item, "alt")) != WF_OK) {
            free(img->thumb);
            free(img->alt);
            memset(img, 0, sizeof(*img));
            return WF_ERR_ALLOC;
        }
        out->image_count++;
    }
    return WF_OK;
}

static wf_status pv_read_external(const cJSON *external_view,
                                  wf_post_embed *out) {
    const cJSON *external =
        cJSON_GetObjectItemCaseSensitive(external_view, "external");
    const char *uri = pv_string(external, "uri");
    if (!uri || !uri[0]) {
        return WF_OK;
    }
    out->has_external = 1;
    wf_status st = pv_dup(&out->external_uri, uri);
    if (st == WF_OK) {
        st = pv_dup(&out->external_title, pv_string(external, "title"));
    }
    if (st == WF_OK) {
        st = pv_dup(&out->external_description,
                    pv_string(external, "description"));
    }
    if (st == WF_OK) {
        st = pv_dup(&out->external_thumb, pv_string(external, "thumb"));
    }
    return st;
}

static wf_status pv_read_video(const cJSON *video_view, wf_post_embed *out) {
    const cJSON *ratio =
        cJSON_GetObjectItemCaseSensitive(video_view, "aspectRatio");
    int w = pv_int(ratio, "width");
    int h = pv_int(ratio, "height");

    if (w > 0 && h > 0) {
        out->video_width = w;
        out->video_height = h;
    }
    wf_status st =
        pv_dup(&out->video_thumb, pv_string(video_view, "thumbnail"));
    if (st == WF_OK) {
        st = pv_dup(&out->video_alt, pv_string(video_view, "alt"));
    }
    return st;
}

/* record#view carries { record: viewRecord }; recordWithMedia#view nests that
 * one level deeper under record.record. */
static wf_status pv_read_quote(const cJSON *embed, int with_media,
                               wf_post_embed *out) {
    const cJSON *rec = cJSON_GetObjectItemCaseSensitive(embed, "record");
    if (with_media) {
        rec = rec ? cJSON_GetObjectItemCaseSensitive(rec, "record") : NULL;
    }
    if (!rec || !pv_has_prefix(pv_string(rec, "$type"),
                               "app.bsky.embed.record#viewRecord")) {
        return WF_OK;
    }
    const cJSON *author = cJSON_GetObjectItemCaseSensitive(rec, "author");
    if (!author) {
        return WF_OK;
    }
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(rec, "value");
    out->has_quote = 1;
    wf_status st = pv_dup(&out->quote_author_display_name,
                          pv_string(author, "displayName"));
    if (st == WF_OK) {
        st = pv_dup(&out->quote_author_handle, pv_string(author, "handle"));
    }
    if (st == WF_OK) {
        st = pv_dup(&out->quote_text, value ? pv_string(value, "text") : NULL);
    }
    return st;
}

wf_status wf_post_embed_from_json(const cJSON *embed, wf_post_embed *out) {
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!embed) {
        return WF_OK;
    }

    const char *type = pv_string(embed, "$type");
    const int with_media =
        pv_has_prefix(type, "app.bsky.embed.recordWithMedia");
    const cJSON *media = embed;
    const char *media_type = type;
    if (with_media) {
        media = cJSON_GetObjectItemCaseSensitive(embed, "media");
        media_type = pv_string(media, "$type");
    }

    wf_status st = pv_dup(&out->type, type);
    if (st == WF_OK) {
        st = pv_dup(&out->media_type, media_type);
    }
    if (st == WF_OK && pv_has_prefix(media_type, "app.bsky.embed.images")) {
        st = pv_read_images(media, out);
    } else if (st == WF_OK &&
               pv_has_prefix(media_type, "app.bsky.embed.external")) {
        st = pv_read_external(media, out);
    } else if (st == WF_OK &&
               pv_has_prefix(media_type, "app.bsky.embed.video")) {
        st = pv_read_video(media, out);
    }
    if (st == WF_OK &&
        (with_media || pv_has_prefix(type, "app.bsky.embed.record"))) {
        st = pv_read_quote(embed, with_media, out);
    }
    if (st != WF_OK) {
        wf_post_embed_free(out);
    }
    return st;
}
