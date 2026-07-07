#ifndef WOLFRAM_RICHTEXT_H
#define WOLFRAM_RICHTEXT_H

#include <stddef.h>
#include <stdint.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_richtext_feature_type {
    WF_RICHTEXT_FEATURE_MENTION,
    WF_RICHTEXT_FEATURE_LINK,
    WF_RICHTEXT_FEATURE_TAG,
} wf_richtext_feature_type;

typedef struct wf_richtext_feature {
    wf_richtext_feature_type type;
    union {
        char did[256];
        char uri[4096];
        char tag[640];
    };
} wf_richtext_feature;

typedef struct wf_richtext_facet {
    uint32_t byte_start;
    uint32_t byte_end;
    wf_richtext_feature *features;
    size_t feature_count;
} wf_richtext_facet;

typedef struct wf_richtext_segment {
    const char *text;
    size_t text_len;
    const wf_richtext_facet *facet;
} wf_richtext_segment;

typedef struct wf_richtext {
    char *text;
    size_t text_len;
    wf_richtext_facet *facets;
    size_t facet_count;
    int owns_text;
} wf_richtext;

size_t wf_richtext_grapheme_len(const char *utf8_text, size_t utf8_len);

wf_status wf_richtext_init(wf_richtext *rt, const char *text);
void wf_richtext_free(wf_richtext *rt);

wf_status wf_richtext_detect_facets(wf_richtext *rt);
wf_status wf_richtext_sanitize(wf_richtext *rt, int clean_newlines);
size_t wf_richtext_segment_count(const wf_richtext *rt);
wf_richtext_segment wf_richtext_get_segment(const wf_richtext *rt, size_t index);

wf_status wf_richtext_insert(wf_richtext *rt, uint32_t offset, const char *text);
wf_status wf_richtext_delete(wf_richtext *rt, uint32_t start, uint32_t end);

int wf_richtext_is_valid_domain(const char *domain);
int wf_richtext_is_valid_tld(const char *tld);

#ifdef __cplusplus
}
#endif

#endif
