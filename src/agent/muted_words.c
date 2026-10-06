/*
 * muted_words.c -- see wolfram/muted_words.h.
 */

#include "wolfram/muted_words.h"

#include "wolfram/time.h"

#include <string.h>

static bool is_word_byte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c >= 0x80;
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool ci_equal(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b)) return false;
    return *a == *b;
}

static bool word_is_plain(const char *w) {
    for (; *w; w++)
        if (!is_word_byte((unsigned char)*w)) return false;
    return true;
}

static bool contains(const char *hay, const char *needle, bool whole_word) {
    const size_t n = strlen(needle);
    if (n == 0) return false;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && lower(p[i]) == lower(needle[i])) i++;
        if (i != n) continue;
        if (whole_word) {
            if (p != hay && is_word_byte((unsigned char)p[-1])) continue;
            if (is_word_byte((unsigned char)p[n])) continue;
        }
        return true;
    }
    return false;
}

static bool has_target(const wf_actor_pref_muted_word *w, const char *name) {
    for (size_t i = 0; i < w->target_count; i++)
        if (w->targets && w->targets[i] && strcmp(w->targets[i], name) == 0)
            return true;
    return false;
}

bool wf_muted_words_match(const wf_actor_pref_muted_word *words, size_t count,
                          const char *text, const char *const *tags,
                          size_t tag_count, bool author_followed, int64_t now) {
    if (!words) return false;
    for (size_t i = 0; i < count; i++) {
        const wf_actor_pref_muted_word *w = &words[i];
        if (!w->value || !w->value[0]) continue;
        if (w->actor_target &&
            strcmp(w->actor_target, "exclude-following") == 0 &&
            author_followed)
            continue;
        if (now != 0 && w->expires_at) {
            int64_t exp;
            if (wf_time_parse_rfc3339(w->expires_at, &exp) == WF_OK &&
                exp <= now)
                continue;
        }
        if (text && has_target(w, "content") &&
            contains(text, w->value, word_is_plain(w->value)))
            return true;
        if (tags && has_target(w, "tag")) {
            const char *v = w->value[0] == '#' ? w->value + 1 : w->value;
            for (size_t t = 0; t < tag_count; t++) {
                const char *tag = tags[t];
                if (!tag) continue;
                if (tag[0] == '#') tag++;
                if (ci_equal(tag, v)) return true;
            }
        }
    }
    return false;
}
