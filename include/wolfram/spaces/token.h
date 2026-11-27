/**
 * spaces/token.h — the three Spaces JWTs (experimental; build with
 * WOLFRAM_BUILD_SPACES). See docs/spaces.md.
 *
 * | typ                              | signed by           | audience      |
 * |----------------------------------|---------------------|---------------|
 * | atproto-space-delegation+jwt     | user `#atproto`     | space host    |
 * | atproto-client-attestation+jwt   | client JWKS key     | space host    |
 * | atproto-space-credential+jwt     | authority `#atproto`| none (cnf)    |
 *
 * Delegation tokens and attestations are single use: remembering `jti` is the
 * caller's job. Treat every one of these tokens as a secret and keep it out of
 * logs.
 */

#ifndef WOLFRAM_SPACES_TOKEN_H
#define WOLFRAM_SPACES_TOKEN_H

#include "wolfram/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WF_SPACE_DELEGATION_LIFETIME 60
#define WF_SPACE_ATTESTATION_LIFETIME 60
#define WF_SPACE_CREDENTIAL_LIFETIME 600
#define WF_SPACE_CREDENTIAL_MAX_LIFETIME 3600
#define WF_SPACE_CLOCK_SKEW 5

typedef enum wf_space_token_type {
    WF_SPACE_TOKEN_DELEGATION = 0,
    WF_SPACE_TOKEN_CREDENTIAL = 1,
    WF_SPACE_TOKEN_CLIENT_ATTESTATION = 2
} wf_space_token_type;

/** A parsed token. Nothing here is trusted until it has been verified. */
typedef struct wf_space_token {
    wf_space_token_type type;
    char *alg;     /* "ES256" or "ES256K" */
    char *kid;     /* "#atproto", or the client's JWKS key id (may be NULL) */
    char *iss;     /* issuer DID, or client_id for an attestation */
    char *sub;     /* space reference, or client_id for an attestation */
    char *aud;     /* NULL in a credential */
    char *cnf_kid; /* credential only: the P-256 credential key */
    char *jti;
    long long iat; /* 0 when absent (never in a credential) */
    long long exp;
    char *signing_input;
    unsigned char signature[64];
} wf_space_token;

/**
 * Parse `jwt` as a token of `type` and apply the structural rules: the `typ`
 * and `kid` header, required claims, a delegation or credential `sub` that is a
 * space reference, a credential issued by that space's authority with a P-256
 * `cnf.kid` and a lifetime of at most an hour, and an attestation whose `iss`
 * equals its `sub`. WF_ERR_VALIDATION when any of them fails. Verifies neither
 * the signature nor the time.
 */
wf_status wf_space_token_parse(wf_space_token_type type, const char *jwt,
                               wf_space_token *out);

void wf_space_token_free(wf_space_token *token);

/**
 * Verify the signature against `did_key`: the issuer's `#atproto` key, or for
 * an attestation the client's JWKS key named by `kid`, as a did:key. The key
 * type must match `alg`. Delegation tokens and credentials require low-S, as
 * the reference implementation does; attestations are ordinary JWTs and may be
 * either. ES256K needs a build with libsecp256k1. If this fails for a DID key,
 * refresh the DID document once and try again.
 */
wf_status wf_space_token_verify_signature(const wf_space_token *token,
                                          const char *did_key);

/**
 * Check time and binding at `now` (Unix seconds), allowing five seconds of
 * skew: not expired, a credential not issued in the future, and, for each that
 * is not NULL, `aud` and `sub` equal to those expected.
 */
wf_status wf_space_token_check_claims(const wf_space_token *token,
                                      long long now, const char *aud,
                                      const char *sub);

/** The space host's service id, "<authority>#atproto_pds". Caller frees. */
wf_status wf_space_host_audience(const char *space_ref, char **out);

/*
 * Minting. `now` is the issue time in Unix seconds; a `lifetime` of 0 means the
 * default above. Each call draws a fresh `jti`. Caller frees *out_jwt.
 */

/** A delegation token for `issuer`, addressed to the host of `space_ref`. For
 *  a PDS, or an identity holding its own `#atproto` key. */
wf_status wf_space_delegation_token_create(const wf_signing_key *key,
                                           const char *issuer,
                                           const char *space_ref, long long now,
                                           long long lifetime, char **out_jwt);

/** Space host side: a credential for `space_ref` bound to the P-256 did:key
 *  `cnf_kid`, signed with the authority's `#atproto` key. */
wf_status wf_space_credential_create(const wf_signing_key *key,
                                     const char *space_ref, const char *cnf_kid,
                                     long long now, long long lifetime,
                                     char **out_jwt);

/** A client attestation signed with a P-256 key from the client's JWKS.
 *  `audience` is wf_space_host_audience of the space. */
wf_status wf_space_client_attestation_create(
    const wf_signing_key *key, const char *key_id, const char *client_id,
    const char *audience, long long now, long long lifetime, char **out_jwt);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SPACES_TOKEN_H */
