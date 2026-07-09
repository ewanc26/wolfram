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
typedef struct wf_xrpc_request {
    const char *nsid;          /* Parsed XRPC NSID from URL path */
    const char *method;        /* "GET" or "POST" */
    const char *auth_header;   /* Raw Authorization header (may be NULL) */
    cJSON      *params;        /* Query params (GET) or body JSON (POST); may be NULL */
    void       *handler_ctx;   /* User context from route registration */
} wf_xrpc_request;

/* ------------------------------------------------------------------ */
/* Response — fill in the handler callback, server sends it           */
/* ------------------------------------------------------------------ */
typedef struct wf_xrpc_response {
    int     http_status;       /* HTTP status code (default 200) */
    char   *body;              /* Heap-allocated body (server frees it) */
    size_t  body_len;
} wf_xrpc_response;

/** Zero-initialiser for a response struct. */
#define WF_XRPC_RESPONSE_INIT { 200, NULL, 0 }

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
/* Auth callback (optional)                                            */
/* ------------------------------------------------------------------ */

/** If set, called before each handler. Return WF_OK to proceed;
 *  anything else sends 401. */
typedef wf_status (*wf_xrpc_auth_cb)(const wf_xrpc_request *req, void *ctx);

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

/* ------------------------------------------------------------------ */
/* Auth callback (optional)                                            */
/* ------------------------------------------------------------------ */
/* Auth middleware                                                      */
/* ------------------------------------------------------------------ */

void wf_xrpc_server_set_auth_callback(wf_xrpc_server *server,
                                       wf_xrpc_auth_cb cb, void *ctx);

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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_XRPC_SERVER_H */
