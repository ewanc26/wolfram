#include "wolfram/richtext.h"
#include "wolfram/xrpc.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

/* ── UTF-8 utilities ── */

size_t wf_richtext_grapheme_len(const char *utf8_text, size_t utf8_len) {
    size_t count = 0;
    size_t i = 0;
    while (i < utf8_len) {
        unsigned char c = (unsigned char)utf8_text[i];
        if (c <= 0x7f)
            i += 1;
        else if (c <= 0xdf)
            i += 2;
        else if (c <= 0xef)
            i += 3;
        else if (c <= 0xf7)
            i += 4;
        else
            i++;
        count++;
    }
    return count;
}

/* ── Internal helpers ── */

static char *wf_richtext_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = std::strlen(s);
    char *copy = (char *)std::malloc(len + 1);
    if (copy) std::memcpy(copy, s, len + 1);
    return copy;
}

static void wf_richtext_free_facet_array(wf_richtext_facet *facets,
                                         size_t count) {
    if (!facets) return;
    for (size_t i = 0; i < count; i++) std::free(facets[i].features);
    std::free(facets);
}

static void wf_richtext_facet_copy(wf_richtext_facet *dst,
                                   const wf_richtext_facet &src) {
    dst->byte_start = src.byte_start;
    dst->byte_end = src.byte_end;
    dst->feature_count = src.feature_count;
    if (src.feature_count > 0 && src.features) {
        dst->features = (wf_richtext_feature *)std::malloc(
            src.feature_count * sizeof(wf_richtext_feature));
        if (dst->features) {
            std::memcpy(dst->features, src.features,
                        src.feature_count * sizeof(wf_richtext_feature));
        }
    } else {
        dst->features = NULL;
    }
}

/* ── richtext lifecycle ── */

wf_status wf_richtext_init(wf_richtext *rt, const char *text) {
    if (!rt || !text) return WF_ERR_INVALID_ARG;
    rt->text = wf_richtext_strdup(text);
    if (!rt->text) return WF_ERR_ALLOC;
    rt->text_len = std::strlen(rt->text);
    rt->facets = NULL;
    rt->facet_count = 0;
    rt->owns_text = 1;
    return WF_OK;
}

void wf_richtext_free(wf_richtext *rt) {
    if (!rt) return;
    if (rt->owns_text) std::free(rt->text);
    wf_richtext_free_facet_array(rt->facets, rt->facet_count);
    std::memset(rt, 0, sizeof(*rt));
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

wf_richtext_segment wf_richtext_get_segment(const wf_richtext *rt,
                                            size_t index) {
    wf_richtext_segment seg = {};
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
            if (flen > 0 && !is_whitespace_only(
                                rt->text + rt->facets[i].byte_start, flen)) {
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

wf_status wf_richtext_insert(wf_richtext *rt, uint32_t offset,
                             const char *text) {
    if (!rt || !text || offset > rt->text_len) return WF_ERR_INVALID_ARG;
    std::string s(rt->text, rt->text_len);
    size_t ilen = std::strlen(text);
    s.insert(offset, text, ilen);

    char *new_text = wf_richtext_strdup(s.c_str());
    if (!new_text) return WF_ERR_ALLOC;

    if (rt->owns_text) std::free(rt->text);
    rt->text = new_text;
    rt->text_len = s.size();
    rt->owns_text = 1;

    for (size_t i = 0; i < rt->facet_count; i++) {
        wf_richtext_facet *f = &rt->facets[i];
        if (offset <= f->byte_start) f->byte_start += (uint32_t)ilen;
        if (offset < f->byte_end) f->byte_end += (uint32_t)ilen;
    }

    return WF_OK;
}

/* ── mutation: delete ── */

wf_status wf_richtext_delete(wf_richtext *rt, uint32_t start, uint32_t end) {
    if (!rt || start >= end || end > rt->text_len) return WF_ERR_INVALID_ARG;
    size_t removed = end - start;

    std::string s(rt->text, rt->text_len);
    s.erase(start, removed);

    char *new_text = wf_richtext_strdup(s.c_str());
    if (!new_text) return WF_ERR_ALLOC;

    if (rt->owns_text) std::free(rt->text);
    rt->text = new_text;
    rt->text_len = s.size();
    rt->owns_text = 1;

    size_t write_idx = 0;
    for (size_t i = 0; i < rt->facet_count; i++) {
        wf_richtext_facet *f = &rt->facets[i];
        if (start <= f->byte_start && end >= f->byte_end) {
            f->byte_start = 0;
            f->byte_end = 0;
        } else if (start > f->byte_end) {
        } else if (start > f->byte_start && start <= f->byte_end &&
                   end > f->byte_end) {
            f->byte_end = start;
        } else if (start >= f->byte_start && end <= f->byte_end) {
            f->byte_end -= (uint32_t)removed;
        } else if (start < f->byte_start && end >= f->byte_start &&
                   end <= f->byte_end) {
            f->byte_start = start;
            f->byte_end -= (uint32_t)removed;
        } else if (end < f->byte_start) {
            f->byte_start -= (uint32_t)removed;
            f->byte_end -= (uint32_t)removed;
        }

        if (f->byte_start < f->byte_end) {
            if (write_idx < i) rt->facets[write_idx] = rt->facets[i];
            write_idx++;
        } else {
            std::free(f->features);
        }
    }
    rt->facet_count = write_idx;

    return WF_OK;
}

/* ── sanitization ── */

static int wf_richtext_is_sanitize_newline(char c) {
    return c == '\r' || c == '\n';
}

static int wf_richtext_skip_sanitize_separator(const char *text, size_t len,
                                               size_t *index) {
    if (!text || !index || *index >= len) return 0;

    switch ((unsigned char)text[*index]) {
        case ' ':
        case '\t':
        case '\v':
        case '\f':
            (*index)++;
            return 1;
        case 0xC2:
            if (*index + 1 < len && (unsigned char)text[*index + 1] == 0xAD) {
                *index += 2;
                return 1;
            }
            break;
        case 0xE2:
            if (*index + 2 < len) {
                unsigned char b1 = (unsigned char)text[*index + 1];
                unsigned char b2 = (unsigned char)text[*index + 2];
                if (b1 == 0x81 && b2 == 0xA0) {
                    *index += 3;
                    return 1;
                }
                if (b1 == 0x80 && (b2 == 0x8D || b2 == 0x8C || b2 == 0x8B)) {
                    *index += 3;
                    return 1;
                }
            }
            break;
    }

    return 0;
}

static size_t wf_richtext_find_newline_run_end(const char *text, size_t len,
                                               size_t start) {
    if (!text || start >= len || !wf_richtext_is_sanitize_newline(text[start]))
        return 0;

    size_t pos = start + 1;
    size_t newline_count = 1;

    while (pos < len) {
        while (wf_richtext_skip_sanitize_separator(text, len, &pos)) {
        }
        if (pos >= len || !wf_richtext_is_sanitize_newline(text[pos])) break;
        newline_count++;
        pos++;
    }

    return newline_count >= 3 ? pos : 0;
}

wf_status wf_richtext_sanitize(wf_richtext *rt, int clean_newlines) {
    if (!rt) return WF_ERR_INVALID_ARG;
    if (!clean_newlines || rt->text_len == 0) return WF_OK;
    if (!rt->text) return WF_ERR_INVALID_ARG;

    size_t i = 0;
    while (i < rt->text_len) {
        size_t match_end =
            wf_richtext_find_newline_run_end(rt->text, rt->text_len, i);
        if (match_end > i) {
            wf_status status =
                wf_richtext_delete(rt, (uint32_t)i, (uint32_t)match_end);
            if (status != WF_OK) return status;
            status = wf_richtext_insert(rt, (uint32_t)i, "\n\n");
            if (status != WF_OK) return status;
            i += 2;
            continue;
        }
        i++;
    }

    return WF_OK;
}

/* ── domain/TLD validation helpers ── */

static const char *known_tlds[] = {
    "com",   "org",    "net",    "edu",  "gov",   "mil",  "int",  "uk",
    "de",    "jp",     "fr",     "au",   "us",    "ca",   "ch",   "it",
    "nl",    "se",     "no",     "dk",   "fi",    "es",   "at",   "be",
    "pl",    "br",     "in",     "cn",   "ru",    "za",   "mx",   "kr",
    "sg",    "hk",     "io",     "app",  "dev",   "co",   "me",   "xyz",
    "info",  "biz",    "name",   "pro",  "tv",    "cc",   "ws",   "cloud",
    "tech",  "site",   "online", "club", "world", "life", "blog", "design",
    "tools", "social", "video",  "wiki", "media", "news", "test", NULL};

int wf_richtext_is_valid_tld(const char *tld) {
    if (!tld || !*tld) return 0;
    for (int i = 0; known_tlds[i]; i++)
        if (strcasecmp(tld, known_tlds[i]) == 0) return 1;
    return 0;
}

int wf_richtext_is_valid_domain(const char *domain) {
    if (!domain || !*domain) return 0;
    const char *dot = std::strrchr(domain, '.');
    if (!dot || dot == domain) return 0;
    return wf_richtext_is_valid_tld(dot + 1);
}

/* ── facet detection (RAII-based, no allocation-leak paths) ── */

static int is_word_boundary(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' ||
           c == '\0';
}

static int is_scheme(const char *s, size_t len) {
    if (len < 8) return 0;
    if (!(s[0] == 'h' && s[1] == 't' && s[2] == 't' && s[3] == 'p')) return 0;
    size_t p = 4;
    if (s[4] == 's') p = 5;
    return s[p] == ':' && s[p + 1] == '/' && s[p + 2] == '/';
}

static int is_valid_mention_char(char c) {
    return std::isalnum((unsigned char)c) || c == '.' || c == '-';
}

static int is_url_char(char c) {
    return std::isgraph((unsigned char)c) && c != '>' && c != ']' && c != '}';
}

static int is_domain_char(char c) {
    return std::isalnum((unsigned char)c) || c == '-' || c == '.';
}

static size_t strip_trailing_punct(const char *s, size_t len) {
    while (len > 0) {
        char c = s[len - 1];
        if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' ||
            c == '?' || c == ')') {
            if (c == ')') {
                int has_open = 0;
                for (size_t j = 0; j < len - 1; j++)
                    if (s[j] == '(') {
                        has_open = 1;
                        break;
                    }
                if (!has_open) {
                    len--;
                    continue;
                }
            }
            len--;
        } else
            break;
    }
    return len;
}

static int is_fullwidth_hash(const unsigned char *s, size_t remaining) {
    return remaining >= 3 && s[0] == 0xef && s[1] == 0xbc && s[2] == 0x83;
}

static int is_tag_body_char(unsigned char c) {
    return !std::isspace(c) && c != '\\' && c != '"' && c != '\'';
}

static void detect_mentions(const char *text, size_t text_len,
                            std::vector<wf_richtext_facet> &facets) {
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] != '@') continue;
        if (i > 0 && !is_word_boundary(text[i - 1])) continue;

        size_t start = i + 1;
        size_t end = start;
        while (end < text_len && is_valid_mention_char(text[end])) end++;

        if (end == start) continue;

        char domain[1024];
        size_t dlen = end - start;
        if (dlen >= sizeof(domain)) continue;
        std::memcpy(domain, text + start, dlen);
        domain[dlen] = '\0';

        if (end < text_len && std::isalnum((unsigned char)text[end])) continue;

        if (!wf_richtext_is_valid_domain(domain)) continue;

        size_t handle_end = end;

        wf_richtext_facet f = {};
        f.byte_start = (uint32_t)i;
        f.byte_end = (uint32_t)handle_end;
        f.features =
            (wf_richtext_feature *)std::malloc(sizeof(wf_richtext_feature));
        if (f.features) {
            f.feature_count = 1;
            f.features[0].type = WF_RICHTEXT_FEATURE_MENTION;
            std::memset(f.features[0].did, 0, sizeof(f.features[0].did));
            facets.push_back(f);
        }
        i = handle_end - 1;
    }
}

static void detect_links(const char *text, size_t text_len,
                         std::vector<wf_richtext_facet> &facets) {
    for (size_t i = 0; i < text_len; i++) {
        if (is_scheme(text + i, text_len - i)) {
            size_t url_end = i;
            while (url_end < text_len && is_url_char(text[url_end])) url_end++;
            size_t stripped = strip_trailing_punct(text + i, url_end - i);

            if (stripped > 0) {
                wf_richtext_facet f = {};
                f.byte_start = (uint32_t)i;
                f.byte_end = (uint32_t)(i + stripped);
                f.features = (wf_richtext_feature *)std::malloc(
                    sizeof(wf_richtext_feature));
                if (f.features) {
                    f.feature_count = 1;
                    f.features[0].type = WF_RICHTEXT_FEATURE_LINK;
                    size_t ulen = stripped < sizeof(f.features[0].uri) - 1
                                      ? stripped
                                      : sizeof(f.features[0].uri) - 1;
                    std::memcpy(f.features[0].uri, text + i, ulen);
                    f.features[0].uri[ulen] = '\0';
                    facets.push_back(f);
                }
                i = f.byte_end - 1;
            }
            continue;
        }

        if (!is_word_boundary(i > 0 ? text[i - 1] : '\0') && i > 0) continue;

        size_t dstart = i;
        size_t dend = dstart;
        while (dend < text_len && is_domain_char(text[dend])) dend++;
        if (dend <= dstart + 2) continue;

        char domain[1024];
        size_t dlen = dend - dstart;
        if (dlen >= sizeof(domain)) continue;
        std::memcpy(domain, text + dstart, dlen);
        domain[dlen] = '\0';

        if (!wf_richtext_is_valid_domain(domain)) continue;

        size_t url_end = dend;
        while (url_end < text_len && is_url_char(text[url_end])) url_end++;
        size_t stripped = strip_trailing_punct(text + dstart, url_end - dstart);

        if (stripped > 0) {
            wf_richtext_facet f = {};
            f.byte_start = (uint32_t)dstart;
            f.byte_end = (uint32_t)(dstart + stripped);
            f.features =
                (wf_richtext_feature *)std::malloc(sizeof(wf_richtext_feature));
            if (f.features) {
                f.feature_count = 1;
                f.features[0].type = WF_RICHTEXT_FEATURE_LINK;
                if (stripped + 8 < sizeof(f.features[0].uri)) {
                    std::memcpy(f.features[0].uri, "https://", 8);
                    std::memcpy(f.features[0].uri + 8, text + dstart, stripped);
                    f.features[0].uri[stripped + 8] = '\0';
                }
                facets.push_back(f);
            }
            i = f.byte_end - 1;
        }
    }
}

static void detect_tags(const char *text, size_t text_len,
                        std::vector<wf_richtext_facet> &facets) {
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] != '#' &&
            !is_fullwidth_hash((const unsigned char *)text + i, text_len - i))
            continue;

        if (i > 0 && !is_word_boundary(text[i - 1])) continue;

        size_t hlen = (text[i] == '#') ? 1 : 3;
        size_t tstart = i + hlen;
        size_t tend = tstart;
        while (tend < text_len && is_tag_body_char((unsigned char)text[tend]))
            tend++;

        if (tend == tstart) continue;

        char tag[640];
        size_t tlen = tend - tstart;
        if (tlen >= sizeof(tag)) continue;
        std::memcpy(tag, text + tstart, tlen);
        tag[tlen] = '\0';

        int has_content = 0;
        for (size_t j = 0; j < tlen; j++) {
            unsigned char c = (unsigned char)tag[j];
            if (c >= 0x80) {
                has_content = 1;
                break;
            }
            if (std::isalpha(c)) {
                has_content = 1;
                break;
            }
        }
        if (!has_content) continue;

        size_t stripped = strip_trailing_punct(tag, tlen);
        if (stripped == 0) continue;
        tlen = stripped;
        tend = tstart + tlen;

        size_t glen = wf_richtext_grapheme_len(tag, tlen);
        if (glen > 64) continue;

        wf_richtext_facet f = {};
        f.byte_start = (uint32_t)i;
        f.byte_end = (uint32_t)tend;
        f.features =
            (wf_richtext_feature *)std::malloc(sizeof(wf_richtext_feature));
        if (f.features) {
            f.feature_count = 1;
            f.features[0].type = WF_RICHTEXT_FEATURE_TAG;
            size_t cplen = tlen < sizeof(f.features[0].tag) - 1
                               ? tlen
                               : sizeof(f.features[0].tag) - 1;
            std::memcpy(f.features[0].tag, tag, cplen);
            f.features[0].tag[cplen] = '\0';
            facets.push_back(f);
        }
        i = tend - 1;
    }
}

static void detect_cashtags(const char *text, size_t text_len,
                            std::vector<wf_richtext_facet> &facets) {
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] != '$') continue;
        if (i > 0 && !is_word_boundary(text[i - 1]) && text[i - 1] != '(')
            continue;

        size_t end = i + 1;
        while (end < text_len && end - i - 1 < 5 &&
               std::isalnum((unsigned char)text[end]))
            end++;

        size_t tlen = end - i - 1;
        if (tlen < 1 || tlen > 5) continue;
        if (!std::isalpha((unsigned char)text[i + 1])) continue;

        if (end < text_len && !is_word_boundary(text[end]) &&
            text[end] != '.' && text[end] != ',' && text[end] != ';' &&
            text[end] != ':' && text[end] != '!' && text[end] != '?' &&
            text[end] != ')' && text[end] != '"' && text[end] != '\'')
            continue;

        wf_richtext_facet f = {};
        f.byte_start = (uint32_t)i;
        f.byte_end = (uint32_t)end;
        f.features =
            (wf_richtext_feature *)std::malloc(sizeof(wf_richtext_feature));
        if (f.features) {
            f.feature_count = 1;
            f.features[0].type = WF_RICHTEXT_FEATURE_TAG;
            f.features[0].tag[0] = '$';
            for (size_t j = 0;
                 j < tlen && j + 1 < sizeof(f.features[0].tag) - 1; j++)
                f.features[0].tag[j + 1] =
                    std::toupper((unsigned char)text[i + 1 + j]);
            f.features[0].tag[tlen + 1] = '\0';
            facets.push_back(f);
        }
        i = end - 1;
    }
}

static int facet_cmp(const void *a, const void *b) {
    const wf_richtext_facet *fa = (const wf_richtext_facet *)a;
    const wf_richtext_facet *fb = (const wf_richtext_facet *)b;
    if (fa->byte_start < fb->byte_start) return -1;
    if (fa->byte_start > fb->byte_start) return 1;
    return 0;
}

wf_status wf_richtext_detect_facets(wf_richtext *rt) {
    if (!rt || !rt->text) return WF_ERR_INVALID_ARG;

    wf_richtext_free_facet_array(rt->facets, rt->facet_count);
    rt->facets = NULL;
    rt->facet_count = 0;

    std::vector<wf_richtext_facet> facets;

    detect_mentions(rt->text, rt->text_len, facets);
    detect_links(rt->text, rt->text_len, facets);
    detect_tags(rt->text, rt->text_len, facets);
    detect_cashtags(rt->text, rt->text_len, facets);

    if (facets.size() > 1) {
        std::sort(facets.begin(), facets.end(),
                  [](const wf_richtext_facet &a, const wf_richtext_facet &b) {
                      return a.byte_start < b.byte_start;
                  });
    }

    rt->facet_count = facets.size();
    if (rt->facet_count > 0) {
        rt->facets = (wf_richtext_facet *)std::malloc(
            rt->facet_count * sizeof(wf_richtext_facet));
        if (!rt->facets) {
            rt->facet_count = 0;
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < rt->facet_count; i++) {
            wf_richtext_facet_copy(&rt->facets[i], facets[i]);
            std::free(facets[i].features);
        }
    }

    return WF_OK;
}
