/*
 * muted_words.c -- see wolfram/muted_words.h.
 *
 * The matching rules are wf_mod_match_mute_words's (moderation.h), a port of
 * the official client's matcher. This file only adapts the account preferences'
 * mutedWord (targets as strings, `expiresAt` as a lexicon datetime) to it, so
 * there is one set of rules: it applies `now` to the expiry, sends content
 * words against the text and tag words against the tags, and frees nothing it
 * did not allocate.
 */

#include "wolfram/muted_words.h"

#include "wolfram/moderation.h"
#include "wolfram/time.h"

#include <stdlib.h>
#include <string.h>

static bool has_target(const wf_actor_pref_muted_word *w, const char *name) {
    for (size_t i = 0; i < w->target_count; i++)
        if (w->targets && w->targets[i] && strcmp(w->targets[i], name) == 0)
            return true;
    return false;
}

/* An expiry in the past, judged at `now` rather than the system clock: the
 * consoles' clocks are not trusted, and 0 means unknown, so nothing expires. An
 * expiry that does not parse is treated as not expired, the safe direction. */
static bool expired(const char *expires_at, int64_t now) {
    int64_t exp;
    if (now == 0 || !expires_at || !expires_at[0]) return false;
    return wf_time_parse_rfc3339(expires_at, &exp) == WF_OK && exp <= now;
}

/* Run the official matcher over the words that pass `want`. */
static bool match_words(const wf_actor_pref_muted_word *words, size_t count,
                        const char *want_target, const char *text,
                        const char *const *tags, size_t tag_count,
                        bool author_followed, int64_t now) {
    wf_mod_muted_word *mods = calloc(count ? count : 1, sizeof *mods);
    wf_mod_mute_word_match *matches = NULL;
    size_t n = 0, match_count = 0;
    bool hit = false;
    size_t i;

    if (!mods) return false;
    for (i = 0; i < count; i++) {
        const wf_actor_pref_muted_word *w = &words[i];
        if (!w->value || !w->value[0] || !has_target(w, want_target)) continue;
        if (expired(w->expires_at, now)) continue;
        mods[n].value = w->value;
        mods[n].actor_target = w->actor_target;
        mods[n].targets_content = 1;
        mods[n].targets_tag = 1;
        n++;
    }
    if (n > 0 && wf_mod_match_mute_words(&matches, &match_count, mods, n, text,
                                         tags, tag_count, NULL,
                                         author_followed ? 1 : 0) == WF_OK) {
        hit = match_count > 0;
        wf_mod_mute_word_matches_free(matches, match_count);
    }
    free(mods);
    return hit;
}

bool wf_muted_words_match(const wf_actor_pref_muted_word *words, size_t count,
                          const char *text, const char *const *tags,
                          size_t tag_count, bool author_followed, int64_t now) {
    if (!words || count == 0) return false;
    /* Content words see only the text. The official matcher also checks tags
     * for every word it is given, so tag words are matched separately, against
     * the tags and an empty text, and content words against no tags. */
    if (text && match_words(words, count, "content", text, NULL, 0,
                            author_followed, now))
        return true;
    if (tags && tag_count > 0) {
        size_t i;
        /* A tag value may be written with or without the leading '#', on either
         * side; hand the matcher both forms stripped. */
        const char **plain = calloc(tag_count, sizeof *plain);
        wf_actor_pref_muted_word *copy = calloc(count, sizeof *copy);
        bool hit = false;
        if (plain && copy) {
            for (i = 0; i < tag_count; i++)
                plain[i] = tags[i] && tags[i][0] == '#' ? tags[i] + 1 : tags[i];
            for (i = 0; i < count; i++) {
                copy[i] = words[i];
                if (copy[i].value && copy[i].value[0] == '#') copy[i].value++;
            }
            hit = match_words(copy, count, "tag", "", plain, tag_count,
                              author_followed, now);
        }
        free(plain);
        free(copy);
        if (hit) return true;
    }
    return false;
}

void wf_muted_list_clear(wf_muted_list *list) {
    if (list) memset(list, 0, sizeof *list);
}

bool wf_muted_list_add(wf_muted_list *list, const char *value, bool content,
                       bool tag, bool exclude_following,
                       const char *expires_at) {
    wf_muted_entry *w;
    if (!list || !value || !value[0] || list->count >= WF_MUTED_LIST_MAX)
        return false;
    w = &list->words[list->count++];
    memset(w, 0, sizeof *w);
    strncpy(w->value, value, sizeof w->value - 1);
    w->content = content || !tag;
    w->tag = tag;
    w->exclude_following = exclude_following;
    if (expires_at)
        strncpy(w->expires_at, expires_at, sizeof w->expires_at - 1);
    return true;
}

void wf_muted_list_from_prefs(wf_muted_list *list,
                              const wf_actor_preferences *prefs, int64_t now) {
    size_t i;
    wf_muted_list_clear(list);
    if (!list || !prefs) return;
    list->now = now;
    for (i = 0; i < prefs->muting_keyword_count; i++) {
        const wf_actor_pref_muted_word *w = &prefs->muting_keywords[i];
        if (!w->value) continue;
        wf_muted_list_add(list, w->value, has_target(w, "content"),
                          has_target(w, "tag"),
                          w->actor_target &&
                              strcmp(w->actor_target, "exclude-following") == 0,
                          w->expires_at);
    }
}

bool wf_muted_list_match(const wf_muted_list *list, const char *text,
                         const char *const *tags, size_t tag_count,
                         bool author_followed) {
    wf_actor_pref_muted_word words[WF_MUTED_LIST_MAX];
    char *targets[WF_MUTED_LIST_MAX][2];
    char content_target[] = "content";
    char tag_target[] = "tag";
    char exclude[] = "exclude-following";
    size_t i;

    if (!list || list->count == 0) return false;
    memset(words, 0, sizeof words);
    for (i = 0; i < list->count; i++) {
        const wf_muted_entry *e = &list->words[i];
        words[i].value = (char *)e->value;
        words[i].expires_at = e->expires_at[0] ? (char *)e->expires_at : NULL;
        words[i].actor_target = e->exclude_following ? exclude : NULL;
        if (e->content) targets[i][words[i].target_count++] = content_target;
        if (e->tag) targets[i][words[i].target_count++] = tag_target;
        words[i].targets = targets[i];
    }
    return wf_muted_words_match(words, list->count, text, tags, tag_count,
                                author_followed, list->now);
}
