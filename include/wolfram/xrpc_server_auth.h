/*
 * xrpc_server_auth.h — service-token / Bearer auth middleware for the XRPC
 * server.
 *
 * Wires the OAuth/service-auth verification primitives (wf_oauth_verify_*
 * and wf_server_verify_service_auth) into the server as a real auth
 * middleware installed via wf_xrpc_server_set_auth_middleware.
 *
 * Built only when WOLFRAM_BUILD_SERVER=ON (mirrors xrpc_server / relay_server).
 *
 * Ownership:
 *   - wf_xrpc_server_auth_config is owned by the caller; free with
 *     wf_xrpc_server_auth_config_free. The middleware deep-copies it at attach
 *     time, so the caller may free the config immediately afterwards.
 *   - The authenticated principal (req->authed_subject) is set on the request
 *     by the middleware and owned/freed by the server after the handler runs.
 *   - resolver / trusted_keys / replay_cache pointers inside the config are
 *     borrowed; the caller retains ownership of referenced objects and must
 *     keep them alive for the server's lifetime.
 */

#ifndef WOLFRAM_XRPC_SERVER_AUTH_H
#define WOLFRAM_XRPC_SERVER_AUTH_H

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* DID-key resolver                                                    */
/* ------------------------------------------------------------------ */

/**
 * Map a DID to its public verification key as a `did:key:z...` (or bare
 * `z...` multibase) string, suitable for passing to wf_server_verify_service_auth
 * or wf_verify. On WF_OK, *out_didkey is heap-allocated and owned by the caller
 * (free() it). Used to fetch the issuer's signing key during token verification.
 *
 * The built-in default resolver handles `did:key:` locally (no network) and,
 * for other methods, defers to an optional wf_xrpc_client supplied via the
 * config (wf_did_resolve). Callers may inject a custom resolver to avoid the
 * network entirely or to use a cached directory. The middleware may call the
 * resolver a second time after signature verification fails, allowing the
 * callback to refresh a stale cached signing key after issuer key rotation.
 */
typedef wf_status (*wf_xrpc_server_auth_resolve_fn)(const char *did,
                                                    char **out_didkey,
                                                    void *ctx);

/* ------------------------------------------------------------------ */
/* Owned configuration                                                 */
/* ------------------------------------------------------------------ */

typedef struct wf_xrpc_server_auth_config {
    char *server_did;          /* owned; this server's DID, used for `aud` checks */
    char *server_origin;       /* owned external origin, used for DPoP `htu` */
    char **protected_nsids;    /* owned array of NSID prefixes requiring auth */
    size_t protected_nsid_count;
    int    require_aud;        /* non-zero => enforce token `aud` == server_did */
    int    require_lxm;        /* non-zero => enforce token `lxm` == request NSID */
    wf_xrpc_client *resolver_client; /* borrowed; used by default resolver for
                                         non-did:key DIDs (NULL => did:key only) */
    wf_xrpc_server_auth_resolve_fn resolver; /* NULL => built-in default resolver */
    void *resolver_ctx;        /* ctx for an injected resolver (borrowed) */
    struct wf_oauth_trusted_keys *trusted_keys; /* optional; OAuth access-token
                                         verification keys (borrowed, may be NULL) */
    struct wf_oauth_dpop_replay_cache *replay_cache; /* optional, borrowed */
} wf_xrpc_server_auth_config;

/** Create an empty config (all fields zeroed, require_aud/require_lxm on). */
wf_status wf_xrpc_server_auth_config_new(wf_xrpc_server_auth_config **out);

/** Free a config produced by wf_xrpc_server_auth_config_new. NULL-safe.
 *  Borrowed pointers (resolver_client, trusted_keys, replay_cache) are not
 *  freed. */
void wf_xrpc_server_auth_config_free(wf_xrpc_server_auth_config *cfg);

/** Set the server's own DID (copied). Used to validate service-token `aud`. */
wf_status wf_xrpc_server_auth_config_set_server_did(wf_xrpc_server_auth_config *cfg,
                                                    const char *server_did);

/** Set the external HTTPS origin used to reconstruct DPoP `htu` values
 *  (for example `https://api.example.com`). The value is copied. */
wf_status wf_xrpc_server_auth_config_set_server_origin(
    wf_xrpc_server_auth_config *cfg, const char *server_origin);

/** Mark an NSID prefix as protected (requires a valid token). Copied.
 *  A request NSID matches when it equals the prefix or begins with
 *  `prefix + "."`. May be called repeatedly to protect several namespaces. */
wf_status wf_xrpc_server_auth_config_protect(wf_xrpc_server_auth_config *cfg,
                                             const char *nsid_prefix);

/** Set whether a token's `aud` claim must equal the configured server_did.
 *  Default: on. */
wf_status wf_xrpc_server_auth_config_set_require_aud(wf_xrpc_server_auth_config *cfg,
                                                     int require);

/** Set whether a token must carry an `lxm` claim equal to the request NSID
 *  (lexicon binding). Default: on. */
wf_status wf_xrpc_server_auth_config_set_require_lxm(wf_xrpc_server_auth_config *cfg,
                                                     int require);

/** Inject a custom DID→did:key resolver (borrowed). Passing NULL restores the
 *  built-in default resolver. */
wf_status wf_xrpc_server_auth_config_set_resolver(wf_xrpc_server_auth_config *cfg,
                                                  wf_xrpc_server_auth_resolve_fn fn,
                                                  void *ctx);

/** Set an optional wf_xrpc_client used by the default resolver to resolve
 *  non-did:key DIDs (borrowed). */
wf_status wf_xrpc_server_auth_config_set_resolver_client(
    wf_xrpc_server_auth_config *cfg, wf_xrpc_client *client);

/** Set optional trusted keys for OAuth access-token (user) verification
 *  (borrowed, may be NULL). */
wf_status wf_xrpc_server_auth_config_set_trusted_keys(
    wf_xrpc_server_auth_config *cfg, struct wf_oauth_trusted_keys *keys);

/** Set an optional shared DPoP replay cache (borrowed). */
wf_status wf_xrpc_server_auth_config_set_replay_cache(
    wf_xrpc_server_auth_config *cfg,
    struct wf_oauth_dpop_replay_cache *replay_cache);

/* ------------------------------------------------------------------ */
/* Middleware attachment                                               */
/* ------------------------------------------------------------------ */

/**
 * Attach the auth middleware to `server`.
 *
 * Installs a wf_xrpc_auth_cb that, for each request:
 *   1. allows the request when its NSID is not in the protected set;
 *   2. otherwise requires an `Authorization: Bearer <token>` header and:
 *      a. treats the token as an app.bsky service JWT when the issuer's
 *         verification key (resolved from the token's `iss` via the resolver)
 *         verifies the signature — checks `aud` (== server_did) and `lxm`
 *         (== request NSID) bindings; or
 *      b. when trusted_keys are configured, verifies it as an OAuth access
 *         token via wf_oauth_verify_bearer (DPoP-capable via
 *         wf_oauth_verify_request when a DPoP header is present).
 *   On success it sets req->authed_subject (the resolved DID) and
 *   req->authed_principal_kind; the handler reads them. On any failure
 *   (missing token, bad signature, wrong/expired aud/lxm) the request is
 *   denied with 401.
 *
 * The server takes ownership of the middleware's internal copy of `cfg` and
 * frees it in wf_xrpc_server_free. Passing NULL `cfg` is an error.
 */
wf_status wf_xrpc_server_set_auth_middleware(wf_xrpc_server *server,
                                             const wf_xrpc_server_auth_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_XRPC_SERVER_AUTH_H */
