# Muted words

[`wolfram/muted_words.h`](../include/wolfram/muted_words.h) answers one question: does the account's `mutedWordsPref` mute this post? Cobalt and Indigo each had a copy of the matching, and neither read `actorTarget` or `expiresAt`. This is the one implementation. It is pure, allocates nothing while matching, and builds on every target.

```c
bool wf_muted_words_match(words, count, text, tags, tag_count, author_followed, now);
```

`words` are the `wf_actor_pref_muted_word` items from `wf_actor_preferences.muting_keywords`. Rules, from the [lexicon](../lexicons/app/bsky/actor/defs.json) and the behaviour the clients already shared:

- A `content` target matches the post text. A `tag` target matches a hashtag exactly, ignoring case, with or without a leading `#` on either side.
- The word rules are `wf_mod_match_mute_words`'s (see [moderation](modules.md)): a port of the official client's matcher, so the clients and the moderation code agree. Matching is case-insensitive. A single word matches whole words and ignores punctuation at the ends of a word, so `cat` matches `cat.` and `(cat)` but not `category` or `cat's`. A phrase, a word with punctuation inside it, and a single character match as a substring. An empty value never matches. Unicode case folding is not done.
- `actorTarget: exclude-following` words do not apply when `author_followed` is true. `all`, or no `actorTarget`, applies to everyone.
- A word whose `expiresAt` has passed does not apply. An `expiresAt` that does not parse counts as not expired, which is the safe direction for a mute. Passing `now` as 0 means the clock is unknown, and nothing expires.

`wf_muted_words_match` is an adapter over that matcher, not a second implementation: it applies `now` to the expiry, sends content words against the text and tag words against the tags, and passes `author_followed` through. It allocates a little per call.

`wf_muted_list` is the copy a client keeps between requests, so it can free the preferences it fetched: a fixed-size, allocation-free list of up to 48 words (64 bytes each). `wf_muted_list_from_prefs` fills it from `wf_actor_preferences` (remembering `now`), `wf_muted_list_add` adds one word, `wf_muted_list_match` is `wf_muted_words_match` over it. Cobalt and Indigo each had this struct, its add function and the load from the preferences as copies of one another; this is that once.

Hiding reposts on the home feed is a client setting, not a muted word, and stays in the client. So does the choice of what to do with a match: drop the post, collapse it, or show a warning.

The vectors are [`test/vectors/muted_words.json`](../test/vectors/muted_words.json), 37 cases, including the punctuation rules above.
