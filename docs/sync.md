# Repository sync (`sync.h`, `sync_subscribe.h`, `sync_verify.h`)

This module covers AT Protocol repository synchronization: downloading a repo
as a CAR via `com.atproto.sync.getRepo` (full or diff), verifying and importing
CARs, reading blobs/blocks/records, and subscribing to the firehose
(`com.atproto.sync.subscribeRepos`) with commit verification.

All sync endpoints are **unauthenticated** read APIs — you only need an
`wf_xrpc_client` bound to a service URL, not a session.

```c
#include "wolfram/sync.h"
#include "wolfram/xrpc.h"

wf_xrpc_client *client = wf_xrpc_client_new("https://bsky.social"); // needs network
```

> Everything here is marked `// needs network` except the pure parse/verify
> helpers, which also run on bytes you already have on disk.

## Downloading a repo CAR (`wf_sync_get_repo`)

```c
wf_car car = {0};
// Full export: pass NULL for `since`.
wf_status st = wf_sync_get_repo(client, "did:plc:abc", NULL, &car); // needs network
if (st == WF_OK) {
    printf("repo has %zu blocks, %zu root(s)\n", car.block_count, car.root_count);
    // car.roots[0] is the head commit CID; iterate car.blocks[i].data.
}
wf_car_free(&car);  // always free when done

// Diff export: pass a repo revision TID (from wf_sync_get_latest_commit).
wf_car diff = {0};
wf_sync_get_repo(client, "did:plc:abc", "3k7l...rev", &diff); // needs network
wf_car_free(&diff);
```

## Verifying and importing a CAR (`wf_repo_verify`, `wf_repo_import`)

`wf_repo_import` parses raw CAR bytes **and** verifies the signed commit
(signature, block integrity, MST structure) when given the repo's DID and
signing key. `wf_repo_verify` re-verifies an already-parsed `wf_car`.

```c
#include "wolfram/repo/diff.h"

// Raw CAR bytes you obtained (e.g. from wf_sync_get_repo, a file, or the firehose).
unsigned char *bytes = /* ... */; size_t len = /* ... */;

wf_repo_verify_options opts = {
    .expected_did  = "did:plc:abc",
    .signing_key   = "sig-key-jwk-or-multibase",
    .expected_prev = NULL,
};

wf_car car = {0};
wf_commit commit = {0};
wf_status st = wf_repo_import(bytes, len, &opts, &car, &commit); // needs network (DID res for key)
if (st == WF_OK) {
    char *head = wf_cid_to_string(&commit.cid);  // caller frees
    printf("verified head commit: %s\n", head);
    free(head);
}
wf_car_free(&car);
/* wf_commit has no dedicated free; its CIDs are value types embedded in car. */

// If you already parsed the CAR:
wf_car parsed = {0};
wf_car_parse(bytes, len, &parsed);
wf_commit verified = {0};
wf_repo_verify(&parsed, &opts, &verified);
wf_car_free(&parsed);
```

You can also `wf_car_find_block(&car, &cid)` to pull a single block, and
`wf_car_write(&car, &out, &out_len)` to re-serialize a CAR.

## Apply / verify incremental diffs

```c
#include "wolfram/repo/diff.h"

// Verify an incremental CAR against a previously trusted base.
wf_repo_diff diff = {0};
wf_sync_verify_diff_car(&base_car, &base_commit_cid, update_bytes, update_len,
                        &opts, &diff);  // needs network
/* diff.operations describes creates/updates/deletes; diff.new_blocks holds new blocks */
for (size_t i = 0; i < diff.operation_count; i++) {
    wf_repo_operation *op = &diff.operations[i];
    printf("action=%d collection=%s rkey=%s\n", op->action, op->collection, op->rkey);
}
wf_repo_diff_free(&diff);  // frees diff.operations, diff.new_blocks, removed_cids

// Invert a set of operations (e.g. to undo):
wf_repo_operation *inverse = NULL;
wf_repo_operations_invert(diff.operations, diff.operation_count, &inverse);
wf_repo_operations_free(inverse, diff.operation_count);
```

## Blob / blocks / record endpoints

```c
// Download a blob's raw bytes (free *out_data with free()).
unsigned char *data = NULL; size_t data_len = 0;
wf_sync_get_blob(client, "did:plc:abc", "bafyre...cid", &data, &data_len); // needs network
free(data);

// Download specific blocks — parsed into a wf_car (free with wf_car_free).
const char *cids[] = { "bafyre...cid" };
wf_car blocks = {0};
wf_sync_get_blocks(client, "did:plc:abc", cids, 1, &blocks); // needs network
wf_car_free(&blocks);

// Download the block(s) for a single record — also a wf_car.
wf_car rec = {0};
wf_sync_get_record(client, "did:plc:abc", "app.bsky.feed.post", "rkey", &rec); // needs network
wf_car_free(&rec);

// List blob CIDs (free with wf_sync_blob_list_free).
wf_sync_blob_list blobs = {0};
wf_sync_list_blobs(client, "did:plc:abc", NULL, 100, NULL, &blobs); // needs network
wf_sync_blob_list_free(&blobs);

// Repo metadata:
wf_sync_head head = {0};
wf_sync_get_head(client, "did:plc:abc", &head);     // needs network
wf_sync_head_free(&head);

wf_sync_commit_info ci = {0};
wf_sync_get_latest_commit(client, "did:plc:abc", &ci); // needs network
wf_sync_commit_info_free(&ci);

wf_sync_repo_status rs = {0};
wf_sync_get_repo_status(client, "did:plc:abc", &rs);    // needs network
wf_sync_repo_status_free(&rs);

wf_sync_repo_list repos = {0};
wf_sync_list_repos(client, NULL, 100, &repos);          // needs network
wf_sync_repo_list_free(&repos);
```

## Firehose subscription (`sync_subscribe.h`)

`wf_subscribe_start` opens a durable WebSocket to `com.atproto.sync.subscribeRepos`
and delivers parsed events through a callback. It performs exponential-backoff
reconnection using the cursor from the last received event.

```c
#include "wolfram/sync_subscribe.h"

static void on_event(const wf_subscribe_event *ev, void *ud) {
    (void)ud;
    switch (ev->type) {
    case WF_SUBSCRIBE_EVENT_COMMIT: {
        const wf_subscribe_commit *c = &ev->data.commit;
        printf("commit seq=%lld did=%s rev=%s ops=%zu\n",
               (long long)c->seq, c->did, c->rev, c->ops_count);
        // c->blocks / c->blocks_len is the CAR payload for this commit.
        break;
    }
    case WF_SUBSCRIBE_EVENT_IDENTITY:
        printf("identity: %s\n", ev->data.identity.handle);
        break;
    case WF_SUBSCRIBE_EVENT_INFO:
        printf("info: %s\n", ev->data.info.message);
        break;
    default:
        break;
    }
}

static void on_error(wf_status status, const char *msg, void *ud) {
    (void)ud;
    fprintf(stderr, "subscribe error %d: %s\n", status, msg ? msg : "");
}

wf_subscribe_options opts = {
    .service            = "wss://bsky.network",  // needs network
    .on_event           = on_event,
    .on_error           = on_error,
    .userdata           = NULL,
    .max_retry_seconds  = 3600,    // cap on reconnect backoff
    .reconnect_delay_ms = 1000,
    // .has_cursor = 1; .cursor = <last_seq>;  // resume from a cursor
};

wf_subscribe_handle *handle = NULL;
wf_subscribe_start(&opts, &handle);  // blocks, delivering events until stopped

// From another thread / signal handler:
wf_subscribe_stop(handle);
```

`wf_subscribe_start` blocks while the stream is active; the cursor is tracked
internally from received `seq` values, so reconnection automatically resumes
where it left off. Set `.has_cursor = 1` and `.cursor` only to resume from a
persisted sequence you recorded earlier.

## Commit verification (`sync_verify.h`)

`wf_sync_verify_commit` takes a firehose `commit` event, resolves the repo's
DID (via the provided `wf_xrpc_client`) to obtain the signing key, and verifies
the commit signature, block integrity, and MST structure.

```c
#include "wolfram/sync_verify.h"

static void on_commit(const wf_subscribe_event *ev, void *ud) {
    wf_xrpc_client *client = ud;
    if (ev->type != WF_SUBSCRIBE_EVENT_COMMIT) return;

    const wf_subscribe_commit *c = &ev->data.commit;
    int verified = 0;
    wf_commit commit = {0};
    wf_status st = wf_sync_verify_commit(c, client, &verified, &commit); // needs network
    if (st == WF_OK && verified) {
        char *cid = wf_cid_to_string(&commit.cid);
        printf("verified commit %s\n", cid);
        free(cid);
    } else {
        printf("commit FAILED verification (status=%d)\n", st);
    }
}
```

`wf_sync_verify_commit` returns `WF_OK` even when signature verification fails
— always check `*out_verified`. A non-`WF_OK` return means parsing or DID
resolution failed (not a signature failure).
