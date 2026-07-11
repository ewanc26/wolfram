# wolfram High-Level Agent API — Usage Guide

This guide documents the high-level **agent** API declared in
`include/wolfram/agent.h`. It wraps session management, the XRPC transport,
and rich-text facet detection into a single `wf_agent` object, in the spirit of
TypeScript's `AtpAgent`.

Every function returns a `wf_status`; `WF_OK` (0) indicates success. Any
non-`WF_OK` return means the call failed and out-parameters were not populated.

```c
#include "wolfram/agent.h"
#include <cJSON.h>   /* for facet/embed/record JSON construction */
```

## Ownership rules (read this first)

The agent API hands back several *owned* resources. Free them exactly as noted:

| Type | Allocated by | Free with |
|------|--------------|-----------|
| `wf_agent *` | `wf_agent_new` | `wf_agent_free` |
| `wf_agent_post_result` (`uri`, `cid`) | `wf_agent_post`, `wf_agent_reply`, `wf_agent_quote`, `wf_agent_follow`, `wf_agent_like`, `wf_agent_repost`, `wf_agent_create_record`, `wf_agent_put_record` | `wf_agent_post_result_free` |
| `wf_response` (`body`, `body_len`, `status`, `dpop_nonce`) | feed/graph/notification/sync/blob/server query & procedure calls | `wf_response_free` |
| `wf_agent_profile` | `wf_agent_get_profile` | `wf_agent_profile_free` |
| `wf_agent_notification_list` | `wf_agent_list_notifications_typed`, `wf_agent_parse_notifications` | `wf_agent_notification_list_free` |
| `wf_agent_server_description` | `wf_agent_describe_server` | `wf_agent_server_description_free` |
| `wf_agent_app_password` | `wf_agent_create_app_password` | `wf_agent_app_password_free` |
| `wf_agent_app_password_list` | `wf_agent_list_app_passwords` | `wf_agent_app_password_list_free` |
| `wf_session_data` | `wf_agent_get_session_data` | `wf_agent_session_data_free` |
| `char *` DID / endpoint out-params (`out_did`, `out_endpoint`, `out_head`) | resolve/mirror calls | `free` |
| `wf_repo_operation *` | `wf_agent_invert_repo_operations` | `wf_repo_operations_free` |

`cJSON` objects you build yourself are *your* responsibility: delete them with
`cJSON_Delete` after serializing with `cJSON_PrintUnformatted`/`cJSON_Print`.

---

## 1. Lifecycle, login, resume, logout

```c
#include "wolfram/agent.h"

wf_agent *agent = wf_agent_new("https://bsky.social");   /* PDS service URL */
if (!agent) { /* allocation failure */ }

/* Login with an identifier (handle or email) + password */
wf_status s = wf_agent_login(agent, "alice.bsky.social", "hunter2-pw");
if (s != WF_OK) { /* login failed */ }

const char *handle = wf_agent_get_handle(agent);   /* borrowed, valid until free */
const char *did    = wf_agent_get_did(agent);      /* borrowed */
printf("Logged in as %s (%s)\n", handle, did);

/* Persist session for later resume */
wf_session_data data = {0};
s = wf_agent_get_session_data(agent, &data);   /* copies creds; free the copy */
/* ... store data.access_jwt, data.refresh_jwt, data.did, data.handle ... */
wf_agent_session_data_free(&data);

/* ... later, in a new process ... */
wf_session_data saved = { /* restored fields */ 0 };
wf_agent *agent2 = wf_agent_new("https://bsky.social");
s = wf_agent_resume(agent2, &saved);   /* uses refresh token, refreshes if needed */

/* Refresh the current session in place */
s = wf_agent_get_session(agent);

/* Log out (revokes the session token) */
s = wf_agent_logout(agent);

wf_agent_free(agent);
wf_agent_free(agent2);
```

`wf_agent_login`, `wf_agent_resume`, and `wf_agent_get_session` require a live
PDS. `wf_agent_get_handle` / `wf_agent_get_did` return `NULL` until a session
exists.

---

## 2. Create a post (plain text)

```c
wf_agent_post_result res = {0};
wf_status s = wf_agent_post(agent, "Hello from wolfram!", &res);
if (s == WF_OK) {
    printf("uri=%s cid=%s\n", res.uri, res.cid);
}
wf_agent_post_result_free(&res);
```

`res.uri` and `res.cid` are heap-owned strings; always free with
`wf_agent_post_result_free`.

---

## 3. Create a post with facets (mentions, links, tags)

`wf_agent_post_with_facets` takes a **facets JSON** string (the `facets` array
of an `app.bsky.feed.post` record). Build it with `cJSON` (see
`examples/create_post.c` for handle resolution, or use `wf_richtext`):

```c
#include <cJSON.h>

/* A minimal facet for a link. In practice build a full facet array. */
cJSON *facet  = cJSON_CreateObject();
cJSON *index  = cJSON_CreateObject();
cJSON_AddNumberToObject(index, "byteStart", 0);
cJSON_AddNumberToObject(index, "byteEnd", 13);
cJSON_AddItemToObject(facet, "index", index);

cJSON *features = cJSON_CreateArray();
cJSON *link = cJSON_CreateObject();
cJSON_AddStringToObject(link, "$type", "app.bsky.richtext.facet#link");
cJSON_AddStringToObject(link, "uri", "https://example.com");
cJSON_AddItemToArray(features, link);
cJSON_AddItemToObject(facet, "features", features);

cJSON *facets = cJSON_CreateArray();
cJSON_AddItemToArray(facets, facet);

char *facets_json = cJSON_PrintUnformatted(facets);
cJSON_Delete(facets);

wf_agent_post_result res = {0};
wf_status s = wf_agent_post_with_facets(agent, "Visit example.com now", facets_json, &res);
free(facets_json);
if (s == WF_OK) {
    printf("uri=%s cid=%s\n", res.uri, res.cid);
}
wf_agent_post_result_free(&res);
```

---

## 4. Reply

`wf_agent_reply` needs the parent post's `uri` and `cid` (and internally wires
the `reply.root` reference from the parent's record). For the simplest case you
only supply the **parent**; the agent resolves the root:

```c
wf_agent_post_result res = {0};
wf_status s = wf_agent_reply(agent,
                             "This is a reply",
                             parent_uri,   /* e.g. "at://did/app.bsky.feed.post/abc" */
                             parent_cid,   /* CID of the parent post */
                             &res);
if (s == WF_OK) {
    printf("reply uri=%s cid=%s\n", res.uri, res.cid);
}
wf_agent_post_result_free(&res);
```

To build a fully-correct reply tree (with `reply.root` set to the *thread root*),
fetch the parent via `wf_agent_get_posts` and read `record.reply.root`. The
low-level `examples/reply_post.c` shows this.

---

## 5. Quote post

**Quote with a record embed only:**

```c
wf_agent_post_result res = {0};
wf_status s = wf_agent_quote(agent,
                             "Check this out",
                             quoted_uri,   /* at://... of the post being quoted */
                             quoted_cid,   /* CID of that post */
                             &res);
if (s == WF_OK) {
    printf("quote uri=%s cid=%s\n", res.uri, res.cid);
}
wf_agent_post_result_free(&res);
```

**Quote with a record + media embed** (e.g. an image):

```c
cJSON *media_embed = wf_embed_images_new();              /* from wolfram/embed.h */
/* ... add an image blob to media_embed ... */
wf_agent_post_result res = {0};
wf_status s = wf_agent_quote_with_media(agent,
                                        "Quoting with a photo",
                                        quoted_uri, quoted_cid,
                                        media_embed,
                                        &res);
cJSON_Delete(media_embed);
wf_agent_post_result_free(&res);
```

---

## 6. Like / Unlike

```c
wf_agent_post_result like = {0};
wf_status s = wf_agent_like(agent, post_uri, post_cid, &like);
if (s == WF_OK) {
    printf("like uri=%s cid=%s\n", like.uri, like.cid);
    /* Keep like.uri to undo later */
}
wf_agent_post_result_free(&like);

/* Unlike: pass the like record's AT URI */
s = wf_agent_unlike(agent, like_uri);
```

---

## 7. Follow / Unfollow

```c
wf_agent_post_result follow = {0};
wf_status s = wf_agent_follow(agent, subject_did, &follow);  /* DID of the target */
if (s == WF_OK) {
    printf("follow uri=%s cid=%s\n", follow.uri, follow.cid);
}
wf_agent_post_result_free(&follow);

/* Unfollow: pass the follow record's AT URI */
s = wf_agent_unfollow(agent, follow_uri);
```

---

## 8. Repost / Delete repost

```c
wf_agent_post_result repost = {0};
wf_status s = wf_agent_repost(agent, post_uri, post_cid, &repost);
if (s == WF_OK) {
    printf("repost uri=%s cid=%s\n", repost.uri, repost.cid);
}
wf_agent_post_result_free(&repost);

/* Undo the repost */
s = wf_agent_delete_repost(agent, repost_uri);
```

---

## 9. Mute / Unmute

```c
wf_status s = wf_agent_mute(agent, "target.bsky.social");     /* handle or DID */
s = wf_agent_unmute(agent, "target.bsky.social");
```

---

## 10. Get a profile

```c
wf_agent_profile profile = {0};
wf_status s = wf_agent_get_profile(agent, "alice.bsky.social", &profile);
if (s == WF_OK) {
    printf("%s (@%s) — %d followers, %d posts\n",
           profile.display_name ? profile.display_name : profile.handle,
           profile.handle, profile.followers_count, profile.posts_count);
}
wf_agent_profile_free(&profile);
```

---

## 11. Get timeline / post thread

Feed and graph endpoints return **raw JSON** in a `wf_response`; free it with
`wf_response_free`.

```c
wf_response res = {0};
wf_status s = wf_agent_get_timeline(agent, 50, NULL, NULL /* algorithm */, &res);
if (s == WF_OK) {
    /* res.body is a NUL-terminated JSON string, res.body_len its length */
    printf("timeline: %s\n", res.body);
}
wf_response_free(&res);

/* Get a full post thread */
wf_response thread = {0};
s = wf_agent_get_post_thread(agent, post_uri, 6 /* depth */, 80 /* parentHeight */, &thread);
if (s == WF_OK) {
    printf("thread: %s\n", thread.body);
}
wf_response_free(&thread);

/* Fetch several posts at once */
const char *uris[] = { post_uri1, post_uri2 };
wf_response posts = {0};
s = wf_agent_get_posts(agent, uris, 2, &posts);
wf_response_free(&posts);
```

The `*_lex` variants (`wf_agent_get_timeline_lex`, `wf_agent_get_author_feed_lex`,
etc.) return the same data but run through the lexicon-generated decoder path;
the JSON shape is equivalent.

---

## 12. List notifications (raw + typed)

**Raw JSON** (caller parses):

```c
wf_response res = {0};
wf_status s = wf_agent_list_notifications(agent, 50, NULL, &res);
if (s == WF_OK) {
    printf("notifications: %s\n", res.body);
}
wf_response_free(&res);

/* Mark seen */
s = wf_agent_update_seen_notifications(agent, "2024-01-01T00:00:00Z");

/* Unread count as raw JSON */
wf_response unread = {0};
s = wf_agent_get_unread_count(agent, &unread);
wf_response_free(&unread);

/* Typed unread count (int) */
int count = 0;
s = wf_agent_get_unread_count_typed(agent, &count);
```

**Typed (parsed into owned structs):**

```c
wf_agent_notification_list list = {0};
wf_status s = wf_agent_list_notifications_typed(agent, 50, NULL, &list);
if (s == WF_OK) {
    for (size_t i = 0; i < list.notification_count; ++i) {
        const wf_agent_notification *n = &list.notifications[i];
        printf("[%s] %s\n", n->reason,
               n->author.handle ? n->author.handle : "?");
        if (n->record) {
            /* n->record is an owned cJSON subtree; read it directly */
            char *rec = cJSON_PrintUnformatted(n->record);
            printf("  record: %s\n", rec);
            free(rec);
        }
        for (size_t j = 0; j < n->label_count; ++j) {
            printf("  label: %s\n", n->labels[j].val);
        }
    }
    if (list.cursor) printf("next cursor: %s\n", list.cursor);
}
wf_agent_notification_list_free(&list);
```

`wf_agent_parse_notifications` is the shared backend — use it to parse a JSON
body you already fetched (e.g. from a cached `wf_response`):

```c
wf_agent_notification_list list = {0};
wf_status s = wf_agent_parse_notifications(res.body, res.body_len, &list);
/* ... use list ... */
wf_agent_notification_list_free(&list);
```

---

## 13. Upload a blob + create an image embed

`wf_agent_upload_blob` returns raw JSON containing a `blob` object. Build the
`images` embed and post with `wf_agent_post_with_embed`:

```c
#include <cJSON.h>

unsigned char *png; size_t png_len;   /* read image bytes from disk */

wf_response upload = {0};
wf_status s = wf_agent_upload_blob(agent, png, png_len, "image/png", &upload);
free(png);
if (s != WF_OK) { wf_response_free(&upload); /* ... */ }

/* Parse the blob CID + mime + size out of upload.body (see examples/post_image.c) */
const char *blob_cid = /* ... from upload.body "blob.ref.$link" ... */;
const char *mime = "image/png";
size_t size = png_len;
wf_response_free(&upload);

cJSON *embed    = cJSON_CreateObject();
cJSON *images   = cJSON_CreateArray();
cJSON *image    = cJSON_CreateObject();
cJSON *blob_json = cJSON_CreateObject();
cJSON *ref       = cJSON_CreateObject();
cJSON_AddStringToObject(ref, "$link", blob_cid);
cJSON_AddItemToObject(blob_json, "ref", ref);
cJSON_AddStringToObject(blob_json, "$type", "blob");
cJSON_AddStringToObject(blob_json, "mimeType", mime);
cJSON_AddNumberToObject(blob_json, "size", (double)size);
cJSON_AddItemToObject(image, "image", blob_json);
cJSON_AddStringToObject(image, "alt", "a description");
cJSON_AddItemToArray(images, image);
cJSON_AddStringToObject(embed, "$type", "app.bsky.embed.images");
cJSON_AddItemToObject(embed, "images", images);

char *embed_json = cJSON_PrintUnformatted(embed);
cJSON_Delete(embed);

wf_agent_post_result res = {0};
s = wf_agent_post_with_embed(agent, "Post with image", embed_json, &res);
free(embed_json);
wf_agent_post_result_free(&res);
```

The helper functions in `wolfram/embed.h` (`wf_embed_images_new`,
`wf_embed_images_add_image`) build the same structure more concisely.

---

## 14. Generic record CRUD

`app.bsky.feed.post` is just one collection. Use the generic helpers for any
NSID collection.

```c
/* Create any record (collection + JSON value) */
wf_agent_post_result res = {0};
wf_status s = wf_agent_create_record(agent,
                                     "app.bsky.labeler.service",
                                     record_json,   /* JSON object string */
                                     &res);
if (s == WF_OK) {
    printf("created uri=%s cid=%s\n", res.uri, res.cid);
}
wf_agent_post_result_free(&res);

/* Get a record by collection + record key */
wf_response got = {0};
s = wf_agent_get_record(agent, "app.bsky.labeler.service", rkey, &got);
wf_response_free(&got);

/* Put (upsert) a record at a known rkey */
wf_agent_post_result put = {0};
s = wf_agent_put_record(agent, "app.bsky.labeler.service", rkey, record_json, &put);
wf_agent_post_result_free(&put);

/* List records in a collection */
wf_response listing = {0};
s = wf_agent_list_records(agent, "app.bsky.labeler.service", 50, NULL, &listing);
wf_response_free(&listing);
```

---

## 15. applyWrites batch

Batch multiple create/update/delete operations in one
`com.atproto.repo.applyWrites` call. `rkey` is `NULL` for auto-generated keys on
create; `value_json` is `NULL` for delete.

```c
wf_agent_write writes[2];

writes[0].type        = WF_AGENT_WRITE_CREATE;
writes[0].collection  = "app.bsky.feed.post";
writes[0].rkey        = NULL;                 /* auto-generate */
writes[0].value_json  = post_json;            /* JSON object string */

writes[1].type        = WF_AGENT_WRITE_DELETE;
writes[1].collection  = "app.bsky.graph.follow";
writes[1].rkey        = follow_rkey;          /* required for delete */
writes[1].value_json  = NULL;

wf_response res = {0};
wf_status s = wf_agent_apply_writes(agent, writes, 2, &res);
if (s == WF_OK) {
    printf("applyWrites: %s\n", res.body);
}
wf_response_free(&res);
```

---

## 16. Repo mirror (seed / apply diff / head)

The agent can keep a *local verified mirror* of a repo for offline inspection
and incremental diff application. Set the DID and, when applying signed diffs,
the signing key; seed from a CAR; apply diff CARs; query the head.

```c
#include "wolfram/repo/car.h"   /* wf_car, wf_car_parse, wf_car_free */

/* Seed from a full repo CAR */
unsigned char *car_bytes; size_t car_len;   /* from wf_agent_sync_get_repo / file */
wf_car car = {0};
wf_car_parse(car_bytes, car_len, &car);
wf_status s = wf_agent_set_did(agent, "did:plc:abc123");
s = wf_agent_seed_repo(agent, &car);
wf_car_free(&car);

/* Later, apply an incremental diff CAR (signing key only needed for verify) */
wf_agent_set_signing_key(agent, "did:key:z...priv...");  /* optional */
s = wf_agent_apply_repo_diff(agent, diff_bytes, diff_len);

/* Query the mirror head (owned string) */
char *head = NULL;
s = wf_agent_repo_head(agent, &head);
if (s == WF_OK) {
    printf("mirror head: %s\n", head);
    free(head);
}

/* Read a record out of the mirror */
unsigned char *data = NULL; size_t data_len = 0;
s = wf_agent_mirror_get_record(agent, "app.bsky.feed.post", rkey, &data, &data_len);
/* data is DAG-CBOR bytes; free(data) when done */

/* Invert operations (e.g. to build an undo) */
wf_repo_operation *inverse = NULL;
s = wf_agent_invert_repo_operations(agent, ops, op_count, &inverse);
/* free with wf_repo_operations_free(inverse, inverse_count) */
```

`wf_agent_mirror_get_record` returns heap-owned `data` (free with `free`);
`wf_agent_invert_repo_operations` returns heap-owned `inverse` (free with
`wf_repo_operations_free`).

---

## 17. Server, sync, and misc endpoints

```c
/* Server description (public metadata) */
wf_agent_server_description desc = {0};
wf_status s = wf_agent_describe_server(agent, &desc);
if (s == WF_OK) {
    printf("server DID: %s\n", desc.did);
    for (size_t i = 0; i < desc.available_user_domain_count; ++i)
        printf("  domain: %s\n", desc.available_user_domains[i]);
}
wf_agent_server_description_free(&desc);

/* App passwords */
wf_agent_app_password pwd = {0};
s = wf_agent_create_app_password(agent, "kilo", 0 /* privileged */, &pwd);
wf_agent_app_password_free(&pwd);

wf_agent_app_password_list pwds = {0};
s = wf_agent_list_app_passwords(agent, &pwds);
wf_agent_app_password_list_free(&pwds);

s = wf_agent_revoke_app_password(agent, "kilo");

/* Resolve a handle to a DID */
char *did = NULL;
s = wf_agent_resolve_handle(agent, "alice.bsky.social", &did);
if (s == WF_OK) { printf("did=%s\n", did); free(did); }

/* Sync (no auth required): list blobs for a repo */
wf_response blobs = {0};
s = wf_agent_sync_list_blobs(agent, "did:plc:abc123", 50, NULL, NULL /* since */, &blobs);
wf_response_free(&blobs);

/* Update the caller's profile */
wf_agent_profile_update up = {
    .display_name = "Alice",
    .description  = "Building with wolfram",
    .avatar_cid   = NULL,   /* pass a CID from wf_agent_upload_blob */
    .banner_cid   = NULL,
};
s = wf_agent_update_profile(agent, &up);

/* Push notification registration */
wf_response push = {0};
s = wf_agent_register_push(agent, "did:web:push.example", "device-token", &push);
wf_response_free(&push);
```

---

## Minimal skeleton

```c
#include "wolfram/agent.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <service> <handle> <password>\n", argv[0]);
        return 1;
    }
    wf_agent *agent = wf_agent_new(argv[1]);
    if (!agent) return 1;

    wf_status s = wf_agent_login(agent, argv[2], argv[3]);
    if (s != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result res = {0};
    s = wf_agent_post(agent, "Hello from wolfram!", &res);
    if (s == WF_OK) printf("uri=%s cid=%s\n", res.uri, res.cid);
    wf_agent_post_result_free(&res);

    wf_agent_free(agent);
    return s == WF_OK ? 0 : 1;
}
```
