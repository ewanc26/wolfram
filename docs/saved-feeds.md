# Saved feeds

A feed picker needs the feeds the account has saved, in order, each with a name to show. [`wolfram/saved_feeds.h`](../include/wolfram/saved_feeds.h) does it once, so Cobalt, Indigo and the rest do not each parse preferences.

```c
wf_saved_feed feeds[32];
size_t n = 0;
if (wf_agent_get_saved_feeds(agent, feeds, 32, &n) == WF_OK)
    /* feeds[i].uri is the generator's AT-URI, feeds[i].name what to show */;
```

- **Raw, not typed.** It reads `getPreferences` as plain JSON rather than through the strict typed parse, because one preference type that parser rejects (a real account returned status 5) must not take the whole picker down with it.
- **V2, then V1.** The items of `savedFeedsPrefV2` whose type is `feed` (timelines and lists are not custom feeds), in order; an older account with none gets the `saved` list of `savedFeedsPref`. Empty values and anything past the caller's `cap` are skipped.
- **Names.** One `getFeedGenerators` call per 25 feeds (the server's limit) gives each feed its display name. A feed the server did not return, or a call that fails, keeps the record key of its URI as its name, so the URIs alone still make a usable picker.
- **Pure parser.** `wf_saved_feeds_parse` is the JSON half with no network, for a client that already holds the preferences. It sets every name to the record key.

`WF_SAVED_FEED_URI_MAX` (256) and `WF_SAVED_FEED_NAME_MAX` (128) bound the strings; longer ones are cut. [`test/test_saved_feeds.c`](../test/test_saved_feeds.c) covers the shapes; the full call is checked against the mock PDS in `test/test_graph_writes.c`. Nothing here has run against a live account.
