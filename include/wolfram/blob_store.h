#ifndef WOLFRAM_BLOB_STORE_H
#define WOLFRAM_BLOB_STORE_H

#include "wolfram/util.h"
#include "wolfram/xrpc_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations used by the per-request resolver typedef below. */
typedef struct wf_repo_store wf_repo_store;

/*
 * blob_store.h — a simple, self-contained blob store keyed by CID string.
 *
 * wolfram can act as a small PDS: it stores uploaded blobs and serves them
 * back via com.atproto.repo.uploadBlob / com.atproto.sync.getBlob. The store
 * is intentionally decoupled from the SQLite session/repo-mirror store so it
 * can be used (and tested) independently.
 *
 * Two modes are supported:
 *   - In-memory: pass NULL/"" for the path to wf_blob_store_new. Blobs live
 *     only for the lifetime of the handle.
 *   - File-backed: pass a directory path. Each blob is written as a file named
 *     by its CID (safe base32 charset), with the MIME type in a sidecar
 *     "<cid>.mime" file. Re-opening the same path reloads the blobs.
 *
 * Ownership: outputs from wf_blob_store_get (out_data, out_mime) are
 * heap-allocated and freed with free() by the caller. The CID is the caller's
 * string (e.g. the canonical raw multicodec CID from wf_cid_of_bytes).
 */

typedef struct wf_blob_store wf_blob_store;

/**
 * Create a blob store.
 *
 * @param path  Directory for file-backed storage, or NULL/"" for in-memory.
 * @return Handle, or NULL on allocation/IO failure.
 */
wf_blob_store *wf_blob_store_new(const char *path);

/** Free the store. File-backed blobs are left on disk (caller removes `path`). */
void wf_blob_store_free(wf_blob_store *store);

/**
 * Store a blob under `cid`. `mime_type` is copied. `data`/`len` hold the raw
 * blob bytes. Returns WF_OK, WF_ERR_INVALID_ARG on bad inputs, or
 * WF_ERR_INTERNAL on file IO failure.
 */
wf_status wf_blob_store_put(wf_blob_store *store, const char *cid,
                            const char *mime_type,
                            const unsigned char *data, size_t len);

/**
 * Retrieve a blob. On WF_OK, out_data/out_len/out_mime are set to owned
 * buffers (each freed with free()). Returns WF_ERR_NOT_FOUND if absent.
 */
wf_status wf_blob_store_get(wf_blob_store *store, const char *cid,
                            unsigned char **out_data, size_t *out_len,
                            char **out_mime);

/** Return WF_OK if the blob exists, WF_ERR_NOT_FOUND otherwise. */
wf_status wf_blob_store_exists(wf_blob_store *store, const char *cid);

wf_status wf_blob_store_delete(wf_blob_store *store, const char *cid);

/**
 * Enumerate every stored blob CID. On WF_OK, *out_cids receives a
 * caller-owned NULL-terminated array of CID strings (each freed with
 * free()); use wf_blob_store_list_free to release it. Returns
 * WF_ERR_ALLOC on OOM. The order of CIDs is unspecified.
 */
wf_status wf_blob_store_list(wf_blob_store *store, char ***out_cids,
                             size_t *out_count);

/** Free a CID array returned by wf_blob_store_list. Safe to call with NULL. */
void wf_blob_store_list_free(char **cids, size_t count);

/*
 * Server integration (requires WOLFRAM_BUILD_SERVER). Registers
 * com.atproto.repo.uploadBlob (procedure) and com.atproto.sync.getBlob (query)
 * on `server`, backed by `store`. The upload handler computes the blob's
 * raw multicodec CID, stores it, and returns the TypedBlobRef; the get handler
 * serves the raw bytes with the stored Content-Type. `store` must outlive the
 * server registration.
 */
typedef struct wf_xrpc_server wf_xrpc_server;
wf_status wf_xrpc_server_register_blob_store(wf_xrpc_server *server,
                                              wf_blob_store *store);

/*
 * Per-request resolver for multi-tenant PDS deployments. Given the
 * incoming request, return the repo store (and/or blob store) that
 * should service it, resolved from req->params (did/repo/collection/
 * rkey) and/or req->authed_subject. The returned pointers are BORROWED
 * for the duration of the request and must NOT be freed by the server.
 * If the account cannot be resolved return WF_ERR_NOT_FOUND (or any
 * error) and leave *out_repo / *out_blobs NULL; the handler maps this
 * to a 400 RepoNotFound / AccountNotFound. out_repo / out_blobs may be
 * independently NULL.
 */
typedef wf_status (*wf_xrpc_repo_resolver)(void *ctx,
                                           const wf_xrpc_request *req,
                                           wf_repo_store **out_repo,
                                           wf_blob_store **out_blobs);

/*
 * Register the blob routes (com.atproto.repo.uploadBlob and
 * com.atproto.sync.getBlob) on an XRPC server with a per-request
 * resolver instead of a fixed store, enabling a multi-tenant PDS to
 * serve different accounts from different blob stores.
 *
 * The resolver is invoked for every request; it must return (via
 * out_blobs) the wf_blob_store that should service the request, resolved
 * from req->params / req->authed_subject. The returned store is
 * borrowed for the request duration; the caller keeps ownership of both
 * `ctx` and the stores the resolver returns and must free them after
 * wf_xrpc_server_free. The server owns the internal routing bundle it
 * allocates and frees it on wf_xrpc_server_free.
 */
wf_status wf_xrpc_server_register_blob_store_resolver(
    wf_xrpc_server *server, wf_xrpc_repo_resolver resolver, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_BLOB_STORE_H */
