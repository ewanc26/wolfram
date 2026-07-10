/*
 * relay_server.h — generic upstream→downstream WebSocket subscription relay
 * for the optional XRPC server.
 *
 * Builds on the optional XRPC server module (`xrpc_server.h`, built only when
 * WOLFRAM_BUILD_SERVER=ON) and the always-built WebSocket client transport
 * (`websocket.h`) to forward a binary subscription (e.g.
 * com.atproto.sync.subscribeRepos or com.atproto.label.subscribeLabels) from an
 * upstream ws(s):// endpoint to downstream clients that connect to a registered
 * NSID on the local server.
 *
 * The relay is protocol-agnostic: it forwards raw frames byte-for-byte and does
 * NOT parse them, so it works for any binary subscription.
 */

#ifndef WOLFRAM_RELAY_SERVER_H
#define WOLFRAM_RELAY_SERVER_H

#include "wolfram/xrpc_server.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

/**
 * Relay configuration. Every string field is heap-owned (or NULL) and released
 * by wf_relay_config_free. wf_xrpc_server_register_relay deep-copies the
 * config, so the caller's copy may be released immediately afterwards.
 *
 * `nsid` and `upstream_url` are required. `reconnect_delay_ms` is optional.
 */
typedef struct wf_relay_config {
    char    *nsid;               /* downstream NSID to register (required) */
    char    *upstream_url;       /* absolute ws(s):// URL to relay from (required) */
    uint32_t reconnect_delay_ms; /* upstream reconnect delay; 0 disables reconnect */
} wf_relay_config;

/** Zero-initialiser for a config struct. */
#define WF_RELAY_CONFIG_INIT { 0 }

/** Free all owned strings in a config and zero it. Safe to call on a
 *  zeroed/partially-initialised struct, or with NULL. */
void wf_relay_config_free(wf_relay_config *cfg);

/* ------------------------------------------------------------------ */
/* Relay helper                                                         */
/* ------------------------------------------------------------------ */

/**
 * Opaque relay helper. Owns the deep-copied config produced by
 * wf_xrpc_server_register_relay; free with wf_relay_server_free.
 *
 * Ownership: the server does NOT take ownership of this handle. The registered
 * route's handler context points at it, so it must outlive the server. Free it
 * only AFTER wf_xrpc_server_free has returned (the server's WebSocket upgrade
 * worker threads may still reference it during shutdown). The relay keeps an
 * honest single copy rather than leaking or silently transferring ownership.
 */
typedef struct wf_relay_server wf_relay_server;

/**
 * Register a WebSocket subscription relay on `server`.
 *
 * When a downstream client connects to `cfg->nsid`, the relay opens an upstream
 * WebSocket connection to `cfg->upstream_url` and forwards each received message
 * to the client byte-for-byte until either side closes or errors. The config is
 * deep-copied; on success returns an opaque handle (free with
 * wf_relay_server_free), or NULL on invalid input or registration failure.
 *
 * Threading: the relay's WS handler runs on the server's per-connection upgrade
 * worker thread (as required by the server's streaming contract). It spawns a
 * separate forward worker thread that drives the upstream connect/receive loop
 * and pushes frames with wf_xrpc_server_ws_send; the server thread then runs its
 * control-frame loop (answering client ping, honouring client close). The
 * forward thread closes the downstream stream with wf_xrpc_server_ws_close when
 * the upstream ends or errors.
 *
 * Reconnect: when `reconnect_delay_ms` is non-zero and the upstream connection
 * fails or drops, the forward loop sleeps that long and retries, staying bound
 * to the same downstream client. It stops retrying once the downstream client
 * disconnects (the next forward attempt fails fast) or the server is torn down.
 *
 * Requires a libcurl built with WebSocket support (wf_websocket_supported());
 * without it the upstream connect fails and the relay closes the downstream
 * stream.
 */
wf_relay_server *wf_xrpc_server_register_relay(wf_xrpc_server *server,
                                               const wf_relay_config *cfg);

/**
 * Free a relay handle and its deep-copied config. NULL-safe. Free only after
 * wf_xrpc_server_free has returned (see ownership note above).
 */
void wf_relay_server_free(wf_relay_server *relay);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_RELAY_SERVER_H */
