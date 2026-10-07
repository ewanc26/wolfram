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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MUTED_WORDS_H */
