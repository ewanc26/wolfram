#include "wolfram/richtext.h"
#include "wolfram/xrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── UTF-8 utilities ── */

size_t wf_richtext_grapheme_len(const char *utf8_text, size_t utf8_len) {
    size_t count = 0;
    size_t i = 0;
    while (i < utf8_len) {
        unsigned char c = (unsigned char)utf8_text[i];
        if (c <= 0x7f) i += 1;
        else if (c <= 0xdf) i += 2;
        else if (c <= 0xef) i += 3;
        else if (c <= 0xf7) i += 4;
        else i++;
        count++;
    }
    return count;
}

/* ── richtext lifecycle ── */

wf_status wf_richtext_init(wf_richtext *rt, const char *text) {
    if (!rt || !text) return WF_ERR_INVALID_ARG;
    rt->text = strdup(text);
    if (!rt->text) return WF_ERR_ALLOC;
    rt->text_len = strlen(rt->text);
    rt->facets = NULL;
    rt->facet_count = 0;
    rt->owns_text = 1;
    return WF_OK;
}

void wf_richtext_free(wf_richtext *rt) {
    if (!rt) return;
    if (rt->owns_text) free(rt->text);
    for (size_t i = 0; i < rt->facet_count; i++)
        free(rt->facets[i].features);
    free(rt->facets);
    memset(rt, 0, sizeof(*rt));
}

static int facet_cmp(const void *a, const void *b) {
    const wf_richtext_facet *fa = (const wf_richtext_facet *)a;
    const wf_richtext_facet *fb = (const wf_richtext_facet *)b;
    if (fa->byte_start < fb->byte_start) return -1;
    if (fa->byte_start > fb->byte_start) return 1;
    return 0;
}

/* ── segment iteration ── */

static int is_whitespace_only(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r')
            return 0;
    return 1;
}

size_t wf_richtext_segment_count(const wf_richtext *rt) {
    if (!rt) return 0;
    if (rt->facet_count == 0) return 1;
    size_t count = 0;
    size_t cursor = 0;
    for (size_t i = 0; i < rt->facet_count; i++) {
        if (rt->facets[i].byte_start > cursor) count++;
        cursor = rt->facets[i].byte_end;
        count++;
    }
    if (cursor < rt->text_len) count++;
    return count;
}

wf_richtext_segment wf_richtext_get_segment(const wf_richtext *rt, size_t index) {
    wf_richtext_segment seg = {0};
    if (!rt) return seg;

    if (rt->facet_count == 0) {
        if (index == 0) {
            seg.text = rt->text;
            seg.text_len = rt->text_len;
        }
        return seg;
    }

    size_t cursor = 0;
    size_t seg_idx = 0;

    for (size_t i = 0; i < rt->facet_count; i++) {
        if (rt->facets[i].byte_start > cursor) {
            if (seg_idx == index) {
                seg.text = rt->text + cursor;
                seg.text_len = rt->facets[i].byte_start - cursor;
                return seg;
            }
            seg_idx++;
            cursor = rt->facets[i].byte_start;
        }
        if (seg_idx == index) {
            size_t flen = rt->facets[i].byte_end - rt->facets[i].byte_start;
            if (flen > 0 && !is_whitespace_only(rt->text + rt->facets[i].byte_start, flen)) {
                seg.text = rt->text + rt->facets[i].byte_start;
                seg.text_len = flen;
                seg.facet = &rt->facets[i];
            }
            return seg;
        }
        seg_idx++;
        cursor = rt->facets[i].byte_end;
    }

    if (cursor < rt->text_len && seg_idx == index) {
        seg.text = rt->text + cursor;
        seg.text_len = rt->text_len - cursor;
    }

    return seg;
}

/* ── mutation: insert ── */

wf_status wf_richtext_insert(wf_richtext *rt, uint32_t offset, const char *text) {
    if (!rt || !text || offset > rt->text_len) return WF_ERR_INVALID_ARG;
    size_t ilen = strlen(text);

    char *new_text = malloc(rt->text_len + ilen + 1);
    if (!new_text) return WF_ERR_ALLOC;
    memcpy(new_text, rt->text, offset);
    memcpy(new_text + offset, text, ilen);
    memcpy(new_text + offset + ilen, rt->text + offset, rt->text_len - offset + 1);

    if (rt->owns_text) free(rt->text);
    rt->text = new_text;
    rt->text_len += ilen;
    rt->owns_text = 1;

    for (size_t i = 0; i < rt->facet_count; i++) {
        wf_richtext_facet *f = &rt->facets[i];
        if (offset <= f->byte_start)
            f->byte_start += ilen;
        if (offset < f->byte_end)
            f->byte_end += ilen;
    }

    return WF_OK;
}

/* ── mutation: delete ── */

wf_status wf_richtext_delete(wf_richtext *rt, uint32_t start, uint32_t end) {
    if (!rt || start >= end || end > rt->text_len) return WF_ERR_INVALID_ARG;
    size_t removed = end - start;

    char *new_text = malloc(rt->text_len - removed + 1);
    if (!new_text) return WF_ERR_ALLOC;
    memcpy(new_text, rt->text, start);
    memcpy(new_text + start, rt->text + end, rt->text_len - end + 1);

    if (rt->owns_text) free(rt->text);
    rt->text = new_text;
    rt->text_len -= removed;
    rt->owns_text = 1;

    size_t write_idx = 0;
    for (size_t i = 0; i < rt->facet_count; i++) {
        wf_richtext_facet *f = &rt->facets[i];
        if (start <= f->byte_start && end >= f->byte_end) {
            continue;
        }
        if (start > f->byte_end) {
            f->byte_start -= removed;
            f->byte_end -= removed;
        } else if (start <= f->byte_start && end <= f->byte_end && end > f->byte_start) {
            f->byte_start = start;
            f->byte_end -= removed;
        } else if (start >= f->byte_start && end <= f->byte_end) {
            f->byte_end -= removed;
        } else if (start < f->byte_start && end >= f->byte_start && end <= f->byte_end) {
            f->byte_start = start;
            f->byte_end -= removed;
        } else if (start > f->byte_start && start < f->byte_end && end > f->byte_end) {
            f->byte_end = start;
        }

        if (f->byte_start < f->byte_end) {
            if (write_idx < i) rt->facets[write_idx] = rt->facets[i];
            write_idx++;
        } else {
            free(f->features);
        }
    }
    rt->facet_count = write_idx;

    return WF_OK;
}

/* ── domain/TLD validation helpers ── */

static const char *known_tlds[] = {
    "com", "org", "net", "edu", "gov", "mil", "int",
    "uk", "de", "jp", "fr", "au", "us", "ca", "ch", "it",
    "nl", "se", "no", "dk", "fi", "es", "at", "be", "pl",
    "br", "in", "cn", "ru", "za", "mx", "kr", "sg", "hk",
    "io", "app", "dev", "co", "me", "xyz", "info", "biz",
    "name", "pro", "tv", "cc", "ws", "cloud", "tech", "site",
    "online", "club", "world", "life", "blog", "design",
    "tools", "social", "video", "wiki", "media", "news",
    "test", NULL
};

int wf_richtext_is_valid_tld(const char *tld) {
    if (!tld || !*tld) return 0;
    for (int i = 0; known_tlds[i]; i++)
        if (strcasecmp(tld, known_tlds[i]) == 0) return 1;
    return 0;
}

int wf_richtext_is_valid_domain(const char *domain) {
    if (!domain || !*domain) return 0;
    const char *dot = strrchr(domain, '.');
    if (!dot || dot == domain) return 0;
    return wf_richtext_is_valid_tld(dot + 1);
}
