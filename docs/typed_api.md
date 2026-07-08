# Typed high-level agent API

The wolfram agent exposes a *typed* layer on top of the raw-JSON XRPC calls.
Instead of returning a `wf_response` whose body you must parse by hand, the
typed wrappers parse the JSON into owned C structs that you free with a matching
`_free` function.

All typed calls are declared in `include/wolfram/agent.h` and the companion
headers `feed_typed.h`, `thread_typed.h`, and `graph_typed.h`. A single include
of `wolfram/agent.h` pulls in all three.

## Typed parser modules

wolfram ships several self-contained typed-parser headers, each parsing a
specific family of responses into owned structs freed by a matching `_free`:

- **Feed** — `feed_typed.h` (`wf_agent_get_timeline_typed`, author feed, quotes, likes)
- **Thread** — `thread_typed.h` (`wf_agent_get_post_thread_typed`, recursive reply tree)
- **List** — `list_typed.h` (list records / members)
- **Graph** — `graph_typed.h` (follows, followers, profiles, search, reposts, likes)
- **Embed** — `embed_typed.h` (build owned image / video / record / external embeds)
- **Notification** — `agent.h` + `notification` parsers (`wf_agent_list_notifications_typed`)
- **Unspecced** (NEW) — `unspecced_typed.h` (trending topics, tagged/suggestions skeletons, config, age assurance, onboarding starter packs, starter-pack search). See [unspecced.md](unspecced.md).
- **Chat** — `chat_typed.h` (conversation / message views)
- **Moderation** — `moderation_typed.h` (label / moderation detail views)
- **Feed generator** — `feedgen_typed.h` (feed generator / skeleton parsing)

Each module documents its own ownership rules; the common conventions in the
next section apply to all of them.

## Ownership rules

- Every `*_list`, `*_thread`, and `*_notification_list` returned by a typed call
  is **owned by the caller** and must be freed exactly once with its matching
  `_free`.
- The `record`, `embed`, `reason`, and `reply` fields are owned `cJSON *`
  subtrees that have been *detached* from the response document. They are freed
  for you by the containing struct's `_free` call — do **not** `cJSON_Delete`
  them yourself.
- The `author` field is a `wf_agent_profile_view { did, handle, display_name,
  avatar }`; all four members are owned strings freed with the parent struct.
- On error the output struct is left fully reset (no partial leaks), so it is
  always safe to call the matching `_free` after a failed call returns.
- Strings pointer fields (`uri`, `handle`, `cursor`, …) are owned by the parsed
  struct; copy them if you need them past the `_free` call.

## Common setup / teardown

```c
#include "wolfram/agent.h"
#include <cJSON.h>

wf_agent *agent = wf_agent_new("https://bsky.social");
if (!agent) { /* out of memory */ }

wf_status s = wf_agent_login(agent, "handle.example", "password");
if (s != WF_OK) {
    fprintf(stderr, "login failed: %d\n", (int)s);
    wf_agent_free(agent);
    return 1;
}

/* ... use typed calls ... */

wf_agent_free(agent);
```

---

## Notifications

### `wf_agent_list_notifications_typed` — fetch and parse

```c
wf_agent_notification_list list = {0};
wf_status s = wf_agent_list_notifications_typed(agent, 50, NULL, &list);
if (s != WF_OK) {
    fprintf(stderr, "list notifications failed: %d\n", (int)s);
    wf_agent_notification_list_free(&list);   /* safe: resets an already-reset list */
    return 1;
}

for (size_t i = 0; i < list.notification_count; ++i) {
    wf_agent_notification *n = &list.notifications[i];
    printf("from %s (%s): %s\n",
           n->author.handle ? n->author.handle : "?",
           n->reason ? n->reason : "?",
           n->uri ? n->uri : "?");
    if (n->record) {
        /* owned cJSON* subtree — read it, then let _free clean it up */
        cJSON *text = cJSON_GetObjectItemCaseSensitive(n->record, "text");
        if (cJSON_IsString(text)) printf("  %s\n", text->valuestring);
    }
}

wf_agent_notification_list_free(&list);
```

### `wf_agent_parse_notifications` — parse an already-fetched body

Use this when you have the raw JSON from `wf_agent_list_notifications` (or an
offline fixture) and want to parse it without a network round-trip.

```c
wf_agent_notification_list list = {0};
wf_status s = wf_agent_parse_notifications(res.body, res.body_len, &list);
if (s != WF_OK) {
    wf_agent_notification_list_free(&list);
    return 1;
}
/* iterate exactly as above */
wf_agent_notification_list_free(&list);
```

### `wf_agent_get_unread_count_typed`

```c
int count = 0;
wf_status s = wf_agent_get_unread_count_typed(agent, &count);
if (s != WF_OK) {
    fprintf(stderr, "get unread count failed: %d\n", (int)s);
    return 1;
}
printf("unread: %d\n", count);
```

Free: `wf_agent_notification_list_free` (for `list_notifications_typed` /
`parse_notifications`). `get_unread_count_typed` writes a plain `int` — nothing
to free.

---

## Feed

### `wf_agent_get_timeline_typed`

```c
wf_agent_feed_list feed = {0};
wf_status s = wf_agent_get_timeline_typed(agent, 10, NULL, &feed);
if (s != WF_OK) {
    wf_agent_feed_list_free(&feed);
    return 1;
}
for (size_t i = 0; i < feed.item_count; ++i) {
    wf_agent_feed_item *item = &feed.items[i];
    wf_agent_post_view *post = &item->post;
    printf("%s: %s (likes=%d)\n",
           post->author.handle ? post->author.handle : "?",
           post->uri ? post->uri : "?",
           post->like_count);
    if (post->record) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(post->record, "text");
        if (cJSON_IsString(text)) printf("  %s\n", text->valuestring);
    }
}
if (feed.cursor) printf("next cursor: %s\n", feed.cursor);
wf_agent_feed_list_free(&feed);
```

### `wf_agent_get_author_feed_typed`

Identical iteration; the `filter` argument accepts `"posts_with_replies"`
(or `NULL` for the default).

```c
wf_agent_feed_list feed = {0};
wf_status s = wf_agent_get_author_feed_typed(agent, "alice.bsky.social",
                                             20, NULL, NULL, &feed);
/* ... iterate feed.items ... */
wf_agent_feed_list_free(&feed);
```

### `wf_agent_get_quotes_typed`

```c
wf_agent_feed_list feed = {0};
wf_status s = wf_agent_get_quotes_typed(agent,
                                       "at://did:plc:abc/app.bsky.feed.post/xyz",
                                       20, NULL, &feed);
/* ... iterate feed.items ... */
wf_agent_feed_list_free(&feed);
```

### `wf_agent_parse_feed` / `wf_agent_parse_feed_key` — parse a raw body

```c
/* timeline / author-feed bodies use the "feed" array */
wf_agent_feed_list feed = {0};
wf_status s = wf_agent_parse_feed(res.body, res.body_len, &feed);
/* ... iterate ... */
wf_agent_feed_list_free(&feed);

/* getQuotes bodies use the "posts" array */
wf_agent_feed_list quotes = {0};
s = wf_agent_parse_feed_key(res.body, res.body_len, "posts", &quotes);
/* ... iterate quotes.items ... */
wf_agent_feed_list_free(&quotes);
```

Free: `wf_agent_feed_list_free` for every feed call above. The `record`,
`embed`, `reason`, and `reply` `cJSON *` members are owned by the list and freed
for you.

---

## Thread

### `wf_agent_get_post_thread_typed`

```c
wf_agent_thread thread = {0};
wf_status s = wf_agent_get_post_thread_typed(agent,
                                             "at://did:plc:abc/app.bsky.feed.post/xyz",
                                             6, &thread);
if (s != WF_OK) {
    wf_agent_thread_free(&thread);
    return 1;
}

/* The root node: a POST has a valid `post`; NOT_FOUND/BLOCKED have a `uri`. */
if (thread.root.kind == WF_AGENT_THREAD_KIND_POST) {
    printf("root: %s by %s\n",
           thread.root.post.uri ? thread.root.post.uri : "?",
           thread.root.post.author.handle ? thread.root.post.author.handle : "?");
    if (thread.root.post.record) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(thread.root.post.record, "text");
        if (cJSON_IsString(text)) printf("  %s\n", text->valuestring);
    }
} else if (thread.root.uri) {
    printf("root node (not a post): %s\n", thread.root.uri);
}

/* Walk the reply tree (recursive example). */
static void walk(wf_agent_thread_node *node, int depth) {
    for (int i = 0; i < depth; ++i) printf("  ");
    if (node->kind == WF_AGENT_THREAD_KIND_POST) {
        printf("%s\n", node->post.uri ? node->post.uri : "?");
    } else {
        printf("%s [%s]\n", node->uri ? node->uri : "?",
               node->kind == WF_AGENT_THREAD_KIND_NOT_FOUND ? "not-found" : "blocked");
    }
    for (size_t i = 0; i < node->replies_count; ++i)
        walk(&node->replies[i], depth + 1);
}
walk(&thread.root, 0);

wf_agent_thread_free(&thread);
```

### `wf_agent_parse_thread` — parse a raw body

```c
wf_agent_thread thread = {0};
wf_status s = wf_agent_parse_thread(res.body, res.body_len, &thread);
/* ... use thread.root ... */
wf_agent_thread_free(&thread);
```

Free: `wf_agent_thread_free`. Recursively frees `parent` and every entry of the
`replies` array; `record` and `embed` `cJSON *` members are freed for you.

---

## Graph (actor lists, likes, reposts)

### `wf_agent_get_follows_typed` / `wf_agent_get_followers_typed`

```c
wf_agent_actor_list actors = {0};
wf_status s = wf_agent_get_follows_typed(agent, "alice.bsky.social", 100, NULL, &actors);
if (s != WF_OK) {
    wf_agent_actor_list_free(&actors);
    return 1;
}
for (size_t i = 0; i < actors.actor_count; ++i) {
    wf_agent_profile_view *p = &actors.actors[i];
    printf("%s (%s)\n", p->handle ? p->handle : "?", p->did ? p->did : "?");
}
if (actors.cursor) printf("next cursor: %s\n", actors.cursor);
wf_agent_actor_list_free(&actors);

/* followers: same shape */
wf_agent_actor_list followers = {0};
s = wf_agent_get_followers_typed(agent, "alice.bsky.social", 100, NULL, &followers);
/* ... iterate followers.actors ... */
wf_agent_actor_list_free(&followers);
```

### `wf_agent_get_profiles_typed`

```c
const char *actors[] = { "alice.bsky.social", "bob.bsky.social" };
wf_agent_actor_list profiles = {0};
wf_status s = wf_agent_get_profiles_typed(agent, actors, 2, 0, NULL, &profiles);
/* ... iterate profiles.actors ... */
wf_agent_actor_list_free(&profiles);
```

### `wf_agent_search_actors_typed`

```c
wf_agent_actor_list results = {0};
wf_status s = wf_agent_search_actors_typed(agent, "kilo", 25, NULL, &results);
/* ... iterate results.actors ... */
wf_agent_actor_list_free(&results);
```

### `wf_agent_get_reposted_by_typed`

```c
wf_agent_actor_list reposters = {0};
wf_status s = wf_agent_get_reposted_by_typed(agent,
                                             "at://did:plc:abc/app.bsky.feed.post/xyz",
                                             50, NULL, &reposters);
/* ... iterate reposters.actors ... */
wf_agent_actor_list_free(&reposters);
```

### `wf_agent_get_likes_typed`

```c
wf_agent_like_list likes = {0};
wf_status s = wf_agent_get_likes_typed(agent,
                                       "at://did:plc:abc/app.bsky.feed.post/xyz",
                                       50, NULL, &likes);
if (s != WF_OK) {
    wf_agent_like_list_free(&likes);
    return 1;
}
for (size_t i = 0; i < likes.like_count; ++i) {
    printf("%s liked at %s\n",
           likes.likes[i].actor.handle ? likes.likes[i].actor.handle : "?",
           likes.likes[i].created_at ? likes.likes[i].created_at : "?");
}
wf_agent_like_list_free(&likes);
```

### Offline parsers for graph responses

```c
/* getFollows / getFollowers / getProfiles / searchActors -> "actors" array */
wf_agent_actor_list a = {0};
wf_status s = wf_agent_parse_actors(res.body, res.body_len, &a);
wf_agent_actor_list_free(&a);

/* same, but read the list from a different key (e.g. "profiles") */
wf_agent_actor_list b = {0};
s = wf_agent_parse_profile_views(res.body, res.body_len, "profiles", &b);
wf_agent_actor_list_free(&b);

/* getRepostedBy -> "repostedBy" key */
wf_agent_actor_list c = {0};
s = wf_agent_parse_reposted_by(res.body, res.body_len, &c);
wf_agent_actor_list_free(&c);

/* getLikes -> "likes" array of {actor, createdAt, indexedAt} */
wf_agent_like_list l = {0};
s = wf_agent_parse_likes(res.body, res.body_len, &l);
wf_agent_like_list_free(&l);
```

Free:
- `wf_agent_actor_list_free` for `get_follows_typed`, `get_followers_typed`,
  `get_profiles_typed`, `search_actors_typed`, `get_reposted_by_typed`,
  `parse_actors`, `parse_profile_views`, `parse_reposted_by`.
- `wf_agent_like_list_free` for `get_likes_typed` and `parse_likes`.
