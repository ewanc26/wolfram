/*
 * xrpc_server.h — minimal XRPC server on libmicrohttpd.
 *
 * Provides an embedded HTTP server that dispatches incoming `/xrpc/<nsid>`
 * requests to registered handler callbacks. Queries (GET) and procedures
 * (POST) are dispatched separately. An optional auth callback is called
 * before each handler.
 *
 * This module is built only when WOLFRAM_BUILD_SERVER=ON and requires
 * libmicrohttpd.
 *
 * Ownership:
 *   - wf_xrpc_server is owned by the caller; free with wf_xrpc_server_free.
 *   - wf_xrpc_response body is freed by the server after sending.
 *   - Handler callbacks must not retain pointers into wf_xrpc_request after
 *     returning.
 */

#ifndef WOLFRAM_XRPC_SERVER_H
#define WOLFRAM_XRPC_SERVER_H

#include "wolfram/xrpc.h"
#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Opaque server handle                                                */
/* ------------------------------------------------------------------ */
typedef struct wf_xrpc_server wf_xrpc_server;

/* ------------------------------------------------------------------ */
/* Request context — valid only during the handler callback           */
/* ------------------------------------------------------------------ */
/** Kind of principal an auth middleware resolved for a request. */
typedef enum {
    WF_XRPC_PRINCIPAL_NONE = 0,
    WF_XRPC_PRINCIPAL_SERVICE,  /* app.bsky service JWT (iss = service DID) */
    WF_XRPC_PRINCIPAL_USER,     /* OAuth/OIDC access token (sub = user DID) */
} wf_xrpc_principal_kind;

typedef struct wf_xrpc_request {
    const char *nsid;          /* Parsed XRPC NSID from URL path */
    const char *path;          /* Exact request path (for non-XRPC routes). */
    const char *method;        /* "GET" or "POST" */
    const char *auth_header;   /* Raw Authorization header (may be NULL) */
    const char *dpop_header;   /* Raw DPoP proof header (may be NULL) */
    cJSON      *params;        /* Query params (GET) or body JSON (POST); may be NULL */
    /* Raw request body (POST). For binary procedures such as blob uploads the
     * body is the raw bytes, not JSON; handlers that need it read body /
     * body_len directly. Valid only during the handler callback. */
    const unsigned char *body;
    size_t      body_len;
    /* Request Content-Type header (may be NULL). Valid only during the handler. */
    const char *content_type;
    void       *handler_ctx;   /* User context from route registration */
    /* The following two fields are populated by an auth middleware (e.g.
     * wf_xrpc_server_set_auth_middleware) and are valid only for the duration
     * of the handler callback. `authed_subject` is server-owned and freed after
     * the handler returns; NULL when no principal was authenticated. */
    char       *authed_subject;       /* resolved DID (iss for service tokens,
                                         sub for user tokens) */
    wf_xrpc_principal_kind authed_principal_kind; /* kind of `authed_subject` */
} wf_xrpc_request;

typedef struct wf_xrpc_response_header wf_xrpc_response_header;

/* ------------------------------------------------------------------ */
/* Response — fill in the handler callback, server sends it           */
/* ------------------------------------------------------------------ */
typedef struct wf_xrpc_response {
    int     http_status;       /* HTTP status code (default 200) */
    char   *body;              /* Heap-allocated body (server frees it) */
    size_t  body_len;
    /* Optional response Content-Type. When non-NULL the server emits this
     * header instead of the default "application/json" (used to serve raw
     * blobs). Owned by the handler; the server frees it after sending. */
    char   *content_type;
    wf_xrpc_response_header *headers; /* private owned response-header list */
} wf_xrpc_response;

/** Zero-initialiser for a response struct. */
#define WF_XRPC_RESPONSE_INIT { 200, NULL, 0, NULL, NULL }

/** Set the response body (copies the string). */
void wf_xrpc_response_set_body(wf_xrpc_response *resp,
                               const char *body, size_t body_len);

/** Set an XRPC error response: {"error":"...","message":"..."} */
void wf_xrpc_response_set_error(wf_xrpc_response *resp,
                                int http_status,
                                const char *error,
                                const char *message);

/** Set a generic error body with the given HTTP status. */
void wf_xrpc_response_set_error_body(wf_xrpc_response *resp,
                                      int http_status,
                                      const char *body, size_t body_len);

/** Set the response Content-Type (copies the string). Pass NULL to clear. */
void wf_xrpc_response_set_content_type(wf_xrpc_response *resp,
                                        const char *content_type);

/** Add a response header (copies name and value). */
wf_status wf_xrpc_response_add_header(wf_xrpc_response *resp,
                                      const char *name, const char *value);

/* ------------------------------------------------------------------ */
/* Handler callbacks                                                   */
/* ------------------------------------------------------------------ */

/** Query handler (GET /xrpc/<nsid>). Return WF_OK to send resp. */
typedef wf_status (*wf_xrpc_query_handler)(void *ctx,
                                            const wf_xrpc_request *req,
                                            wf_xrpc_response *resp);

/** Procedure handler (POST /xrpc/<nsid>). Return WF_OK to send resp. */
typedef wf_status (*wf_xrpc_procedure_handler)(void *ctx,
                                                const wf_xrpc_request *req,
                                                wf_xrpc_response *resp);

/** Handler for an exact-path GET or POST route outside /xrpc. */
typedef wf_status (*wf_http_route_handler)(void *ctx,
                                           const wf_xrpc_request *req,
                                           wf_xrpc_response *resp);

/* ------------------------------------------------------------------ */
/* SSE (Server-Sent Events) streaming                                  */
/* ------------------------------------------------------------------ */

/**
 * Opaque handle for an open Server-Sent Events stream.
 *
 * Created by the server when an SSE route is invoked. Pass it to
 * wf_xrpc_server_sse_send / wf_xrpc_server_sse_close from a worker thread
 * (NOT from inside the SSE handler itself) to push frames and then close the
 * stream. The server owns the handle and frees it once the connection ends.
 */
typedef struct wf_xrpc_sse_stream wf_xrpc_sse_stream;

/**
 * SSE handler (GET /xrpc/<nsid> registered as SSE).
 *
 * The handler should return quickly. To stream, spawn a worker thread that
 * calls wf_xrpc_server_sse_send / wf_xrpc_server_sse_close; the connection is
 * left open (suspended) until the stream is closed. A handler that sends one
 * frame and immediately closes produces a single-shot SSE response.
 */
typedef wf_status (*wf_xrpc_sse_handler)(void *ctx,
                                          const wf_xrpc_request *req,
                                          wf_xrpc_sse_stream *stream);

/* ------------------------------------------------------------------ */
/* WebSocket (RFC 6455) subscription endpoints                         */
/* ------------------------------------------------------------------ */

/**
 * Opaque handle for an open WebSocket connection (server → client push).
 *
 * Created by the server after a successful RFC 6455 upgrade. Pass it to
 * wf_xrpc_server_ws_send / wf_xrpc_server_ws_close from a worker thread
 * (NOT from inside the WS handler itself) to push binary frames and then
 * close the connection. The server owns the handle and frees it once the
 * connection ends (after the upgrade worker thread returns).
 *
 * Thread-safety: wf_xrpc_server_ws_send / wf_xrpc_server_ws_close are
 * serialised against each other and against the server's control-frame
 * reader by an internal mutex, so they may be called from any thread that
 * holds the stream (typically a worker the handler spawned). All server →
 * client frames are sent UNMASKED per RFC 6455; client frames are masked
 * and only control frames (close / ping) are processed (ping is answered
 * with pong). Inbound data frames are drained and ignored.
 */
typedef struct wf_xrpc_ws_stream wf_xrpc_ws_stream;

/**
 * WebSocket handler (GET /xrpc/<nsid> registered as a WS subscription).
 *
 * Invoked after the RFC 6455 handshake completes (101 Switching Protocols),
 * from the connection's upgrade worker thread. The handler should return
 * promptly. Before spawning a worker, retain the stream with
 * wf_xrpc_server_ws_retain; the worker must release it after its final
 * send/close. The connection is kept open until wf_xrpc_server_ws_close is
 * called or the client closes it. A handler that sends one frame and closes
 * produces a single-shot stream.
 */
typedef wf_status (*wf_xrpc_ws_handler)(void *ctx,
                                        const wf_xrpc_request *req,
                                        wf_xrpc_ws_stream *stream);

/* ------------------------------------------------------------------ */
/* Auth callback (optional)                                            */
/* ------------------------------------------------------------------ */

/** If set, called before each handler. Return WF_OK to proceed;
 *  anything else sends 401. The callback may populate `req->authed_subject`
 *  (a heap string it owns) and `req->authed_principal_kind` so the handler can
 *  read the authenticated principal. The server takes ownership of
 *  `authed_subject` and frees it after the handler returns. */
typedef wf_status (*wf_xrpc_auth_cb)(wf_xrpc_request *req, void *ctx);

/* ------------------------------------------------------------------ */
/* Server lifecycle                                                    */
/* ------------------------------------------------------------------ */

/**
 * Start an XRPC server on the given address:port.
 *
 * @param address   Listen address ("0.0.0.0", "127.0.0.1", "::1", etc.)
 * @param port      TCP port (0 = ephemeral, query with wf_xrpc_server_port)
 * @param thread_count  Number of worker threads (0 = default).
 * @return Server handle, or NULL on failure.
 */
wf_xrpc_server *wf_xrpc_server_start(const char *address, uint16_t port,
                                      unsigned int thread_count);

/** Stop accepting new requests. Safe to call more than once. */
void wf_xrpc_server_stop(wf_xrpc_server *server);

/** Free the server and all registered data. Safe to call with NULL. */
void wf_xrpc_server_free(wf_xrpc_server *server);

/** Return the bound TCP port (0 if not started). */
uint16_t wf_xrpc_server_port(const wf_xrpc_server *server);

/**
 * Register a fixed public GET response outside the /xrpc namespace (for
 * example a /.well-known document). The server copies all strings.
 */
wf_status wf_xrpc_server_register_static_get(wf_xrpc_server *server,
                                             const char *path,
                                             const char *content_type,
                                             const void *body,
                                             size_t body_len);

/**
 * Register a dynamic public route outside /xrpc. `method` must be GET or POST.
 * POST bodies are exposed through req->body and req->body_len; JSON bodies are
 * additionally decoded into req->params. These routes deliberately bypass the
 * XRPC auth callback so protocol-specific handlers can authenticate themselves.
 */
wf_status wf_xrpc_server_register_http_route(wf_xrpc_server *server,
                                             const char *method,
                                             const char *path,
                                             wf_http_route_handler handler,
                                             void *ctx);

/* ------------------------------------------------------------------ */
/* Route registration                                                  */
/* ------------------------------------------------------------------ */

wf_status wf_xrpc_server_register_query(wf_xrpc_server *server,
                                         const char *nsid,
                                         wf_xrpc_query_handler handler,
                                         void *ctx);

wf_status wf_xrpc_server_register_procedure(wf_xrpc_server *server,
                                          const char *nsid,
                                          wf_xrpc_procedure_handler handler,
                                          void *ctx);

/* Register an SSE (Server-Sent Events) endpoint (GET). The connection is kept
 * open and frames are pushed with wf_xrpc_server_sse_send until closed with
 * wf_xrpc_server_sse_close. A handler that sends a single frame and closes
 * produces a single-shot SSE response. */
wf_status wf_xrpc_server_register_sse(wf_xrpc_server *server,
                                      const char *nsid,
                                      wf_xrpc_sse_handler handler,
                                      void *ctx);

/**
 * Push a single SSE frame to an open stream.
 *
 * Formats `data: <payload>\n\n` (optionally preceded by `event: <event>\n`
 * when `event` is non-NULL and non-empty). Newlines in `data` are expanded
 * into multiple `data:` lines per the SSE spec. The frame is buffered and the
 * suspended connection is resumed so libmicrohttpd flushes it to the client.
 *
 * Must be called from a context other than the SSE handler invocation
 * (typically a dedicated worker thread). Returns WF_ERR_INVALID_ARG if the
 * stream is already closed.
 */
wf_status wf_xrpc_server_sse_send(wf_xrpc_sse_stream *stream,
                                  const char *event, const char *data);

/**
 * Push a pre-formatted raw frame to an open stream. The caller is responsible
 * for SSE framing (`data: ...\n\n`). The connection is resumed after queuing.
 */
wf_status wf_xrpc_server_sse_send_raw(wf_xrpc_sse_stream *stream,
                                      const char *frame, size_t len);

/**
 * Close an SSE stream. Any buffered frames are flushed, then the connection is
 * ended (HTTP chunk terminated / connection closed) and the stream handle is
 * released by libmicrohttpd. Subsequent sends return WF_ERR_INVALID_ARG.
 */
wf_status wf_xrpc_server_sse_close(wf_xrpc_sse_stream *stream);

/**
 * Register a WebSocket (RFC 6455) subscription endpoint (GET). On a valid
 * upgrade request the server completes the handshake (101 Switching
 * Protocols) and invokes `handler` with a live `wf_xrpc_ws_stream`; the
 * handler pushes binary frames with wf_xrpc_server_ws_send and ends the
 * stream with wf_xrpc_server_ws_close. An invalid or non-upgrade request
 * receives a 400 error. Enough WebSocket machinery is provided for
 * server → client subscription streaming: binary and close frames, and
 * answering client ping with pong.
 */
wf_status wf_xrpc_server_register_ws(wf_xrpc_server *server,
                                     const char *nsid,
                                     wf_xrpc_ws_handler handler,
                                     void *ctx);

/**
 * Send a single binary (opcode 0x2) WebSocket frame to the client.
 *
 * Must be called from a context other than the WS handler invocation
 * (typically a dedicated worker thread holding a retained reference). Returns
 * WF_ERR_INVALID_ARG if the stream is already closed. Server → client frames
 * are sent UNMASKED.
 */
wf_status wf_xrpc_server_ws_send(wf_xrpc_ws_stream *stream,
                                 const void *data, size_t len);

/**
 * Retain a WebSocket stream for use by a worker thread. A successful retain
 * must be paired with wf_xrpc_server_ws_release. Returns WF_ERR_INVALID_ARG
 * when the stream is already closing.
 */
wf_status wf_xrpc_server_ws_retain(wf_xrpc_ws_stream *stream);

/** Release a worker reference acquired by wf_xrpc_server_ws_retain. */
void wf_xrpc_server_ws_release(wf_xrpc_ws_stream *stream);

/** Return non-zero when the stream is closing or has been closed. */
int wf_xrpc_server_ws_is_closed(wf_xrpc_ws_stream *stream);

/**
 * Send a close (opcode 0x8) frame carrying `code` and tear down the
 * connection. The upgrade worker thread then ends the stream and the
 * socket is closed via libmicrohttpd. Subsequent sends return
 * WF_ERR_INVALID_ARG.
 */
wf_status wf_xrpc_server_ws_close(wf_xrpc_ws_stream *stream, uint16_t code);

/* ------------------------------------------------------------------ */
/* Auth callback (optional)                                            */
/* ------------------------------------------------------------------ */
/* Auth middleware                                                      */
/* ------------------------------------------------------------------ */

void wf_xrpc_server_set_auth_callback(wf_xrpc_server *server,
                                        wf_xrpc_auth_cb cb, void *ctx);

/**
 * Like wf_xrpc_server_set_auth_callback, but also records an owned middleware
 * context (`mw_ctx`) that the server frees via `mw_free` in wf_xrpc_server_free.
 * Installing a different auth callback (via wf_xrpc_server_set_auth_callback)
 * releases any previously recorded middleware context. Used by
 * wf_xrpc_server_set_auth_middleware; not generally needed by callers.
 */
void wf_xrpc_server_set_auth_callback_owned(wf_xrpc_server *server,
                                            wf_xrpc_auth_cb cb, void *ctx,
                                            void *mw_ctx,
                                            void (*mw_free)(void *));

/* ------------------------------------------------------------------ */
/* Rate limiter (optional, per-server-mount or per-route)              */
/* ------------------------------------------------------------------ */

/** Opaque token-bucket rate limiter handle. */
typedef struct wf_rate_limiter wf_rate_limiter;

/**
 * Create a memory-backed token-bucket rate limiter.
 *
 * @param points            Maximum tokens the bucket can hold (burst).
 * @param duration_seconds  Time window in seconds for token refill.
 *                          Tokens refill at `points / duration_seconds` per second.
 * @param bucket_count      Number of buckets (keys tracked simultaneously).
 *                          0 uses a default (256). Each bucket is a linked-list
 *                          node; higher values reduce hash collisions.
 * @return  Handle, or NULL on allocation failure.
 */
wf_rate_limiter *wf_rate_limiter_new(unsigned int points,
                                      unsigned int duration_seconds,
                                      unsigned int bucket_count);

/** Free a rate limiter. Safe to call with NULL. */
void wf_rate_limiter_free(wf_rate_limiter *rl);

/**
 * Try to consume `cost` tokens for the given `key`.
 *
 * @param rl    Rate limiter handle.
 * @param key   NUL-terminated key string (e.g. client IP, auth token).
 * @param cost  Number of tokens to consume (1 for a normal request).
 * @param out_retry_after  If non-NULL and rate limited, set to the number of
 *              seconds the caller should wait before retrying.
 * @return WF_OK if tokens were consumed, WF_ERR_RATE_LIMIT if the bucket is
 *         empty, WF_ERR_INVALID_ARG on bad inputs.
 */
wf_status wf_rate_limiter_consume(wf_rate_limiter *rl,
                                   const char *key,
                                   unsigned int cost,
                                   unsigned int *out_retry_after);

/* Attach a rate limiter to a server. When set, every request is charged
 * 1 token against the client's IP address before the auth callback.
 * Passing NULL removes the rate limiter. */
void wf_xrpc_server_set_rate_limiter(wf_xrpc_server *server,
                                       wf_rate_limiter *rl);

/**
 * Attach a rate limiter the server owns. Unlike wf_xrpc_server_set_rate_limiter
 * (which borrows the handle), the server frees it in wf_xrpc_server_free.
 * Passing NULL is a no-op. Primarily used by wf_xrpc_server_new_with_config.
 */
void wf_xrpc_server_set_rate_limiter_owned(wf_xrpc_server *server,
                                             wf_rate_limiter *rl);

/* Attach a per-route rate limiter. Requests to this exact method/nsid
 * will be charged `rl` tokens (defaults to IP-based limiter if none set).
 * Passing NULL removes that route's rate limiter.
 *
 * The route is identified by the exact method ("GET" or "POST") and the
 * full URL path as it appears in the request (e.g. "/xrpc/io.example.ping").
 */
void wf_xrpc_server_set_route_rate_limiter(wf_xrpc_server *server,
                                            const char *method,
                                            const char *url,
                                            wf_rate_limiter *rl);

/* ------------------------------------------------------------------ */
/* CORS configuration                                                   */
/* ------------------------------------------------------------------ */

/**
 * Enable/disable CORS and set the allowed origin returned in
 * `Access-Control-Allow-Origin`.
 *
 * When `enabled` is false, no CORS headers are emitted. When true, the
 * origin is taken from `origin` (which may be "*", a single origin, or NULL
 * to fall back to "*"). The server takes a copy of `origin`.
 */
void wf_xrpc_server_set_cors(wf_xrpc_server *server, bool enabled,
                              const char *origin);

/* ------------------------------------------------------------------ */
/* JSON configuration loader                                            */
/* ------------------------------------------------------------------ */

/** Method discriminator for a configured route entry. */
typedef enum {
    WF_XRPC_CONFIG_METHOD_QUERY = 0,
    WF_XRPC_CONFIG_METHOD_PROCEDURE = 1,
} wf_xrpc_server_config_method;

/** A single configured route entry: NSID + method. */
typedef struct wf_xrpc_server_config_route {
    char *nsid;   /* owned; e.g. "io.example.ping" */
    int    method; /* wf_xrpc_server_config_method */
} wf_xrpc_server_config_route;

/**
 * Owned XRPC server configuration, parsed from JSON.
 *
 * Ownership: produced by wf_xrpc_server_config_parse and freed with
 * wf_xrpc_server_config_free. All string members are owned copies.
 *
 * Defaults (applied when the corresponding field is absent or invalid):
 *   - host            "0.0.0.0"
 *   - port            8080
 *   - cors_enabled    true
 *   - cors_origin     "*"
 *   - rate_limit      disabled (max_tokens == 0 || refill_per_second == 0)
 *   - routes          empty
 *
 * The JSON schema is:
 * {
 *   "host": "0.0.0.0",
 *   "port": 8080,
 *   "cors": { "enabled": true, "allowed_origin": "*" },
 *   "rate_limit": { "max_tokens": 100, "refill_per_second": 10 },
 *   "routes": [ { "nsid": "io.example.ping", "method": "query" } ]
 * }
 *
 * Unknown/extra fields are ignored. Malformed JSON returns WF_ERR_CONFIG.
 */
typedef struct wf_xrpc_server_config {
    char    *host;               /* owned; default "0.0.0.0" */
    uint16_t port;               /* default 8080 */
    bool     cors_enabled;       /* default true */
    char    *cors_origin;        /* owned; default "*" */
    struct {
        unsigned int max_tokens;        /* burst capacity; 0 disables */
        unsigned int refill_per_second; /* tokens refilled per second */
    } rate_limit;
    wf_xrpc_server_config_route *routes; /* owned array; route_count entries */
    size_t route_count;
} wf_xrpc_server_config;

/**
 * Parse an XRPC server config from a JSON buffer of `len` bytes.
 *
 * @param json  NUL-terminated-or-not JSON text (len bytes parsed exactly).
 * @param len   Number of bytes in `json`.
 * @param out   On WF_OK, set to a newly allocated config (free with
 *              wf_xrpc_server_config_free). Set to NULL on error.
 * @return WF_OK on success, WF_ERR_CONFIG on malformed JSON,
 *         WF_ERR_ALLOC on allocation failure, WF_ERR_INVALID_ARG on NULL
 *         inputs. On any error *out is NULL and nothing is leaked.
 */
wf_status wf_xrpc_server_config_parse(const char *json, size_t len,
                                       wf_xrpc_server_config **out);

/** Free a config produced by wf_xrpc_server_config_parse. NULL-safe. */
void wf_xrpc_server_config_free(wf_xrpc_server_config *cfg);

/**
 * Create and start an XRPC server from a parsed config.
 *
 * Applies the configured host/port, CORS settings, and (if enabled) a global
 * per-IP token-bucket rate limiter derived from `rate_limit`. Each configured
 * route is registered with a built-in echo handler (it replies 200 with the
 * NSID/method and a copy of the request params/body) so the server is
 * immediately usable for smoke testing. Callers that need real handlers should
 * additionally register them with wf_xrpc_server_register_query/procedure.
 *
 * @param cfg  Parsed config (must not be NULL).
 * @param out  On WF_OK, set to a started server (free with
 *             wf_xrpc_server_free). NULL on error.
 * @return WF_OK, WF_ERR_INVALID_ARG, or WF_ERR_INTERNAL on start failure.
 */
wf_status wf_xrpc_server_new_with_config(const wf_xrpc_server_config *cfg,
                                         wf_xrpc_server **out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_XRPC_SERVER_H */
