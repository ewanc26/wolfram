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

/* ------------------------------------------------------------------ */
/* Auth middleware                                                      */
/* ------------------------------------------------------------------ */

void wf_xrpc_server_set_auth_callback(wf_xrpc_server *server,
                                       wf_xrpc_auth_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_XRPC_SERVER_H */
