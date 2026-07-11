#ifndef WOLFRAM_SYNC_SUBSCRIBE_H
#define WOLFRAM_SYNC_SUBSCRIBE_H

#include "wolfram/repo/cid.h"
#include "wolfram/xrpc.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_subscribe_event_type {
    WF_SUBSCRIBE_EVENT_COMMIT,
    WF_SUBSCRIBE_EVENT_SYNC,
    WF_SUBSCRIBE_EVENT_IDENTITY,
    WF_SUBSCRIBE_EVENT_ACCOUNT,
    WF_SUBSCRIBE_EVENT_INFO,
    WF_SUBSCRIBE_EVENT_ERROR,
} wf_subscribe_event_type;

typedef struct wf_subscribe_repo_op {
    char action[8];
    char *path;
    wf_cid cid;
    int has_cid;
    wf_cid prev;
    int has_prev;
} wf_subscribe_repo_op;

typedef struct wf_subscribe_commit {
    int64_t seq;
    char did[256];
    wf_cid commit_cid;
    char rev[64];
    char since[64];
    unsigned char *blocks;
    size_t blocks_len;
    wf_subscribe_repo_op *ops;
    size_t ops_count;
    char time[64];
    wf_cid prev_data;   /* inductive-firehose prev MST root CID (prevData), optional */
    int has_prev_data;
} wf_subscribe_commit;

typedef struct wf_subscribe_sync {
    int64_t seq;
    char did[256];
    unsigned char *blocks;
    size_t blocks_len;
    char rev[64];
    char time[64];
} wf_subscribe_sync;

typedef struct wf_subscribe_identity {
    int64_t seq;
    char did[256];
    char time[64];
    char handle[256];
    int has_handle;
} wf_subscribe_identity;

typedef struct wf_subscribe_account {
    int64_t seq;
    char did[256];
    char time[64];
    int active;
    char status[64];
    int has_status;
} wf_subscribe_account;

typedef struct wf_subscribe_info {
    char name[64];
    char message[512];
    int has_message;
} wf_subscribe_info;

typedef struct wf_subscribe_event {
    wf_subscribe_event_type type;
    int64_t seq;
    union {
        wf_subscribe_commit commit;
        wf_subscribe_sync sync;
        wf_subscribe_identity identity;
        wf_subscribe_account account;
        wf_subscribe_info info;
        struct { char *error; char *message; } error;
    } data;
} wf_subscribe_event;

typedef void (*wf_subscribe_event_cb)(const wf_subscribe_event *event, void *userdata);
typedef void (*wf_subscribe_error_cb)(wf_status status, const char *msg, void *userdata);

typedef struct wf_subscribe_options {
    const char *service;
    int64_t cursor;
    int has_cursor;
    wf_subscribe_event_cb on_event;
    wf_subscribe_error_cb on_error;
    void *userdata;
    int max_retry_seconds;
    int reconnect_delay_ms;
    /* Client keepalive: send a WebSocket PING when no frame has been received
     * for this many milliseconds. 0 selects a 30000ms default. The peer's PONG
     * is auto-handled by libcurl and not surfaced, so this only guards against
     * idle TCP/proxy reaping (no missed-pong termination). */
    uint32_t ping_interval_ms;
} wf_subscribe_options;

typedef struct wf_subscribe_handle wf_subscribe_handle;

wf_status wf_subscribe_start(const wf_subscribe_options *opts, wf_subscribe_handle **out);
void wf_subscribe_stop(wf_subscribe_handle *handle);

/* Decode a single framed subscription message (the exact output of
 * `wf_sync_publish_event` / `wf_sync_publish_error`): a CBOR header map
 * `{ "op": ..., "t": ... }` immediately followed by the CBOR body map.
 *
 * On success `*out` is initialised to an owned event; free any heap members
 * via `wf_subscribe_event_free` (or a manual clear) when done.
 * @return WF_OK on success, WF_ERR_PARSE if the framing or body is invalid. */
wf_status wf_subscribe_decode_frame(const unsigned char *data, size_t len,
                                    wf_subscribe_event *out);

/* Free heap-allocated members of a decoded event (does not free `ev` itself). */
void wf_subscribe_event_free(wf_subscribe_event *ev);

#ifdef __cplusplus
}
#endif

#endif
