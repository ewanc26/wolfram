#include "wolfram/richtext.h"
#include "wolfram/xrpc.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ── MENTION detection ── */
/* Matches @handle.domain-like at word boundaries */

static int is_word_boundary(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '(' || c == '\0';
}

static int is_scheme(const char *s, size_t len) {
    if (len < 8) return 0;
    if (!(s[0] == 'h' && s[1] == 't' && s[2] == 't' && s[3] == 'p')) return 0;
    size_t p = 4;
    if (s[4] == 's') p = 5;
    return s[p] == ':' && s[p + 1] == '/' && s[p + 2] == '/';
}

static int is_valid_mention_char(char c) {
    return isalnum((unsigned char)c) || c == '.' || c == '-';
}

static void detect_mentions(const char *text, size_t text_len,
                            wf_richtext_facet **facets, size_t *count) {
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] != '@') continue;
        if (i > 0 && !is_word_boundary(text[i - 1])) continue;

        size_t start = i + 1;
        size_t end = start;
        while (end < text_len && is_valid_mention_char(text[end]))
            end++;

        if (end == start) continue;

        char domain[1024];
        size_t dlen = end - start;
        if (dlen >= sizeof(domain)) continue;
        memcpy(domain, text + start, dlen);
        domain[dlen] = '\0';

        if (end < text_len && isalnum((unsigned char)text[end]))
            continue;

        if (!wf_richtext_is_valid_domain(domain))
            continue;

        /* At this point text[end] is not part of the handle, so end-1 is last handle char */
        size_t handle_end = end;

        wf_richtext_facet *f = realloc(*facets, (*count + 1) * sizeof(wf_richtext_facet));
        if (!f) return;
        *facets = f;
        wf_richtext_facet *cur = &(*facets)[*count];
        cur->byte_start = i;
        cur->byte_end = handle_end;

        cur->features = malloc(sizeof(wf_richtext_feature));
        if (cur->features) {
            cur->feature_count = 1;
            cur->features[0].type = WF_RICHTEXT_FEATURE_MENTION;
            memset(cur->features[0].did, 0, sizeof(cur->features[0].did));
        }
        (*count)++;
        i = handle_end - 1;
    }
}

/* ── URL detection ── */

static int is_url_char(char c) {
    return isgraph((unsigned char)c) && c != '>' && c != ']' && c != '}';
}

static int is_domain_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '.';
}

static size_t strip_trailing_punct(const char *s, size_t len) {
    while (len > 0) {
        char c = s[len - 1];
        if (c == '.' || c == ',' || c == ';' || c == ':' ||
            c == '!' || c == '?' || c == ')') {
            if (c == ')') {
                int has_open = 0;
                for (size_t j = 0; j < len - 1; j++)
                    if (s[j] == '(') { has_open = 1; break; }
                if (!has_open) { len--; continue; }
            }
            len--;
        } else break;
    }
    return len;
}

static void detect_links(const char *text, size_t text_len,
                         wf_richtext_facet **facets, size_t *count) {
    for (size_t i = 0; i < text_len; i++) {
        if (is_scheme(text + i, text_len - i)) {
            size_t url_end = i;
            while (url_end < text_len && is_url_char(text[url_end]))
                url_end++;
            size_t stripped = strip_trailing_punct(text + i, url_end - i);

            if (stripped > 0) {
                wf_richtext_facet *f = realloc(*facets, (*count + 1) * sizeof(wf_richtext_facet));
                if (!f) return;
                *facets = f;
                wf_richtext_facet *cur = &(*facets)[*count];
                cur->byte_start = i;
                cur->byte_end = i + stripped;
                cur->features = malloc(sizeof(wf_richtext_feature));
                if (cur->features) {
                    cur->feature_count = 1;
                    cur->features[0].type = WF_RICHTEXT_FEATURE_LINK;
                    size_t ulen = stripped < sizeof(cur->features[0].uri) - 1 ? stripped : sizeof(cur->features[0].uri) - 1;
                    memcpy(cur->features[0].uri, text + i, ulen);
                    cur->features[0].uri[ulen] = '\0';
                }
                (*count)++;
                i = cur->byte_end - 1;
            }
            continue;
        }

        /* Bare domain detection */
        if (!is_word_boundary(i > 0 ? text[i-1] : '\0') && i > 0)
            continue;

        size_t dstart = i;
        size_t dend = dstart;
        while (dend < text_len && is_domain_char(text[dend]))
            dend++;
        if (dend <= dstart + 2) continue;

        char domain[1024];
        size_t dlen = dend - dstart;
        if (dlen >= sizeof(domain)) continue;
        memcpy(domain, text + dstart, dlen);
        domain[dlen] = '\0';

        if (!wf_richtext_is_valid_domain(domain))
            continue;

        size_t url_end = dend;
        while (url_end < text_len && is_url_char(text[url_end]))
            url_end++;
        size_t stripped = strip_trailing_punct(text + dstart, url_end - dstart);

        wf_richtext_facet *f = realloc(*facets, (*count + 1) * sizeof(wf_richtext_facet));
        if (!f) return;
        *facets = f;
        wf_richtext_facet *cur = &(*facets)[*count];
        cur->byte_start = dstart;
        cur->byte_end = dstart + stripped;
            cur->features = malloc(sizeof(wf_richtext_feature));
        if (cur->features) {
            cur->feature_count = 1;
            cur->features[0].type = WF_RICHTEXT_FEATURE_LINK;
            if (stripped + 8 < sizeof(cur->features[0].uri)) {
                memcpy(cur->features[0].uri, "https://", 8);
                memcpy(cur->features[0].uri + 8, text + dstart, stripped);
                cur->features[0].uri[stripped + 8] = '\0';
            }
        }
        (*count)++;
        i = cur->byte_end - 1;
    }
}

/* ── TAG detection ── */

/* is_zero_width and is_tag_start not needed — handled inline */

/* simplified: checks for full-width hash U+FF03 which is EF BC 83 in UTF-8 */
static int is_fullwidth_hash(const unsigned char *s, size_t remaining) {
    return remaining >= 3 && s[0] == 0xef && s[1] == 0xbc && s[2] == 0x83;
}

static int is_tag_body_char(unsigned char c) {
    return !isspace(c) && c != '\\' && c != '"' && c != '\'';
}

static void detect_tags(const char *text, size_t text_len,
                        wf_richtext_facet **facets, size_t *count) {
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] != '#' && !is_fullwidth_hash((const unsigned char *)text + i, text_len - i))
            continue;

        if (i > 0 && !is_word_boundary(text[i - 1]))
            continue;

        size_t hlen = (text[i] == '#') ? 1 : 3;
        size_t tstart = i + hlen;
        size_t tend = tstart;
        while (tend < text_len && is_tag_body_char((unsigned char)text[tend]))
            tend++;

        if (tend == tstart) continue;

        char tag[640];
        size_t tlen = tend - tstart;
        if (tlen >= sizeof(tag)) continue;
        memcpy(tag, text + tstart, tlen);
        tag[tlen] = '\0';

        /* Tag must contain at least one non-digit, non-space, non-punctuation
         * code point (matches upstream [^\d\s\p{P}…]+ requirement). */
        int has_content = 0;
        for (size_t j = 0; j < tlen; j++) {
            unsigned char c = (unsigned char)tag[j];
            if (c >= 0x80) { has_content = 1; break; }
            if (isalpha((unsigned char)c)) { has_content = 1; break; }
        }
        if (!has_content) continue;

        /* Strip trailing punctuation from both the stored tag and the range,
         * matching upstream's TRAILING_PUNCTUATION_REGEX behaviour. */
        size_t stripped = strip_trailing_punct(tag, tlen);
        if (stripped == 0) continue;
        tlen = stripped;
        tend = tstart + tlen;

        size_t glen = wf_richtext_grapheme_len(tag, tlen);
        if (glen > 64) continue;

        wf_richtext_facet *f = realloc(*facets, (*count + 1) * sizeof(wf_richtext_facet));
        if (!f) return;
        *facets = f;
        wf_richtext_facet *cur = &(*facets)[*count];
        cur->byte_start = i;
        cur->byte_end = tend;
        cur->features = malloc(sizeof(wf_richtext_feature));
        if (cur->features) {
            cur->feature_count = 1;
            cur->features[0].type = WF_RICHTEXT_FEATURE_TAG;
            size_t cplen = tlen < sizeof(cur->features[0].tag) - 1 ? tlen : sizeof(cur->features[0].tag) - 1;
            memcpy(cur->features[0].tag, tag, cplen);
            cur->features[0].tag[cplen] = '\0';
        }
        (*count)++;
        i = tend - 1;
    }
}

/* ── CASHTAG detection ── */

static void detect_cashtags(const char *text, size_t text_len,
                            wf_richtext_facet **facets, size_t *count) {
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] != '$') continue;
        if (i > 0 && !is_word_boundary(text[i - 1]) && text[i - 1] != '(')
            continue;

        size_t end = i + 1;
        while (end < text_len && end - i - 1 < 5 && isalnum((unsigned char)text[end]))
            end++;

        size_t tlen = end - i - 1;
        if (tlen < 1 || tlen > 5) continue;
        if (!isalpha((unsigned char)text[i + 1])) continue;

        if (end < text_len && !is_word_boundary(text[end]) &&
            text[end] != '.' && text[end] != ',' && text[end] != ';' &&
            text[end] != ':' && text[end] != '!' && text[end] != '?' &&
            text[end] != ')' && text[end] != '"' && text[end] != '\'')
            continue;

        wf_richtext_facet *f = realloc(*facets, (*count + 1) * sizeof(wf_richtext_facet));
        if (!f) return;
        *facets = f;
        wf_richtext_facet *cur = &(*facets)[*count];
        cur->byte_start = i;
        cur->byte_end = end;
        cur->features = malloc(sizeof(wf_richtext_feature));
        if (cur->features) {
            cur->feature_count = 1;
            cur->features[0].type = WF_RICHTEXT_FEATURE_TAG;
            cur->features[0].tag[0] = '$';
            for (size_t j = 0; j < tlen && j + 1 < sizeof(cur->features[0].tag) - 1; j++)
                cur->features[0].tag[j + 1] = toupper((unsigned char)text[i + 1 + j]);
            cur->features[0].tag[tlen + 1] = '\0';
        }
        (*count)++;
        i = end - 1;
    }
}

/* ── facet sort helper ── */

static int facet_cmp(const void *a, const void *b) {
    const wf_richtext_facet *fa = (const wf_richtext_facet *)a;
    const wf_richtext_facet *fb = (const wf_richtext_facet *)b;
    if (fa->byte_start < fb->byte_start) return -1;
    if (fa->byte_start > fb->byte_start) return 1;
    return 0;
}

/* ── public detect wrapper ── */

wf_status wf_richtext_detect_facets(wf_richtext *rt) {
    if (!rt || !rt->text) return WF_ERR_INVALID_ARG;

    /* Free existing facets */
    for (size_t i = 0; i < rt->facet_count; i++)
        free(rt->facets[i].features);
    free(rt->facets);
    rt->facets = NULL;
    rt->facet_count = 0;

    detect_mentions(rt->text, rt->text_len, &rt->facets, &rt->facet_count);
    detect_links(rt->text, rt->text_len, &rt->facets, &rt->facet_count);
    detect_tags(rt->text, rt->text_len, &rt->facets, &rt->facet_count);
    detect_cashtags(rt->text, rt->text_len, &rt->facets, &rt->facet_count);

    /* Sort by byte_start */
    if (rt->facet_count > 1) {
        qsort(rt->facets, rt->facet_count, sizeof(wf_richtext_facet), facet_cmp);
    }

    return WF_OK;
}
