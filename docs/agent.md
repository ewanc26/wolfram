# Agent API (`agent.h`)

The agent API is a high-level convenience layer that wraps session management,
XRPC transport, and rich-text facet detection into a single `wf_agent` object,
comparable to TypeScript's `AtpAgent`. It is the recommended entry point for
most apps.

Conventions used throughout:

- Success is `WF_OK` (the `wf_status` enum); any other value is an error.
- Functions that return JSON put it into a caller-provided `wf_response`. Free
  it with `wf_response_free`.
- Functions that return a typed/owned struct come with a matching `_free`
  (e.g. `wf_agent_post_result_free`, `wf_agent_profile_free`,
  `wf_agent_notification_list_free`). Call it when done.

> All snippets below that talk to a PDS are marked `// needs network`. The
> agent talks to a live service; there is no offline mode for most calls.

## Lifecycle and session

```c
#include "wolfram/agent.h"

// Create an agent bound to a service (e.g. "https://bsky.social").
wf_agent *agent = wf_agent_new("https://bsky.social");   // needs network

// Log in with a handle or email + app password.
wf_status st = wf_agent_login(agent, "alice.example.com", "app-password");
if (st != WF_OK) { /* handle error */ }

// Read identity back from the live session.
const char *did    = wf_agent_get_did(agent);     // borrowed; valid until logout/free
const char *handle = wf_agent_get_handle(agent);  // borrowed

// Persist credentials for later (deep copy owned by the caller).
wf_session_data data = {0};
wf_agent_get_session_data(agent, &data);   // needs network
/* ... serialize data.access_jwt / data.refresh_jwt / data.did ... */
wf_agent_session_data_free(&data);         // free the copy when done

// Resume later without re-entering the password:
// wf_agent_resume(agent, &data);

wf_agent_logout(agent);   // needs network
wf_agent_free(agent);
```

Refreshing and re-reading the server session are also available:
`wf_agent_get_session` (re-fetches `getSession`) and `wf_agent_logout`.

## Posts and replies

```c
// Plain post — facets (mentions/links/tags) are detected automatically.
wf_agent_post_result post = {0};
wf_status st = wf_agent_post(agent, "Hello from wolfram! #atproto", &post); // needs network
if (st == WF_OK) {
    printf("uri=%s cid=%s\n", post.uri, post.cid);
}
wf_agent_post_result_free(&post);   // frees post.uri and post.cid

// Reply to a post (need its AT-URI and CID).
wf_agent_post_result reply = {0};
wf_status st2 = wf_agent_reply(agent, "nice post",
                               "at://did:plc:abc/app.bsky.feed.post/xyz",
                               "bafyre...cid", &reply);  // needs network
wf_agent_post_result_free(&reply);

// Quote-post with a record embed.
wf_agent_post_result quote = {0};
wf_agent_quote(agent, "check this out",
               "at://did:plc:abc/app.bsky.feed.post/xyz",
               "bafyre...cid", &quote);  // needs network
wf_agent_post_result_free(&quote);

// Delete a post you created (by AT-URI).
wf_agent_delete_post(agent, "at://did:plc:abc/app.bsky.feed.post/xyz");  // needs network
```

Other post variants: `wf_agent_post_with_facets` (pass pre-built facets JSON),
`wf_agent_post_with_embed` (pass an embed JSON), and
`wf_agent_quote_with_media` (quote + media embed as a `cJSON *`).

## Generic record CRUD

`createRecord` semantics are available for any collection via
`wf_agent_create_record`, and `getRecord` / `putRecord` / `listRecords` wrap the
corresponding `com.atproto.repo` endpoints.

```c
// Create an arbitrary record (e.g. a custom collection).
const char *rec = "{\"$type\":\"com.example.myCollection.record\","
                  "\"text\":\"hi\",\"createdAt\":\"2024-01-01T00:00:00Z\"}";
wf_agent_post_result out = {0};
wf_agent_create_record(agent, "com.example.myCollection", rec, &out); // needs network
wf_agent_post_result_free(&out);

// Fetch a single record (raw JSON response).
wf_response resp = {0};
wf_agent_get_record(agent, "app.bsky.feed.post", "xyz", &resp);  // needs network
/* resp.body holds the JSON; resp.body_len its length. */
wf_response_free(&resp);

// List records with cursor pagination.
wf_response list = {0};
wf_agent_list_records(agent, "app.bsky.feed.post", 50, NULL, &list);  // needs network
wf_response_free(&list);
```

`wf_agent_put_record` updates an existing record (pass the `rkey`).

Batch writes go through `wf_agent_apply_writes`, which wraps
`com.atproto.repo.applyWrites`:

```c
wf_agent_write writes[2] = {
    { WF_AGENT_WRITE_CREATE, "app.bsky.feed.post", NULL,
      "{\"text\":\"one\",\"createdAt\":\"2024-01-01T00:00:00Z\"}" },
    { WF_AGENT_WRITE_DELETE, "app.bsky.feed.post", "rkey-to-delete", NULL },
};
wf_response apply = {0};
wf_agent_apply_writes(agent, writes, 2, &apply);  // needs network
wf_response_free(&apply);
```

## Profile

```c
wf_agent_profile profile = {0};
wf_status st = wf_agent_get_profile(agent, "alice.example.com", &profile); // needs network
if (st == WF_OK) {
    printf("%s (@%s) — %d posts\n",
           profile.display_name ? profile.display_name : "",
           profile.handle, profile.posts_count);
}
wf_agent_profile_free(&profile);  // frees all owned strings

// Update your own profile (NULL fields are omitted).
wf_agent_profile_update upd = {
    .display_name = "Alice",
    .description  = "Building with wolfram.",
    .avatar_cid   = NULL,
    .banner_cid   = NULL,
};
wf_agent_update_profile(agent, &upd);  // needs network

// Change handle.
wf_agent_update_handle(agent, "new.handle.example.com");  // needs network
```

`followers_count`, `follows_count`, and `posts_count` are `int`; `did`,
`handle`, `display_name`, `description`, and `avatar_cid` are owned strings.

## Social graph (follow / mute / block)

```c
// Follow a DID.
wf_agent_post_result follow = {0};
wf_agent_follow(agent, "did:plc:target", &follow);  // needs network
wf_agent_post_result_free(&follow);

// Unfollow by the follow record's AT-URI.
wf_agent_unfollow(agent, "at://did:plc:me/app.bsky.graph.follow/rkey"); // needs network

// Like / unlike a post.
wf_agent_post_result like = {0};
wf_agent_like(agent, "at://did:plc:abc/app.bsky.feed.post/xyz", "bafyre...cid", &like); // needs network
wf_agent_post_result_free(&like);
wf_agent_unlike(agent, "at://did:plc:me/app.bsky.feed.post.like/rkey"); // needs network

// Mute / unmute an actor by handle or DID.
wf_agent_mute(agent, "spam.example.com");    // needs network
wf_agent_unmute(agent, "spam.example.com");  // needs network

// Repost / delete repost.
wf_agent_post_result rp = {0};
wf_agent_repost(agent, "at://did:plc:abc/app.bsky.feed.post/xyz", "bafyre...cid", &rp); // needs network
wf_agent_post_result_free(&rp);
wf_agent_delete_repost(agent, "at://did:plc:me/app.bsky.feed.repost/rkey"); // needs network
```

## Feeds, threads, and search

All of these return **raw JSON** in a `wf_response`; free with
`wf_response_free`. (Typed feed parsers live in `feed_typed.h` / `thread_typed.h`.)

```c
// Home timeline.
wf_response tl = {0};
wf_agent_get_timeline(agent, 50, NULL, NULL, &tl);  // needs network
/* tl.body is the JSON; tl.body_len its length */
wf_response_free(&tl);

// A specific author's feed.
wf_response af = {0};
wf_agent_get_author_feed(agent, "alice.example.com", 50, NULL, NULL, false, &af); // needs network
wf_response_free(&af);

// Post thread.
wf_response th = {0};
wf_agent_get_post_thread(agent, "at://did:plc:abc/app.bsky.feed.post/xyz", 6, 80, &th); // needs network
wf_response_free(&th);

// Fetch several posts at once by AT-URI.
const char *uris[] = { "at://did:plc:abc/app.bsky.feed.post/xyz" };
wf_response posts = {0};
wf_agent_get_posts(agent, uris, 1, &posts);  // needs network
wf_response_free(&posts);

// Search posts.
wf_response search = {0};
wf_agent_search_posts(agent, "atproto", 25, NULL, NULL, NULL, NULL, NULL, &search); // needs network
wf_response_free(&search);

// Likes / reposts / quotes of a post, and custom feed generators.
wf_response likes = {0};
wf_agent_get_likes(agent, "at://did:plc:abc/app.bsky.feed.post/xyz", 50, NULL, &likes); // needs network
wf_response_free(&likes);

wf_response feed = {0};
wf_agent_get_feed(agent, "at://did:plc:gen/app.bsky.feed.generator/abc", 50, NULL, &feed); // needs network
wf_response_free(&feed);
```

Graph queries follow the same shape: `wf_agent_get_profiles`,
`wf_agent_get_follows`, `wf_agent_get_followers`, `wf_agent_get_blocks`,
`wf_agent_get_mutes`, `wf_agent_get_known_followers`, `wf_agent_get_relationships`,
`wf_agent_get_list`, `wf_agent_get_lists`, `wf_agent_get_suggested_follows_by_actor`,
and actor search via `wf_agent_search_actors` / `wf_agent_search_actors_typeahead`.

## Notifications

```c
// Raw JSON:
wf_response notifs = {0};
wf_agent_list_notifications(agent, 50, NULL, &notifs);  // needs network
wf_response_free(&notifs);

// Mark everything seen up to an RFC 3339 timestamp.
wf_agent_update_seen_notifications(agent, "2024-01-01T00:00:00Z"); // needs network

// Typed output — parses into owned structs.
wf_agent_notification_list list = {0};
wf_agent_list_notifications_typed(agent, 50, NULL, &list);  // needs network
for (size_t i = 0; i < list.notification_count; i++) {
    wf_agent_notification *n = &list.notifications[i];
    printf("%s from %s\n", n->reason, n->author.handle);
    if (n->record) { /* owned cJSON record subtree */ }
}
wf_agent_notification_list_free(&list);  // frees the whole list

// Unread count (typed integer):
int unread = 0;
wf_agent_get_unread_count_typed(agent, &unread);  // needs network
```

## Blobs, preferences, and push

```c
// Upload a blob (e.g. an image). Content is raw bytes.
unsigned char *png = /* ... */; size_t png_len = /* ... */;
wf_response blob = {0};
wf_agent_upload_blob(agent, png, png_len, "image/png", &blob);  // needs network
/* blob.body contains {"blob":{"$type":"blob","ref":{...},"mimeType":...}} */
wf_response_free(&blob);

// Persist preferences (raw JSON document).
wf_response prefs = {0};
wf_agent_put_preferences(agent, "{\"#[...]\":true}", &prefs);  // needs network
wf_response_free(&prefs);

// Register / unregister a push token.
wf_response push = {0};
wf_agent_register_push(agent, "did:web:push.example.com", "device-token", &push); // needs network
wf_response_free(&push);
wf_agent_unregister_push(agent, "did:web:push.example.com", "device-token");      // needs network

// Resolve the notification endpoint for a push service (free with free()).
char *endpoint = NULL;
wf_get_notif_endpoint(agent, "did:web:push.example.com", &endpoint);  // needs network
free(endpoint);
```

## Sync, server, and repo-mirror helpers

The agent also exposes thin wrappers around `com.atproto.sync` (no auth needed)
and the `com.atproto.server` account lifecycle:

```c
// Sync wrappers (no auth required):
wf_response b = {0};
wf_agent_sync_get_blob(agent, "did:plc:abc", "bafyre...cid", &b);  // needs network
wf_response_free(&b);

const char *cids[] = { "bafyre...cid" };
wf_response blocks = {0};
wf_agent_sync_get_blocks(agent, "did:plc:abc", cids, 1, &blocks);  // needs network
wf_response_free(&blocks);

// Server management:
wf_agent_server_description desc = {0};
wf_agent_describe_server(agent, &desc);  // needs network
wf_agent_server_description_free(&desc);

wf_agent_app_password pwd = {0};
wf_agent_create_app_password(agent, "mobile", 0, &pwd);  // needs network
wf_agent_app_password_free(&pwd);
```

A local, cryptographically verified repo mirror is available for offline reads
and incremental diff application (see [sync.md](sync.md)):

```c
wf_agent_set_did(agent, "did:plc:abc");
wf_agent_set_signing_key(agent, "...");                  // needs network for seed data
// wf_agent_seed_repo(agent, &car);
// wf_agent_apply_repo_diff(agent, car_bytes, car_len);
char *head = NULL;
wf_agent_repo_head(agent, &head);  // free(head) when done
```

## Cursor pagination helpers

The newer pagination layer loops an underlying read endpoint page by page.
`max_pages <= 0` means "iterate until the cursor is exhausted". The
`on_page` callback receives a **borrow** that is only valid for the duration of
the callback — copy anything you need out of it. The final cursor is returned
as a heap-allocated copy (free it with `free()`); it is `NULL` when the
sequence ended because the cursor was exhausted.

```c
static wf_status on_timeline_page(wf_agent *agent,
                                  const wf_agent_feed_list *feed,
                                  const char *cursor, void *ud) {
    (void)agent; (void)cursor; (void)ud;
    for (size_t i = 0; i < feed->item_count; i++) {
        cJSON *rec = feed->items[i].post.record;
        if (rec) {
            const char *text = cJSON_GetObjectItem(rec, "text")
                                   ? cJSON_GetObjectItem(rec, "text")->valuestring
                                   : "";
            printf("- %s\n", text);
        }
    }
    return WF_OK;
}

char *last_cursor = NULL;
wf_status st = wf_agent_get_timeline_paged(agent, 50, 5,   // up to 5 pages
                                           on_timeline_page, NULL,
                                           &last_cursor);  // needs network
free(last_cursor);  // may be NULL
```

Other paged wrappers: `wf_agent_get_author_feed_paged`,
`wf_agent_list_notifications_paged` (typed list borrow), and
`wf_agent_list_records_paged` (raw `wf_response` borrow). For full control over
which underlying endpoint is paged, use the generic `wf_agent_page` with a
`wf_agent_page_call_fn` / `wf_agent_page_cb` pair, or extract a cursor from any
raw response with `wf_response_cursor`.

## Function reference

Quick, self-contained examples for the most-used agent calls. Each snippet
assumes an authenticated `wf_agent` (see
[Lifecycle and session](#lifecycle-and-session)) and shows the call, an
`if (st != WF_OK)` error check, and the matching `_free`. Every call that talks
to a PDS is marked `// needs network`.

### `wf_agent_new` / `wf_agent_login`

```c
#include "wolfram/agent.h"
#include <stdio.h>

wf_agent *agent = wf_agent_new("https://bsky.social");
if (!agent) { /* out of memory */ }

wf_status st = wf_agent_login(agent, "alice.example.com", "app-password"); // needs network
if (st != WF_OK) {
    fprintf(stderr, "login failed: %d\n", (int)st);
    wf_agent_free(agent);
    return 1;
}

/* ... use agent ... */

wf_agent_free(agent);   // always free when done
```

### `wf_agent_post`

```c
wf_agent_post_result post = {0};
wf_status st = wf_agent_post(agent, "Hello from wolfram! #atproto", &post); // needs network
if (st != WF_OK) {
    fprintf(stderr, "post failed: %d\n", (int)st);
    return;
}
printf("uri=%s cid=%s\n", post.uri, post.cid);
wf_agent_post_result_free(&post);   // frees post.uri and post.cid
```

### `wf_agent_get_post_thread_typed`

```c
#include "wolfram/thread_typed.h"

wf_agent_thread thread = {0};
wf_status st = wf_agent_get_post_thread_typed(
    agent, "at://did:plc:abc/app.bsky.feed.post/xyz", 6, &thread); // needs network
if (st != WF_OK) {
    fprintf(stderr, "thread failed: %d\n", (int)st);
    return;
}
if (thread.root.kind == WF_AGENT_THREAD_KIND_POST) {
    printf("root: %s\n", thread.root.post.uri);
}
wf_agent_thread_free(&thread);   // recursively frees the reply tree
```

### `wf_agent_get_timeline_typed`

```c
#include "wolfram/feed_typed.h"

wf_agent_feed_list feed = {0};
wf_status st = wf_agent_get_timeline_typed(agent, 50, NULL, &feed); // needs network
if (st != WF_OK) {
    fprintf(stderr, "timeline failed: %d\n", (int)st);
    return;
}
for (size_t i = 0; i < feed.item_count; i++) {
    printf("%s\n", feed.items[i].post.uri ? feed.items[i].post.uri : "?");
}
wf_agent_feed_list_free(&feed);
```

The raw-JSON sibling `wf_agent_get_timeline` returns the same data in a
`wf_response`; free it with `wf_response_free`.

### `wf_agent_list_notifications_typed`

```c
wf_agent_notification_list list = {0};
wf_status st = wf_agent_list_notifications_typed(agent, 50, NULL, &list); // needs network
if (st != WF_OK) {
    fprintf(stderr, "notifications failed: %d\n", (int)st);
    return;
}
for (size_t i = 0; i < list.notification_count; i++) {
    printf("%s from %s\n", list.notifications[i].reason,
           list.notifications[i].author.handle);
}
wf_agent_notification_list_free(&list);
```

### `wf_agent_follow`

```c
wf_agent_post_result follow = {0};
wf_status st = wf_agent_follow(agent, "did:plc:target", &follow); // needs network
if (st != WF_OK) {
    fprintf(stderr, "follow failed: %d\n", (int)st);
    return;
}
printf("follow uri=%s\n", follow.uri);
wf_agent_post_result_free(&follow);

/* Later, to unfollow: wf_agent_unfollow(agent, follow_uri); */
```

### `wf_agent_like`

```c
wf_agent_post_result like = {0};
wf_status st = wf_agent_like(agent,
    "at://did:plc:abc/app.bsky.feed.post/xyz", "bafyre...cid", &like); // needs network
if (st != WF_OK) {
    fprintf(stderr, "like failed: %d\n", (int)st);
    return;
}
printf("like uri=%s\n", like.uri);
wf_agent_post_result_free(&like);

/* Later, to unlike: wf_agent_unlike(agent, like_uri); */
```

### `wf_agent_upload_blob`

```c
unsigned char *png = /* ... */; size_t png_len = /* ... */; // image bytes
wf_response blob = {0};
wf_status st = wf_agent_upload_blob(agent, png, png_len, "image/png", &blob); // needs network
if (st != WF_OK) {
    fprintf(stderr, "upload failed: %d\n", (int)st);
    return;
}
/* blob.body is {"blob":{"$type":"blob","ref":{...},"mimeType":...}} */
wf_response_free(&blob);
```
