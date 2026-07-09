/*
 * feedgen_server.h — high-level feed-generator skeleton server helper.
 *
 * Builds on the optional XRPC server module (`xrpc_server.h`, built only when
 * WOLFRAM_BUILD_SERVER=ON) to implement the two endpoints a Bluesky feed
 * generator must serve:
 *
 *   - app.bsky.feed.getFeedSkeleton — delegated to a caller-supplied
 *     callback that returns the list of skeleton posts (AT-URIs).
 *   - app.bsky.feed.getFeedGenerator — synthesised from the supplied
 *     configuration (display name, description, DID, avatar, ...).
 *
 * This complements the always-built skeleton *builder* helper
 * (`wolfram/feedgen.h`, `wf_feedgen_build_skeleton`) — that one renders the
 * JSON for a page; this one runs the server and wires the builder's output to
 * a callback.
 *
 * Ownership:
 *   - wf_feedgen_server_config strings are owned by the config struct and
 *     freed by wf_feedgen_server_config_free. wf_feedgen_server_new takes a
 *     deep copy, so the caller may free its config immediately afterwards.
 *   - The skeleton callback returns owned values: *out_feed is a cJSON array
 *     (freed by the helper after serialisation) and *out_cursor is a heap
 *     string (freed by the helper after serialisation).
 *   - wf_feedgen_server is owned by the caller; free with
 *     wf_feedgen_server_free.
 */

#ifndef WOLFRAM_FEEDGEN_SERVER_H
#define WOLFRAM_FEEDGEN_SERVER_H

#include "wolfram/xrpc_server.h"

#include <cJSON.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

/**
 * Feed generator configuration. Every string field is heap-owned (or NULL)
 * and released by wf_feedgen_server_config_free.
 *
 * `feed_uri` and `did` are required. `display_name` is required for a
 * meaningful generator view; the remaining fields are optional and omitted
 * from the wire response when NULL.
 */
typedef struct wf_feedgen_server_config {
    char *feed_uri;     /* AT-URI of the generator record (required) */
    char *did;          /* Creator / service DID (required) */
    char *handle;       /* Creator handle (optional) */
    char *display_name; /* Human-readable name (required for a view) */
    char *description;  /* Long description (optional) */
    char *avatar;       /* Avatar URI (optional) */
    char *cid;          /* CID of the generator record (optional) */
    int    is_online;   /* Emitted as view isOnline (default: 1) */
    int    is_valid;    /* Emitted as view isValid (default: 1) */
} wf_feedgen_server_config;

/** Zero-initialiser for a config struct. */
#define WF_FEEDGEN_SERVER_CONFIG_INIT { 0 }

/** Free all owned strings in a config and zero it. Safe to call on a
 *  zeroed/partially-initialised struct. */
void wf_feedgen_server_config_free(wf_feedgen_server_config *config);

/* ------------------------------------------------------------------ */
/* Skeleton callback                                                    */
/* ------------------------------------------------------------------ */

/**
 * Callback invoked for app.bsky.feed.getFeedSkeleton.
 *
 * @param ctx      User context supplied to wf_feedgen_server_new.
 * @param feed     The requested feed generator AT-URI (NUL-terminated).
 * @param cursor   Incoming pagination cursor, or NULL/"" for the first page.
 * @param limit    Requested maximum number of results (already clamped to
 *                 1..100 by the helper).
 * @param out_feed On success, set to a freshly allocated cJSON array of
 *                 app.bsky.feed.defs#skeletonFeedPost objects. Ownership
 *                 transfers to the helper, which frees it after serialising.
 * @param out_cursor On success, optionally set to a freshly allocated
 *                 NUL-terminated cursor string (strdup). Ownership transfers
 *                 to the helper, which frees it after serialising. Set to NULL
 *                 (or leave untouched) when there are no further pages.
 * @return WF_OK on success; anything else produces a 500 server error.
 */
typedef wf_status (*wf_feedgen_server_skeleton_cb)(void *ctx,
                                                     const char *feed,
                                                     const char *cursor,
                                                     size_t limit,
                                                     cJSON **out_feed,
                                                     char **out_cursor);

/* ------------------------------------------------------------------ */
/* Feed generator helper                                                */
/* ------------------------------------------------------------------ */

/** Opaque feed generator helper. */
typedef struct wf_feedgen_server wf_feedgen_server;

/**
 * Create a feed generator helper from a configuration and a skeleton callback.
 *
 * The config is deep-copied; `config` may be freed immediately after this
 * call with wf_feedgen_server_config_free. Returns NULL on allocation
 * failure.
 */
wf_feedgen_server *wf_feedgen_server_new(
    const wf_feedgen_server_config *config,
    wf_feedgen_server_skeleton_cb skeleton_cb,
    void *ctx);

/**
 * Start serving on the given address:port. Registers both feed endpoints and
 * begins accepting requests. port 0 selects an ephemeral port (read back with
 * wf_feedgen_server_port). Returns WF_OK on success.
 */
wf_status wf_feedgen_server_start(wf_feedgen_server *fg, const char *address,
                                  uint16_t port, unsigned int thread_count);

/** Bound TCP port (0 if not started). */
uint16_t wf_feedgen_server_port(const wf_feedgen_server *fg);

/** Stop accepting new requests. Safe to call more than once. */
void wf_feedgen_server_stop(wf_feedgen_server *fg);

/** Free the helper, its server, and its copied configuration. Safe with NULL. */
void wf_feedgen_server_free(wf_feedgen_server *fg);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_FEEDGEN_SERVER_H */
