/** jetstream.h — filtered JSON subscriptions to a Jetstream endpoint. */
#ifndef WOLFRAM_JETSTREAM_H
#define WOLFRAM_JETSTREAM_H

#include "wolfram/websocket.h"
#include <cJSON.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_jetstream_options {
    const char *endpoint; /* absolute ws(s) URL, usually ending in /subscribe */
    const char * const *wanted_collections;
    size_t wanted_collections_count;
    const char * const *wanted_dids;
    size_t wanted_dids_count;
    int64_t cursor; /* Unix microseconds; 0 omits the parameter */
    uint32_t max_message_size_bytes; /* 0 means no server-side limit */
    int require_hello; /* pause delivery until the first options update */
    /**
     * Request zstd-compressed binary messages. `zstd_dictionary` must contain
     * the official Jetstream dictionary; it is copied by connect. Requires a
     * build with libzstd.
     */
    int compress;
    const void *zstd_dictionary;
    size_t zstd_dictionary_len;
    /** Non-blocking reconnect backoff. Zero selects 250ms / 30s defaults. */
    uint32_t reconnect_initial_delay_ms;
    uint32_t reconnect_max_delay_ms;
} wf_jetstream_options;

typedef struct wf_jetstream_options_update {
    const char * const *wanted_collections;
    size_t wanted_collections_count;
    const char * const *wanted_dids;
    size_t wanted_dids_count;
    uint32_t max_message_size_bytes;
} wf_jetstream_options_update;

typedef enum wf_jetstream_event_kind {
    WF_JETSTREAM_EVENT_UNKNOWN = 0,
    WF_JETSTREAM_EVENT_COMMIT,
    WF_JETSTREAM_EVENT_IDENTITY,
    WF_JETSTREAM_EVENT_ACCOUNT,
    WF_JETSTREAM_EVENT_ACCOUNT_DELETE,
    WF_JETSTREAM_EVENT_SYNC,
    WF_JETSTREAM_EVENT_FORK,
    WF_JETSTREAM_EVENT_TIMEOUT,
    WF_JETSTREAM_EVENT_MIGRATE,
    WF_JETSTREAM_EVENT_INFO,
} wf_jetstream_event_kind;

/** Parsed envelope plus its complete owned JSON. Free with wf_jetstream_event_free. */
typedef struct wf_jetstream_event {
    wf_jetstream_event_kind kind;
    char *did;
    int64_t time_us;
    char *json;
    size_t json_len;
} wf_jetstream_event;

/**
 * One repo mutation operation inside a commit payload. `cid`/`prev` may be
 * NULL (e.g. deletion has no `cid`). `value` is the owned record JSON for
 * create/update events; `has_value` distinguishes "present" from "absent".
 * The owning `wf_jetstream_event_typed_free` deletes `value`.
 */
typedef struct wf_jetstream_op {
    char *action; /* "create" | "update" | "delete" */
    char *path;   /* "collection/rkey" */
    char *cid;    /* nullable */
    char *prev;   /* nullable */
    cJSON *value; /* owned; only for create/update */
    int has_value;
} wf_jetstream_op;

/** Owned, typed commit payload. `ops` is owned; `blocks` is owned base64 text. */
typedef struct wf_jetstream_commit {
    char *seq;         /* decimal stream sequence as text */
    char *did;
    char *time;
    char *repo;
    char *repo_rev;
    int too_big;
    char *blocks;      /* base64 CAR; owned */
    wf_jetstream_op *ops;
    size_t op_count;
} wf_jetstream_commit;

/** Owned, typed identity payload. */
typedef struct wf_jetstream_identity {
    char *seq;
    char *did;
    char *time;
    char *handle;
} wf_jetstream_identity;

/** Owned, typed account payload. `status` is nullable. */
typedef struct wf_jetstream_account {
    char *seq;
    char *did;
    char *time;
    int active;
    char *status; /* nullable */
} wf_jetstream_account;

/**
 * Owned, typed sync-family payload. Covers fork/timeout/migrate/info; `kind`
 * records which of those triggered the parse so the caller can disambiguate.
 * `seq`/`did`/`time`/`rev`/`too_big` are filled when present in the source.
 */
typedef struct wf_jetstream_sync {
    char *seq;
    char *did;
    char *time;
    char *rev;
    int too_big;
    wf_jetstream_event_kind kind;
} wf_jetstream_sync;

/**
 * Owned, typed event: the envelope `kind`/`did` plus the typed payload of the
 * matching union member. On WF_OK, `*out` is owned by the caller and freed
 * with `wf_jetstream_event_typed_free`; on error it is left reset (no leaks).
 */
typedef struct wf_jetstream_event_typed {
    wf_jetstream_event_kind kind;
    char *did;
    wf_jetstream_commit commit;
    wf_jetstream_identity identity;
    wf_jetstream_account account;
    wf_jetstream_sync sync;
} wf_jetstream_event_typed;

typedef struct wf_jetstream wf_jetstream;

/** Build the subscription URL. Caller owns `*out_url` and must free it. */
wf_status wf_jetstream_build_url(const wf_jetstream_options *options,
                                 char **out_url);

/** Connect using the generic WebSocket transport. Caller owns `*out`. */
wf_status wf_jetstream_connect(const wf_jetstream_options *options,
                               wf_jetstream **out);

/** Encode the official options_update envelope. Caller owns `*out_json`. */
wf_status wf_jetstream_options_update_json(
    const wf_jetstream_options_update *update,
    char **out_json, size_t *out_json_len);

/** Replace filters on an open stream; retry unchanged after WOULD_BLOCK. */
wf_status wf_jetstream_update_options(
    wf_jetstream *stream, const wf_jetstream_options_update *update);

/**
 * Receive and parse one event. On transport failure, schedules a reconnect
 * from the last delivered cursor and returns WF_ERR_WOULD_BLOCK while waiting.
 */
wf_status wf_jetstream_next(wf_jetstream *stream, wf_jetstream_event *out);

/** Milliseconds until the next reconnect attempt, or zero when connected/due. */
uint32_t wf_jetstream_reconnect_after_ms(const wf_jetstream *stream);

/** True when this build can decode Jetstream's zstd binary messages. */
int wf_jetstream_zstd_supported(void);

/** Decode one zstd message with the required Jetstream dictionary. */
wf_status wf_jetstream_event_parse_zstd(const void *compressed,
                                        size_t compressed_len,
                                        const void *dictionary,
                                        size_t dictionary_len,
                                        wf_jetstream_event *out);

/** Parse one uncompressed Jetstream JSON message without doing I/O. */
wf_status wf_jetstream_event_parse(const char *json, size_t json_len,
                                   wf_jetstream_event *out);

void wf_jetstream_event_free(wf_jetstream_event *event);
void wf_jetstream_free(wf_jetstream *stream);

/**
 * Parse one Jetstream JSON message into its typed payload. On WF_OK, `*out`
 * is owned by the caller and freed with `wf_jetstream_event_typed_free`; on
 * error it is left reset (no allocations escape). `time_us` is not required
 * for this parser — only `kind` and `did` gate parsing.
 */
wf_status wf_jetstream_event_parse_typed(const char *json, size_t json_len,
                                         wf_jetstream_event_typed *out);

/** Free a typed event produced by `wf_jetstream_event_parse_typed`. */
void wf_jetstream_event_typed_free(wf_jetstream_event_typed *ev);

#ifdef __cplusplus
}
#endif
#endif
