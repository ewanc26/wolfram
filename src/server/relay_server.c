/*
 * relay_server.c — generic upstream→downstream WebSocket subscription relay
 * for the optional XRPC server module.
 *
 * Forwards raw frames from an upstream ws(s):// subscription to downstream
 * clients that connect to a registered NSID. Protocol-agnostic: frames are
 * copied byte-for-byte and never parsed.
 *
 * Requires WOLFRAM_BUILD_SERVER (libmicrohttpd) for the downstream server side;
 * the upstream side reuses the always-built WebSocket client transport.
 */

#include "wolfram/relay_server.h"
#include "wolfram/websocket.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Relay handle (owns the deep-copied config)                          */
/* ------------------------------------------------------------------ */
struct wf_relay_server {
    char    *nsid;               /* owned copy of downstream NSID */
    char    *upstream_url;       /* owned copy of absolute ws(s):// URL */
    uint32_t reconnect_delay_ms; /* 0 disables reconnect */
};

void wf_relay_config_free(wf_relay_config *cfg) {
    if (!cfg) {
        return;
    }
    free(cfg->nsid);
    free(cfg->upstream_url);
    memset(cfg, 0, sizeof(*cfg));
}

static char *relay_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

/* ------------------------------------------------------------------ */
/* Forward loop                                                         */
/* ------------------------------------------------------------------ */

/** Reason a single upstream session ended (drives reconnect policy). */
typedef enum {
    RELAY_END_DOWNSTREAM_CLOSED = 0, /* client went away; never reconnect */
    RELAY_END_UPSTREAM_CLOSED   = 1, /* upstream dropped; reconnect if enabled */
    RELAY_END_CONNECT_FAILED    = 2, /* upstream connect failed; reconnect if enabled */
} relay_end_reason;

/** State handed to the forward worker thread. */
struct relay_fwd {
    wf_relay_server    *relay;
    wf_xrpc_ws_stream  *stream;
};

/**
 * Run one upstream session: connect, then forward every received message
 * downstream until a side closes or errors. Returns the end reason so the
 * caller can decide whether to reconnect.
 *
 * Owns the upstream connection only for the lifetime of this call: on success
 * (or upstream/connect failure) `*up_out` is set to the live connection (or
 * NULL) so the caller frees it after the reconnect decision.
 */
static relay_end_reason relay_run_session(struct relay_fwd *fwd,
                                          wf_websocket **up_out) {
    wf_relay_server   *relay = fwd->relay;
    wf_xrpc_ws_stream *stream = fwd->stream;
    wf_websocket      *up = NULL;

    *up_out = NULL;

    if (wf_websocket_connect(relay->upstream_url, &up) != WF_OK) {
        return RELAY_END_CONNECT_FAILED;
    }
    *up_out = up;

    for (;;) {
        wf_websocket_message msg;
        wf_status rc = wf_websocket_receive(up, &msg);
        if (rc != WF_OK) {
            /* Upstream closed (CURLWS_CLOSE) or errored. */
            return RELAY_END_UPSTREAM_CLOSED;
        }
        /* Forward the raw bytes verbatim. A non-OK result means the downstream
         * stream is already closed/errored — stop reading upstream too. */
        if (wf_xrpc_server_ws_send(stream, msg.data, msg.len) != WF_OK) {
            wf_websocket_message_free(&msg);
            return RELAY_END_DOWNSTREAM_CLOSED;
        }
        wf_websocket_message_free(&msg);
    }
}

/**
 * Forward worker: drives the (possibly reconnecting) upstream session(s) and
 * tears down the downstream stream when done. Runs detached; the server's
 * upgrade worker thread proceeds to its control-frame loop in parallel.
 */
static void *relay_forward(void *arg) {
    struct relay_fwd  *fwd = (struct relay_fwd *)arg;
    wf_relay_server   *relay = fwd->relay;
    wf_xrpc_ws_stream *stream = fwd->stream;

    for (;;) {
        wf_websocket    *up = NULL;
        relay_end_reason reason = relay_run_session(fwd, &up);
        if (up) {
            wf_websocket_free(up);
        }

        if (reason == RELAY_END_DOWNSTREAM_CLOSED) {
            break;
        }
        if (relay->reconnect_delay_ms == 0) {
            break;
        }
        /* Upstream/connect failed with reconnect enabled. If the downstream
         * client has since disconnected, the next session attempt fails fast
         * (send returns WF_ERR_INVALID_ARG) and breaks the loop. Otherwise
         * throttle and retry. */
        usleep(relay->reconnect_delay_ms * 1000u);
    }

    /* End the downstream stream (no-op if already closed by the control loop).
     * The control loop then observes `closed` and exits, closing the socket. */
    wf_xrpc_server_ws_close(stream, 1000);
    wf_xrpc_server_ws_release(stream);
    free(fwd);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* WS handler (runs on the server's upgrade worker thread)             */
/* ------------------------------------------------------------------ */

static wf_status relay_ws_handler(void *ctx, const wf_xrpc_request *req,
                                  wf_xrpc_ws_stream *stream) {
    wf_relay_server  *relay = (wf_relay_server *)ctx;
    struct relay_fwd *fwd;
    pthread_t         tid;
    (void)req;

    fwd = (struct relay_fwd *)malloc(sizeof(*fwd));
    if (!fwd) {
        return WF_ERR_ALLOC;
    }
    fwd->relay = relay;
    fwd->stream = stream;
    if (wf_xrpc_server_ws_retain(stream) != WF_OK) {
        free(fwd);
        return WF_ERR_INVALID_ARG;
    }

    /* Spawn a detached forward worker; the handler must return promptly so the
     * server can run its control-frame loop (ping/pong, client close). */
    if (pthread_create(&tid, NULL, relay_forward, fwd) != 0) {
        wf_xrpc_server_ws_release(stream);
        free(fwd);
        return WF_ERR_ALLOC;
    }
    pthread_detach(tid);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

wf_relay_server *wf_xrpc_server_register_relay(wf_xrpc_server *server,
                                               const wf_relay_config *cfg) {
    wf_relay_server *relay;

    if (!server || !cfg || !cfg->nsid || !cfg->upstream_url) {
        return NULL;
    }
    relay = (wf_relay_server *)calloc(1, sizeof(*relay));
    if (!relay) {
        return NULL;
    }
    relay->nsid = relay_strdup(cfg->nsid);
    relay->upstream_url = relay_strdup(cfg->upstream_url);
    relay->reconnect_delay_ms = cfg->reconnect_delay_ms;
    if (!relay->nsid || !relay->upstream_url) {
        wf_relay_server_free(relay);
        return NULL;
    }

    if (wf_xrpc_server_register_ws(server, relay->nsid,
                                   relay_ws_handler, relay) != WF_OK) {
        wf_relay_server_free(relay);
        return NULL;
    }
    return relay;
}

void wf_relay_server_free(wf_relay_server *relay) {
    if (!relay) {
        return;
    }
    free(relay->nsid);
    free(relay->upstream_url);
    free(relay);
}
