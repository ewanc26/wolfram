/**
 * xrpc.h — low-level XRPC client.
 *
 * Handles the wire-level mechanics of com.atproto's XRPC transport:
 * building `xrpc/{query,procedure}` requests over HTTP, attaching
 * bearer auth, and returning raw response bodies for the caller (or a
 * higher-level layer) to decode.
 *
 * This module does not know about lexicons, JSON schemas, or specific
 * NSIDs beyond the string you hand it. That belongs to a generated
 * layer built on top of this.
 */

#ifndef WOLFRAM_XRPC_H
#define WOLFRAM_XRPC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result/error codes returned by wolfram functions. This is the single
 *  canonical status enum; `wf_error` (in _result.h) is a typedef alias of it. */
typedef enum wf_status {
    WF_OK = 0,
    WF_ERR_INVALID_ARG = 1,
    WF_ERR_ALLOC = 2,
    WF_ERR_NETWORK = 3,
    WF_ERR_HTTP = 4,      /* transport succeeded, server returned non-2xx */
    WF_ERR_PARSE = 5,
    WF_ERR_NOT_FOUND = 6,
    WF_ERR_WOULD_BLOCK = 7, /* non-blocking transport has no data ready yet */
    WF_ERR_DID_RESOLVE = 8,
    WF_ERR_DID_DOCUMENT_NOT_FOUND = 9,
    WF_ERR_HANDLE_RESOLVE = 10,
    WF_ERR_HANDLE_DOCUMENT_NOT_FOUND = 11,
    WF_ERR_HANDLE_TTL_EXPIRED = 12,
    WF_ERR_HANDLE_CACHE_KEY = 13,
    WF_ERR_CRYPTO = 14,
    WF_ERR_VALIDATION = 15,
    WF_ERR_STATE = 16,
    WF_ERR_CONFIG = 17,
    WF_ERR_TIMEOUT = 18,
    WF_ERR_UNSUPPORTED = 19,
    WF_ERR_PERMISSION = 20,
    WF_ERR_RATE_LIMIT = 21,  /* rate limiter rejected the request */
    WF_ERR_DUPLICATE = 22,
    WF_ERR_CONFLICT = 23,
    WF_ERR_NOT_IMPLEMENTED = 24,
    WF_ERR_INTERNAL = 25,
    WF_ERR_UNKNOWN = 26
} wf_status;

/** Opaque XRPC client. Holds the service base URL, auth state, and
 *  the underlying transport handle (libcurl, currently). */
typedef struct wf_xrpc_client wf_xrpc_client;

/** A raw HTTP response. `body` is heap-owned; free with wf_response_free. */
typedef struct wf_response {
    long   status;
    char  *body;
    size_t body_len;
    char  *dpop_nonce; /* optional owned DPoP-Nonce response header */
} wf_response;

typedef struct wf_http_header {
    const char *name;
    const char *value;
} wf_http_header;

/** A single UTF-8 XRPC query parameter. Names and values are URL-encoded. */
typedef struct wf_xrpc_param {
    const char *name;
    const char *value;
} wf_xrpc_param;

/**
 * Create a client bound to a PDS/service base URL, e.g.
 * "https://bsky.social" or a self-hosted PDS such as
 * "https://eurosky.social".
 *
 * Returns NULL on allocation failure.
 */
wf_xrpc_client *wf_xrpc_client_new(const char *service_base_url);

/** Free a client created with wf_xrpc_client_new. Safe to call with NULL. */
void wf_xrpc_client_free(wf_xrpc_client *client);

/**
 * Attach a bearer token (access JWT or service auth token) to all
 * subsequent requests on this client. Pass NULL to clear it.
 */
void wf_xrpc_client_set_auth(wf_xrpc_client *client, const char *access_jwt);

/**
 * Issue an `xrpc/{nsid}` GET (query).
 *
 * `query_string` is the already-encoded query component (without the
 * leading '?'), or NULL for no parameters. Caller owns encoding for
 * now — a params builder can come later.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_response_free.
 */
wf_status wf_xrpc_query(wf_xrpc_client *client,
                         const char *nsid,
                         const char *query_string,
                         wf_response *out);

/**
 * Issue an XRPC query from unencoded name/value parameters.
 *
 * Encoding remains in the transport layer so higher-level protocol APIs do
 * not need to construct URLs. NULL parameter values are rejected.
 */
wf_status wf_xrpc_query_params(wf_xrpc_client *client,
                                const char *nsid,
                                const wf_xrpc_param *params,
                                size_t param_count,
                                wf_response *out);

/**
 * Issue an `xrpc/{nsid}` POST (procedure).
 *
 * `json_body` is a raw, already-serialised JSON payload, or NULL for
 * an empty body. Sets Content-Type: application/json.
 */
wf_status wf_xrpc_procedure(wf_xrpc_client *client,
                             const char *nsid,
                             const char *json_body,
                             wf_response *out);

/**
 * Issue an `xrpc/{nsid}` POST with a binary body.
 *
 * `data` must point to `data_len` bytes and `content_type` is used as the
 * request Content-Type (for example, "image/jpeg"). The response body is
 * JSON and returned in `out->body`.
 */
wf_status wf_xrpc_upload_blob(wf_xrpc_client *client,
                              const char *nsid,
                              const void *data,
                              size_t data_len,
                              const char *content_type,
                              wf_response *out);

/** Release a response's owned buffer. Safe to call on a zeroed struct. */
void wf_response_free(wf_response *res);

/**
 * Decode the atproto XRPC error envelope `{ "error": <str>, "message": <str> }`
 * from a non-OK `wf_response` (i.e. one whose `status >= 400`, typically
 * produced with `WF_ERR_HTTP`).
 *
 * On success returns WF_OK and, for each non-NULL `out_error`/`out_message`,
 * sets it to a caller-owned string (free() it) copied from the envelope.
 * Either field may be absent on the wire; a missing field yields a NULL out
 * pointer without failing the call.
 *
 * Returns WF_ERR_NOT_FOUND if the body is not a JSON object or carries no
 * `error` member (i.e. no envelope), and WF_ERR_PARSE if the envelope is
 * present but malformed. Both `out_error` and `out_message` are left NULL on
 * any non-WF_OK return. Safe to call regardless of `resp->status`.
 */
wf_status wf_xrpc_error(const wf_response *resp,
                        char **out_error, char **out_message);

/**
 * Perform a generic HTTP GET with extra headers.
 *
 * Uses the client's transport and auth settings. `url` must be a complete
 * absolute URL.
 *
 * On WF_OK, `out` is populated and must be released with `wf_response_free`.
 */
wf_status wf_http_get_with_headers(wf_xrpc_client *client, const char *url,
                                  const wf_http_header *extra, size_t extra_count,
                                  wf_response *out);

/**
 * Returns a copy of the client's base URL.
 * The caller owns the returned string and must free it.
 */
char *wf_xrpc_get_base_url(wf_xrpc_client *client);

/** Perform a generic HTTP GET to an arbitrary URL (not an XRPC endpoint).
 *
 * Uses the client's transport and auth settings. `url` must be a complete
 * absolute URL. On WF_OK, `out` is populated and must be released with
 * wf_response_free.
 */
wf_status wf_http_get(wf_xrpc_client *client, const char *url, wf_response *out);

/**
 * Perform a generic HTTP POST. This is the transport primitive used by
 * non-XRPC protocols such as OAuth. On WF_OK or WF_ERR_HTTP, `out` is
 * populated (including an optional DPoP-Nonce) and must be freed.
 */
wf_status wf_http_post(wf_xrpc_client *client, const char *url,
                       const char *content_type, const char *body,
                       const wf_http_header *headers, size_t header_count,
                       wf_response *out);

/**
 * Upload a binary blob to `xrpc/{nsid}` with explicit request headers.
 *
 * Identical to `wf_xrpc_upload_blob` but lets the caller supply extra
 * headers (for example an `Authorization` and `DPoP` proof), since the
 * transport does not otherwise know about session auth. On WF_OK or
 * WF_ERR_HTTP, `out` is populated and must be freed.
 */
wf_status wf_xrpc_upload_blob_with_headers(
    wf_xrpc_client *client, const char *nsid, const void *data,
    size_t data_len, const char *content_type, const wf_http_header *headers,
    size_t header_count, wf_response *out);

/**
 * Test/diagnostic seam: install a handler that replaces real network I/O for
 * every request issued on this client. Intended for offline unit tests;
 * production code must never install a handler. Pass NULL for `fn` to restore
 * the real transport.
 *
 * When a handler is installed it receives the full request (method, URL,
 * content type, body, and headers) and is responsible for populating `out`
 * (owned `body`/`dpop_nonce`) and returning WF_OK for a 2xx response or
 * WF_ERR_HTTP for a non-2xx response. The handler still runs inside the
 * transport module, so network I/O remains isolated to xrpc.c.
 */
typedef wf_status (*wf_xrpc_handler_fn)(void *userdata, const char *method,
                                        const char *url, const char *content_type,
                                        const char *body, size_t body_len,
                                        const wf_http_header *headers,
                                        size_t header_count, wf_response *out);
void wf_xrpc_set_handler(wf_xrpc_client *client, wf_xrpc_handler_fn fn,
                         void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_XRPC_H */
