/*
 * muted_words.h -- muted-word matching for the account's `mutedWordsPref`.
 *
 * The lexicon (app.bsky.actor.defs#mutedWord) gives each word a value, targets
 * (`content`, `tag`), an optional `actorTarget` (`all` by default, or
 * `exclude-following`) and an optional `expiresAt`. Cobalt and Indigo each
 * implemented this by copy, without `actorTarget` or `expiresAt`; this is the
 * one implementation.
 *
 * The rules are wf_mod_match_mute_words's (moderation.h): a port of the
 * official client's matcher, so Cobalt, Indigo and the moderation code agree.
 * This adapts the preferences' mutedWord to it. It allocates a little on each
 * call and builds on every target.
 *
 * Rules:
 *  - Matching is case-insensitive. A single word matches whole words (ignoring
 *    punctuation at its ends: "cat." matches "cat", "cat's" does not); a phrase
 *    or a word with punctuation in it, and one character, match as a substring.
 *  - An empty value never matches.
 *  - `content` targets match the post text; `tag` targets match a hashtag
 *    exactly (case-insensitive), with or without a leading '#' on either side.
 *  - `exclude-following` words do not apply to accounts the viewer follows.
 *  - A word whose `expiresAt` has passed at `now` does not apply. An
 *    `expiresAt` that does not parse is treated as not expired (the safe
 *    direction for a mute), and `now` of 0 (clock unknown) expires nothing.
 */

#ifndef WOLFRAM_MUTED_WORDS_H
#define WOLFRAM_MUTED_WORDS_H

#include "wolfram/actor_prefs_typed.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True if any word in `words` mutes a post with this `text` and these hashtag
 * `tags` (without '#', or with it) at time `now` (Unix seconds; 0 means the
 * clock is unknown and nothing is treated as expired). `author_followed` says
 * whether the viewer follows the post's author. `text` and `tags` may be NULL
 * (`tag_count` is then ignored). */
bool wf_muted_words_match(const wf_actor_pref_muted_word *words, size_t count,
                          const char *text, const char *const *tags,
                          size_t tag_count, bool author_followed, int64_t now);

/*
 * A fixed-size, allocation-free copy of the account's muted words, for a client
 * that keeps its filter between requests and frees the preferences it fetched.
 * Both consoles did this with the same struct, the same add and the same load
 * from wf_actor_preferences; this is that once.
 */
#define WF_MUTED_LIST_MAX 48
#define WF_MUTED_VALUE_MAX 64
#define WF_MUTED_EXPIRES_MAX 40

typedef struct wf_muted_entry {
    char value[WF_MUTED_VALUE_MAX];
    bool content; /* applies to post text */
    bool tag;     /* applies to hashtags */
    bool exclude_following;
    char expires_at[WF_MUTED_EXPIRES_MAX]; /* lexicon datetime, "" for none */
} wf_muted_entry;

typedef struct wf_muted_list {
    wf_muted_entry words[WF_MUTED_LIST_MAX];
    size_t count;
    int64_t now; /* Unix seconds when it was loaded; 0 = clock unknown */
} wf_muted_list;

void wf_muted_list_clear(wf_muted_list *list);

/* Add one word. Empty values, and a full list, are ignored (false). A word with
 * neither target is content-only, which is what the server means by the
 * default. `expires_at` may be NULL. */
bool wf_muted_list_add(wf_muted_list *list, const char *value, bool content,
                       bool tag, bool exclude_following,
                       const char *expires_at);

/* Replace `list` with the words in `prefs` (at most WF_MUTED_LIST_MAX; the rest
 * are dropped), remembering `now` for expiry. */
void wf_muted_list_from_prefs(wf_muted_list *list,
                              const wf_actor_preferences *prefs, int64_t now);

/* wf_muted_words_match over `list`, at the time it was loaded. */
bool wf_muted_list_match(const wf_muted_list *list, const char *text,
                         const char *const *tags, size_t tag_count,
                         bool author_followed);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MUTED_WORDS_H */
