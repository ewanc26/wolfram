#ifndef WOLFRAM_XRPC_SERVER_INTERNAL_H
#define WOLFRAM_XRPC_SERVER_INTERNAL_H

/* The full wf_xrpc_server and wf_xrpc_ws_stream layouts, shared between
 * xrpc_server.c (the request dispatcher and server lifecycle, which defines
 * these) and xrpc_server_ws.c (the WebSocket upgrade/framing
 * implementation, which needs the same direct field access the dispatcher
 * has -- server lifecycle teardown walks and closes every open WS stream
 * directly). Not part of the public API -- include/wolfram/xrpc_server.h
 * keeps wf_xrpc_server / wf_xrpc_ws_stream opaque for external consumers;
 * this header is the real definition, visible only within this module. */

#include "wolfram/xrpc_server.h"

#include <microhttpd.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for types whose full layout only xrpc_server.c
 * itself needs (route tables, owned-context list, rate-limit entries,
 * queued-but-not-yet-upgraded connections) -- the struct below only holds
 * pointers to them. Completed later in xrpc_server.c / xrpc_server_ws.c. */
typedef struct wf_static_route wf_static_route;
typedef struct wf_http_route wf_http_route;
typedef struct wf_rate_limit_entry wf_rate_limit_entry;
struct wf_owned_ctx;
struct wf_ws_pending;
typedef struct wf_ws_upgrade_ctx wf_ws_upgrade_ctx;

/* Route entry. Full definition (not just forward-declared) because
 * xrpc_server_ws.c's upgrade worker dereferences route->handler.ws and
 * route->ctx directly. */
typedef enum {
    WF_ROUTE_QUERY,
    WF_ROUTE_PROCEDURE,
} wf_route_kind;

typedef struct wf_route {
    char *nsid;
    wf_route_kind kind;
    bool is_sse;       /* true if this route uses Server-Sent Events */
    bool is_ws;        /* true if this route is a WebSocket subscription */
    bool is_streaming; /* true for bounded-memory POST body delivery */
    union {
        wf_xrpc_query_handler query;
        wf_xrpc_procedure_handler procedure;
        wf_xrpc_streaming_procedure_handler streaming;
        wf_xrpc_sse_handler sse;
        wf_xrpc_ws_handler ws;
    } handler;
    void *ctx;
    struct wf_route *next; /* linked list */
} wf_route;

struct wf_xrpc_server {
    struct MHD_Daemon *daemon;
    uint16_t port;
    wf_route *routes;
    wf_static_route *static_routes;
    wf_http_route *http_routes;
    struct wf_owned_ctx *owned_ctxs; /* heap allocations freed on server free */
    pthread_mutex_t
        routes_mutex; /* guards routes, http_routes, static_routes */
    wf_xrpc_auth_cb auth_cb;
    void *auth_ctx;
    /* Optional handler for NSIDs with no registered route (see
     * wf_xrpc_server_set_fallback). ctx is borrowed, not owned. */
    wf_xrpc_fallback_handler fallback;
    void *fallback_ctx;
    /* Owned context installed by wf_xrpc_server_set_auth_middleware; the
     * server frees it (via auth_mw_free) in wf_xrpc_server_free. Cleared when
     * an external auth callback replaces the middleware. */
    void *auth_mw_ctx;
    void (*auth_mw_free)(void *);
    wf_xrpc_request_observer observer; /* optional, not owned */
    void *observer_ctx;
    wf_rate_limiter *rate_limiter;           /* global IP-based limiter */
    wf_rate_limiter *rate_limiter_owned;     /* non-NULL => server frees it */
    wf_rate_limit_entry *rate_limit_entries; /* per-route list */
    pthread_mutex_t rate_limit_mutex;        /* guards rate_limit_entries */
    wf_xrpc_sse_stream *sse_streams;         /* open SSE connections */
    pthread_mutex_t sse_mutex;               /* guards sse_streams */
    wf_xrpc_ws_stream *ws_streams;           /* open WebSocket connections */
    pthread_mutex_t ws_mutex; /* guards ws_streams + ws_stopping */
    /* Set once teardown has begun. A stream linked after the join loop has
     * drained the list would never be joined, so upgrades are refused from
     * here on rather than accepted into a server that is going away. */
    bool ws_stopping;
    /* Upgrades queued but not yet handed to wf_ws_upgrade_handler. MHD gives
     * no callback when a response is destroyed without upgrading, so a client
     * that disappears between the 101 and the handover would otherwise strand
     * the context and stream forever. Guarded by ws_mutex. */
    struct wf_ws_pending *ws_pending;
    bool cors_enabled; /* emit CORS headers when true */
    char *cors_origin; /* owned Allow-Origin value */
    /* Header to trust for the real client IP instead of the immediate TCP
     * peer (see wf_xrpc_server_set_trusted_client_ip_header); NULL (the
     * default) always uses the raw socket peer address. */
    char *trusted_client_ip_header;
};

/** A live WebSocket connection, created after a successful upgrade. */
struct wf_xrpc_ws_stream {
    struct MHD_Connection *conn;           /* owning MHD connection */
    struct MHD_UpgradeResponseHandle *urh; /* upgrade handle (for close) */
    wf_xrpc_server *server;                /* back-pointer for teardown */
    char *nsid;                            /* route NSID (diagnostics) */
    int sock;                              /* raw socket fd (server->client) */
    pthread_t thread;                      /* upgrade worker thread */
    pthread_mutex_t mutex;      /* guards closed + serialises writes */
    pthread_cond_t worker_cond; /* signalled when refs reach zero */
    unsigned int worker_refs;   /* retained producer workers */
    bool closed;                /* end-of-stream requested */
    /* Set once the spawning thread has recorded `thread`. The worker must not
     * touch (or free) the stream until then -- see wf_ws_upgrade_handler. */
    bool thread_ready;
    /* Set under ws_mutex by wf_xrpc_server_stop when it takes responsibility
     * for joining this worker. The worker detaches itself when this is false,
     * so a connection that ends on its own leaves nothing to reap. Guarded by
     * ws_mutex, not mutex: it is decided at the same moment as list removal. */
    bool reaped;
    struct wf_xrpc_ws_stream *next; /* global list in server */
};

/* Handle a WebSocket upgrade request for `route` on `conn`: validates the
 * handshake headers, queues the upgrade, and responds 101. Returns MHD_YES
 * if the upgrade was queued (caller returns MHD_YES), or MHD_NO if the
 * request was not a valid WebSocket upgrade (caller should send a 400). */
enum MHD_Result wf_server_ws_handshake(wf_xrpc_server *server, wf_route *route,
                                       struct MHD_Connection *conn,
                                       const wf_xrpc_request *request);

/* MHD connection-notification callback: on close, discard any queued
 * upgrade for `conn` that was never handed to wf_ws_upgrade_handler.
 * Installed via MHD_OPTION_NOTIFY_CONNECTION in wf_xrpc_server_start. */
void wf_ws_notify_connection(void *cls, struct MHD_Connection *conn,
                             void **socket_context,
                             enum MHD_ConnectionNotificationCode code);

/* Remove and return the queued upgrade matching `conn` (or `uc` itself, if
 * already taken), or NULL if none is pending. Used both by
 * wf_ws_notify_connection and by wf_xrpc_server_free's teardown of upgrades
 * that never completed. */
wf_ws_upgrade_ctx *wf_ws_pending_take(wf_xrpc_server *server,
                                      struct MHD_Connection *conn,
                                      wf_ws_upgrade_ctx *uc);

/* Discard a queued upgrade context that will never be handed to a worker
 * (connection closed, or server torn down first). Safe to call with NULL. */
void wf_ws_discard_upgrade(wf_ws_upgrade_ctx *uc);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_XRPC_SERVER_INTERNAL_H */
