#ifndef WOLFRAM_LABEL_H
#define WOLFRAM_LABEL_H

#include "wolfram/websocket.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_LABEL_SUBSCRIBE_NSID "com.atproto.label.subscribeLabels"

/* A single label record from the subscribeLabels stream. */
typedef struct wf_label {
    int64_t seq;
    int64_t ver;
    int has_ver;
    char *src;
    char *uri;
    char *cid;
    int has_cid;
    char *val;
    int neg;
    int has_neg;
    char *cts;
    char *exp;
    int has_exp;
    char *sig;
    int has_sig;
} wf_label;

typedef struct wf_label_batch {
    int64_t seq;
    wf_label *items;
    size_t count;
} wf_label_batch;

typedef struct wf_label_info {
    char *name;
    char *message;
    int has_message;
} wf_label_info;

typedef enum wf_label_message_type {
    WF_LABEL_MESSAGE_NONE = 0,
    WF_LABEL_MESSAGE_LABELS,
    WF_LABEL_MESSAGE_INFO,
} wf_label_message_type;

typedef struct wf_label_message {
    wf_label_message_type type;
    union {
        wf_label_batch labels;
        wf_label_info info;
    } data;
} wf_label_message;

typedef void (*wf_label_cb)(const wf_label *label, void *userdata);
typedef void (*wf_label_info_cb)(const wf_label_info *info, void *userdata);
typedef void (*wf_label_error_cb)(wf_status status, const char *message, void *userdata);

typedef struct wf_label_subscribe_options {
    const char *service;      /* absolute ws(s) or http(s) service URL */
    int64_t cursor;           /* last seen sequence number; 0 omits the query */
    int has_cursor;
    uint32_t reconnect_delay_ms; /* 0 selects the default 3000ms backoff */
    wf_label_cb on_label;
    wf_label_cb on_neg;
    wf_label_info_cb on_info;
    wf_label_error_cb on_error;
    void *userdata;
} wf_label_subscribe_options;

typedef struct wf_label_subscribe_handle wf_label_subscribe_handle;

/** Build the subscribeLabels WebSocket URL. Caller owns `*out_url`. */
wf_status wf_label_build_url(const char *service, int64_t cursor,
                             char **out_url);

/** Parse one subscribeLabels frame. Caller owns any allocations in `*out`. */
wf_status wf_label_message_parse(const char *json, size_t json_len,
                                 wf_label_message *out);

/** Free a parsed subscribeLabels frame. */
void wf_label_message_free(wf_label_message *message);

/**
 * Subscribe to labels from a labeler service. The call blocks until stopped,
 * a fatal initial connection error occurs, or the service closes cleanly.
 */
wf_status wf_label_subscribe_start(const wf_label_subscribe_options *opts,
                                   wf_label_subscribe_handle **out);

/** Request that an active subscription loop stop as soon as possible. */
void wf_label_subscribe_stop(wf_label_subscribe_handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_LABEL_H */
