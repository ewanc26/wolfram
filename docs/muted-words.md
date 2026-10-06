# Muted words

[`wolfram/muted_words.h`](../include/wolfram/muted_words.h) answers one question: does the account's `mutedWordsPref` mute this post? Cobalt and Indigo each had a copy of the matching, and neither read `actorTarget` or `expiresAt`. This is the one implementation. It is pure, allocates nothing while matching, and builds on every target.

```c
bool wf_muted_words_match(words, count, text, tags, tag_count, author_followed, now);
```

`words` are the `wf_actor_pref_muted_word` items from `wf_actor_preferences.muting_keywords`. Rules, from the [lexicon](../lexicons/app/bsky/actor/defs.json) and the behaviour the clients already shared:

- A `content` target matches the post text. A `tag` target matches a hashtag exactly, ignoring case, with or without a leading `#` on either side.
- Matching is case-insensitive over ASCII only. Other bytes compare exactly, so `É` does not match `é`; Unicode case folding is not done.
- A plain word (ASCII letters and digits, and any byte from 0x80) matches whole words, so `cat` does not match `category`. Anything else (`c++`, `good morning`) matches as a substring. An empty value never matches.
- `actorTarget: exclude-following` words do not apply when `author_followed` is true. `all`, or no `actorTarget`, applies to everyone.
- A word whose `expiresAt` has passed does not apply. An `expiresAt` that does not parse counts as not expired, which is the safe direction for a mute. Passing `now` as 0 means the clock is unknown, and nothing expires.

Hiding reposts on the home feed is a client setting, not a muted word, and stays in the client. So does the choice of what to do with a match: drop the post, collapse it, or show a warning.

The vectors are [`test/vectors/muted_words.json`](../test/vectors/muted_words.json), 31 cases. They include what Indigo's and Cobalt's matching did, plus the two behaviours they lacked, so a client switching over can see exactly what changes.
