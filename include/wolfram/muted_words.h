/*
 * muted_words.h -- muted-word matching for the account's `mutedWordsPref`.
 *
 * The lexicon (app.bsky.actor.defs#mutedWord) gives each word a value, targets
 * (`content`, `tag`), an optional `actorTarget` (`all` by default, or
 * `exclude-following`) and an optional `expiresAt`. Cobalt and Indigo each
 * implemented this by copy, without `actorTarget` or `expiresAt`; this is the
 * one implementation.
 *
 * Pure and allocation-free on the match path; builds on every target.
 *
 * Rules, as the clients already applied them:
 *  - Matching is case-insensitive over ASCII; other bytes compare exactly.
 *    (Unicode case folding is not done: "É" does not match "é".)
 *  - A "plain" word, one made only of ASCII letters, digits and bytes >= 0x80,
 *    matches whole words only: "cat" does not match "category". Any other
 *    word (spaces, punctuation: "c++", "good morning") matches as a substring.
 *  - An empty value never matches.
 *  - `content` targets match the post text; `tag` targets match a hashtag
 *    exactly (case-insensitive), with or without a leading '#' on either side.
 *  - `exclude-following` words do not apply to accounts the viewer follows.
 *  - A word whose `expiresAt` has passed does not apply. An `expiresAt` that
 *    does not parse is treated as not expired (the safe direction for a mute).
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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MUTED_WORDS_H */
