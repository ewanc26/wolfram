/** jetstream.h — filtered JSON subscriptions to a Jetstream endpoint. */
#ifndef WOLFRAM_JETSTREAM_H
#define WOLFRAM_JETSTREAM_H

#include "wolfram/websocket.h"
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
} wf_jetstream_event_kind;

/** Parsed envelope plus its complete owned JSON. Free with wf_jetstream_event_free. */
typedef struct wf_jetstream_event {
    wf_jetstream_event_kind kind;
    char *did;
    int64_t time_us;
    char *json;
    size_t json_len;
} wf_jetstream_event;

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

#ifdef __cplusplus
}
#endif
#endif
