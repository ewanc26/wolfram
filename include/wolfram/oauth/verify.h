#ifndef WOLFRAM_OAUTH_VERIFY_H
#define WOLFRAM_OAUTH_VERIFY_H

#include <stdint.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* OAuth resource-server token verification                            */
/*                                                                     */
/* These helpers let an XRPC server (or any atproto resource server)   */
/* validate an incoming `Authorization: Bearer <access-token>` and     */
/* `DPoP: <proof-jwt>` pair, per RFC 9449 (OAuth 2.0 Demonstrating     */
/* Proof-of-Possession at the Application Layer) and the atproto       */
/* OAuth profile. They reuse the P-256 / ES256, SHA-256, base64url and */
/* JWK primitives in wolfram/crypto.h and never hand-roll crypto.      */
/*                                                                     */
/* Integration story: a `wf_xrpc_auth_cb` can parse the request's      */
/* `Authorization` and `DPoP` headers, reconstruct the request URI     */
/* (the server exposes `req->method` and `req->nsid`; the htu is the   */
/* server's external origin + "/xrpc/" + nsid), then call             */
/* `wf_oauth_verify_request` with a `wf_oauth_trusted_keys` populated  */
/* from the authorization server's JWKS and a shared                 */
/* `wf_oauth_dpop_replay_cache`. Return WF_OK to proceed; any other    */
/* status yields a 401.                                                */
/* ------------------------------------------------------------------ */

/** Maximum acceptable age of a DPoP proof `iat` (seconds). */
#define WF_OAUTH_DPOP_MAX_AGE 300

/** Clock-skew tolerance applied to `iat`/`exp` checks (seconds). */
#define WF_OAUTH_CLOCK_SKEW 30

/**
 * A set of trusted public JWKs (P-256) used to verify access-token
 * signatures. The caller populates it (typically from the authorization
 * server's published JWKS) before verifying; ownership of the container
 * (and any heap strings it holds) is released with wf_oauth_trusted_keys_free.
 */
typedef struct wf_oauth_trusted_keys wf_oauth_trusted_keys;

/** Create an empty trusted-keys container. Free with wf_oauth_trusted_keys_free. */
wf_status wf_oauth_trusted_keys_new(wf_oauth_trusted_keys **out);

/**
 * Add a public JWK (a NUL-terminated JSON object, e.g.
 * `{"kty":"EC","crv":"P-256","x":"...","y":"..."}`) to the container.
 * The JSON string is copied; the caller retains ownership of `jwk_json`.
 */
wf_status wf_oauth_trusted_keys_add_jwk(wf_oauth_trusted_keys *keys,
                                        const char *jwk_json);

/** Free a trusted-keys container and all JWK copies it holds. Safe with NULL. */
void wf_oauth_trusted_keys_free(wf_oauth_trusted_keys *keys);

/**
 * A verified principal derived from a Bearer access token and (optionally) a
 * DPoP proof. All string fields are heap-owned and released by
 * wf_oauth_verified_token_free. `sub` is the token subject (typically a DID);
 * `dpop_jkt` is the RFC 7638 JWK thumbprint of the DPoP proof's public key when
 * DPoP binding was satisfied. `dpop_bound` is non-zero when a valid DPoP proof
 * was presented and bound to the access token.
 */
typedef struct wf_oauth_verified_token {
    char *sub;        /* subject / DID (may be NULL for a DPoP-only proof) */
    char *iss;        /* token issuer (may be NULL) */
    char *aud;        /* audience (first entry if an array; may be NULL) */
    char *scope;      /* space-separated scopes (may be NULL) */
    char *dpop_jkt;   /* DPoP proof JWK thumbprint (NULL unless DPoP-bound) */
    int64_t exp;      /* expiry (unix seconds); 0 if absent */
    int dpop_bound;   /* 1 when a valid DPoP proof was bound to this token */
} wf_oauth_verified_token;

/** Free a verified token and all strings it owns. Safe with NULL. */
void wf_oauth_verified_token_free(wf_oauth_verified_token *token);

/**
 * In-memory replay cache for DPoP `jti` values. Entries auto-expire after an
 * optional TTL (see wf_oauth_dpop_replay_cache_mark_seen). There is no global
 * capacity eviction beyond per-entry TTL expiry; servers with very high
 * request rates should wrap this or back it with an external store.
 */
typedef struct wf_oauth_dpop_replay_cache wf_oauth_dpop_replay_cache;

/** Create an empty replay cache. Free with wf_oauth_dpop_replay_cache_free. */
wf_status wf_oauth_dpop_replay_cache_new(wf_oauth_dpop_replay_cache **out);

/**
 * Return non-zero if `jti` is already recorded (and not expired). Pure read;
 * does not mutate the cache.
 */
int wf_oauth_dpop_replay_cache_is_seen(const wf_oauth_dpop_replay_cache *cache,
                                       const char *jti);

/**
 * Record `jti` as seen. `ttl` is the lifetime in seconds; a value <= 0 means
 * the entry never expires. Returns WF_OK on success, WF_ERR_ALLOC on OOM.
 * Re-recording an existing (unexpired) jti refreshes its expiry.
 */
wf_status wf_oauth_dpop_replay_cache_mark_seen(wf_oauth_dpop_replay_cache *cache,
                                               const char *jti, int64_t ttl);

/** Free a replay cache and all recorded `jti` strings. Safe with NULL. */
void wf_oauth_dpop_replay_cache_free(wf_oauth_dpop_replay_cache *cache);

/**
 * Verify a JWT access token against the supplied trusted JWK(s).
 *
 * Checks: ES256 signature (against one of `keys`, matched by `kid` when the
 * JWT header carries one, otherwise by trying each key), `alg` must be ES256
 * (rejects `none` and other algorithms), and `exp` must be in the future.
 * On WF_OK, *out is heap-allocated and must be freed with
 * wf_oauth_verified_token_free; `dpop_bound` is set to 0 and `dpop_jkt` is
 * taken from the token's `cnf.jkt` claim when present.
 */
wf_status wf_oauth_verify_bearer(const char *access_token,
                                 const wf_oauth_trusted_keys *keys,
                                 wf_oauth_verified_token **out);

/**
 * Verify a DPoP proof JWT.
 *
 * Checks: ES256 signature over the proof using the proof's own JWK (from the
 * header `jwk`), `typ` == "dpop+jwt", `alg` == ES256, and (when `access_token`
 * is non-NULL) `ath` == base64url(SHA-256(access_token)). Also validates
 * `htm` against `http_method`, `htu` against `http_uri`, `jti` presence, and
 * the `iat`/`exp` freshness window. Replay is enforced via `replay` (may be
 * NULL to disable): a `jti` already seen fails with WF_ERR_DUPLICATE.
 *
 * On WF_OK, *out is heap-allocated and freed with wf_oauth_verified_token_free;
 * `dpop_jkt` carries the proof key's RFC 7638 thumbprint and `dpop_bound` is 1.
 * `sub`/`iss`/`aud`/`scope` are left NULL (a DPoP proof is not an identity token).
 */
wf_status wf_oauth_verify_dpop(const char *dpop_proof,
                               const char *access_token,
                               const char *http_method,
                               const char *http_uri,
                               wf_oauth_dpop_replay_cache *replay,
                               wf_oauth_verified_token **out);

/**
 * Verify an incoming request's authorization. `authorization` is the raw
 * `Authorization` header value (must be a `Bearer` scheme); `dpop_proof` is the
 * raw `DPoP` header value (may be NULL for Bearer-only requests). Both tokens
 * are verified, the DPoP `ath` is checked against the access token, and — when
 * the access token carries a `cnf.jkt` confirmation claim — it must match the
 * DPoP proof's key thumbprint.
 *
 * On WF_OK, *out is heap-allocated and freed with wf_oauth_verified_token_free.
 * The convenience accepts both:
 *   - Bearer-only (dpop_proof == NULL): token verified, dpop_bound = 0.
 *   - DPoP-bound (full pair): token + proof verified and bound.
 *   - DPoP-only (authorization == NULL but dpop_proof != NULL): only the proof
 *     is verified; `sub` is NULL (no identity), dpop_bound = 1.
 */
wf_status wf_oauth_verify_request(const char *authorization,
                                  const char *dpop_proof,
                                  const char *http_method,
                                  const char *http_uri,
                                  const wf_oauth_trusted_keys *keys,
                                  wf_oauth_dpop_replay_cache *replay,
                                  wf_oauth_verified_token **out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_VERIFY_H */
