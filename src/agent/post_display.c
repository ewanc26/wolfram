/*
 * post_display.c — display helpers over typed feed posts (see
 * wolfram/post_display.h).
 *
 * Follows feed_typed.c: static helpers local to the TU, WF_OK / WF_ERR_*
 * status codes, full cleanup on the first error. Everything read from the
 * cJSON subtrees is untrusted: every access is type-checked.
 */

#include "wolfram/post_display.h"

#include "_internal.h"

#include <cJSON.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WF_DISPLAY_MAX_FACETS 512

static char *wf_pd_strdup(const char *s) {
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

/* Borrowed string member of `obj`, or NULL if absent / not a string. */
static const char *wf_pd_str(const cJSON *obj, const char *key) {
    if (!cJSON_IsObject(obj)) {
        return NULL;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(v) && v->valuestring) ? v->valuestring : NULL;
}

static const cJSON *wf_pd_obj(const cJSON *obj, const char *key) {
    if (!cJSON_IsObject(obj)) {
        return NULL;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsObject(v) ? v : NULL;
}

/* Copy member `key` into *dst if it is a non-empty string. */
static wf_status wf_pd_copy_str(const cJSON *obj, const char *key, char **dst) {
    const char *s = wf_pd_str(obj, key);
    if (!s || !*s) {
        return WF_OK;
    }
    *dst = wf_pd_strdup(s);
    return *dst ? WF_OK : WF_ERR_ALLOC;
}

/* $type match tolerant of a missing "#view"/"#main" suffix mismatch: compares
 * the full string exactly. */
static int wf_pd_type_is(const cJSON *obj, const char *type) {
    const char *t = wf_pd_str(obj, "$type");
    return t && strcmp(t, type) == 0;
}

void wf_post_display_free(wf_post_display *d) {
    if (!d) {
        return;
    }
    free(d->text);
    free(d->created_at);
    for (size_t i = 0; i < d->facet_count; ++i) {
        free(d->facets[i].target);
    }
    free(d->facets);
    free(d->external_title);
    free(d->external_uri);
    free(d->quote_uri);
    free(d->quote_author_handle);
    free(d->quote_text);
    memset(d, 0, sizeof(*d));
}

/* ── facets ─────────────────────────────────────────────────────────── */

/* Read a byte offset: a finite, integral number in [0, limit]. */
static int wf_pd_offset(const cJSON *n, size_t limit, size_t *out) {
    if (!cJSON_IsNumber(n)) {
        return 0;
    }
    double v = n->valuedouble;
    if (!isfinite(v) || v < 0.0 || v != floor(v) || v > (double)limit) {
        return 0;
    }
    *out = (size_t)v;
    return 1;
}

static int wf_pd_is_boundary(const char *text, size_t len, size_t pos) {
    return pos >= len || ((unsigned char)text[pos] & 0xC0) != 0x80;
}

static int wf_pd_facet_cmp(const void *a, const void *b) {
    const wf_display_facet *fa = (const wf_display_facet *)a;
    const wf_display_facet *fb = (const wf_display_facet *)b;
    if (fa->byte_start != fb->byte_start) {
        return fa->byte_start < fb->byte_start ? -1 : 1;
    }
    if (fa->byte_end != fb->byte_end) {
        return fa->byte_end < fb->byte_end ? -1 : 1;
    }
    return 0;
}

/* Pick the first known feature of a facet. *target is a malloc'd copy. */
static wf_status wf_pd_facet_feature(const cJSON *features, wf_facet_kind *kind,
                                     char **target, int *found) {
    *found = 0;
    if (!cJSON_IsArray(features)) {
        return WF_OK;
    }
    const cJSON *f;
    cJSON_ArrayForEach(f, features) {
        const char *key = NULL;
        wf_facet_kind k;
        if (wf_pd_type_is(f, "app.bsky.richtext.facet#link")) {
            key = "uri";
            k = WF_FACET_LINK;
        } else if (wf_pd_type_is(f, "app.bsky.richtext.facet#mention")) {
            key = "did";
            k = WF_FACET_MENTION;
        } else if (wf_pd_type_is(f, "app.bsky.richtext.facet#tag")) {
            key = "tag";
            k = WF_FACET_TAG;
        } else {
            continue;
        }
        const char *val = wf_pd_str(f, key);
        if (!val || !*val) {
            continue;
        }
        *target = wf_pd_strdup(val);
        if (!*target) {
            return WF_ERR_ALLOC;
        }
        *kind = k;
        *found = 1;
        return WF_OK;
    }
    return WF_OK;
}

static wf_status wf_pd_parse_facets(const cJSON *record, wf_post_display *out) {
    const cJSON *arr = cJSON_IsObject(record)
                           ? cJSON_GetObjectItemCaseSensitive(record, "facets")
                           : NULL;
    if (!cJSON_IsArray(arr)) {
        return WF_OK;
    }
    size_t text_len = strlen(out->text);
    size_t cap = (size_t)cJSON_GetArraySize(arr);
    if (cap > WF_DISPLAY_MAX_FACETS) {
        cap = WF_DISPLAY_MAX_FACETS;
    }
    if (cap == 0) {
        return WF_OK;
    }
    wf_display_facet *cand = (wf_display_facet *)calloc(cap, sizeof(*cand));
    if (!cand) {
        return WF_ERR_ALLOC;
    }
    size_t n = 0;
    wf_status status = WF_OK;
    const cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (n >= cap) {
            break;
        }
        const cJSON *index = wf_pd_obj(item, "index");
        size_t start, end;
        if (!index ||
            !wf_pd_offset(cJSON_GetObjectItemCaseSensitive(index, "byteStart"),
                          text_len, &start) ||
            !wf_pd_offset(cJSON_GetObjectItemCaseSensitive(index, "byteEnd"),
                          text_len, &end) ||
            start >= end || !wf_pd_is_boundary(out->text, text_len, start) ||
            !wf_pd_is_boundary(out->text, text_len, end)) {
            continue;
        }
        wf_facet_kind kind = WF_FACET_LINK;
        char *target = NULL;
        int found = 0;
        status = wf_pd_facet_feature(
            cJSON_GetObjectItemCaseSensitive(item, "features"), &kind, &target,
            &found);
        if (status != WF_OK) {
            break;
        }
        if (!found) {
            continue;
        }
        cand[n].kind = kind;
        cand[n].byte_start = start;
        cand[n].byte_end = end;
        cand[n].target = target;
        n++;
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < n; ++i) {
            free(cand[i].target);
        }
        free(cand);
        return status;
    }
    if (n > 1) {
        /* qsort is not stable; ties (same start and end) are dropped below as
         * overlaps anyway, so which one survives is irrelevant. */
        qsort(cand, n, sizeof(*cand), wf_pd_facet_cmp);
    }
    size_t kept = 0;
    size_t last_end = 0;
    for (size_t i = 0; i < n; ++i) {
        if (kept > 0 && cand[i].byte_start < last_end) {
            free(cand[i].target);
            continue;
        }
        last_end = cand[i].byte_end;
        cand[kept++] = cand[i];
    }
    if (kept == 0) {
        free(cand);
        return WF_OK;
    }
    out->facets = cand;
    out->facet_count = kept;
    return WF_OK;
}

/* ── embed ──────────────────────────────────────────────────────────── */

static wf_post_embed_kind wf_pd_media_kind(const cJSON *e) {
    if (wf_pd_type_is(e, "app.bsky.embed.images#view")) {
        return WF_EMBED_IMAGES;
    }
    if (wf_pd_type_is(e, "app.bsky.embed.video#view")) {
        return WF_EMBED_VIDEO;
    }
    if (wf_pd_type_is(e, "app.bsky.embed.external#view")) {
        return WF_EMBED_EXTERNAL;
    }
    return WF_EMBED_UNKNOWN;
}

/* Fill image_count / external_* from an images/video/external view. */
static wf_status wf_pd_apply_media(const cJSON *media, wf_post_embed_kind kind,
                                   wf_post_display *out) {
    if (kind == WF_EMBED_IMAGES) {
        const cJSON *images = cJSON_GetObjectItemCaseSensitive(media, "images");
        int n = cJSON_IsArray(images) ? cJSON_GetArraySize(images) : 0;
        out->image_count = n > 0 ? (size_t)n : 0;
    } else if (kind == WF_EMBED_EXTERNAL) {
        const cJSON *ext = wf_pd_obj(media, "external");
        wf_status s = wf_pd_copy_str(ext, "title", &out->external_title);
        if (s != WF_OK) {
            return s;
        }
        return wf_pd_copy_str(ext, "uri", &out->external_uri);
    }
    return WF_OK;
}

/* `rec` is the record#view's `record` member (a viewRecord, viewNotFound, ...).
 * Only a viewRecord populates the quote_* fields. */
static wf_status wf_pd_apply_quote(const cJSON *rec, wf_post_display *out) {
    if (!wf_pd_type_is(rec, "app.bsky.embed.record#viewRecord")) {
        return WF_OK;
    }
    wf_status s = wf_pd_copy_str(rec, "uri", &out->quote_uri);
    if (s != WF_OK) {
        return s;
    }
    s = wf_pd_copy_str(wf_pd_obj(rec, "author"), "handle",
                       &out->quote_author_handle);
    if (s != WF_OK) {
        return s;
    }
    return wf_pd_copy_str(wf_pd_obj(rec, "value"), "text", &out->quote_text);
}

static wf_status wf_pd_parse_embed(const cJSON *embed, wf_post_display *out) {
    if (!cJSON_IsObject(embed)) {
        return WF_OK; /* NONE */
    }
    if (wf_pd_type_is(embed, "app.bsky.embed.record#view")) {
        out->embed_kind = WF_EMBED_RECORD;
        return wf_pd_apply_quote(wf_pd_obj(embed, "record"), out);
    }
    if (wf_pd_type_is(embed, "app.bsky.embed.recordWithMedia#view")) {
        out->embed_kind = WF_EMBED_RECORD_WITH_MEDIA;
        const cJSON *outer = wf_pd_obj(embed, "record"); /* record#view */
        wf_status s = wf_pd_apply_quote(wf_pd_obj(outer, "record"), out);
        if (s != WF_OK) {
            return s;
        }
        const cJSON *media = wf_pd_obj(embed, "media");
        return wf_pd_apply_media(media, wf_pd_media_kind(media), out);
    }
    wf_post_embed_kind kind = wf_pd_media_kind(embed);
    out->embed_kind = kind;
    return wf_pd_apply_media(embed, kind, out);
}

/* ── public API ─────────────────────────────────────────────────────── */

wf_status wf_agent_post_view_display(const wf_agent_post_view *post,
                                     wf_post_display *out) {
    if (!post || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    const cJSON *record = cJSON_IsObject(post->record) ? post->record : NULL;
    const char *text = wf_pd_str(record, "text");
    out->text = wf_pd_strdup(text ? text : "");
    wf_status status = out->text ? WF_OK : WF_ERR_ALLOC;

    if (status == WF_OK) {
        status = wf_pd_copy_str(record, "createdAt", &out->created_at);
    }
    if (status == WF_OK) {
        status = wf_pd_parse_facets(record, out);
    }
    if (status == WF_OK) {
        status = wf_pd_parse_embed(post->embed, out);
    }
    if (status == WF_OK) {
        out->is_reply = wf_pd_obj(record, "reply") != NULL;
    }
    if (status != WF_OK) {
        wf_post_display_free(out);
    }
    return status;
}

wf_status wf_agent_feed_item_reposted_by(const wf_agent_feed_item *item,
                                         char **out_name) {
    if (!item || !out_name) {
        return WF_ERR_INVALID_ARG;
    }
    *out_name = NULL;
    if (!wf_pd_type_is(item->reason, "app.bsky.feed.defs#reasonRepost")) {
        return WF_OK;
    }
    const cJSON *by = wf_pd_obj(item->reason, "by");
    const char *name = wf_pd_str(by, "displayName");
    if (!name || !*name) {
        name = wf_pd_str(by, "handle");
    }
    if (!name || !*name) {
        return WF_OK;
    }
    *out_name = wf_pd_strdup(name);
    return *out_name ? WF_OK : WF_ERR_ALLOC;
}

wf_status wf_agent_fetch_public(wf_agent *agent, const char *url,
                                size_t max_bytes, wf_response *out) {
    if (!agent || !agent->client || !url || !out || max_bytes == 0) {
        return WF_ERR_INVALID_ARG;
    }
    /* wf_http_get_public enforces https-only, strips credentials from a private
     * config snapshot (CA bundle and TLS RNG are carried over from the agent's
     * client), and caps redirects. */
    return wf_http_get_public(agent->client, url, max_bytes, out);
}
