#ifndef WOLFRAM_OAUTH_DPOP_H
#define WOLFRAM_OAUTH_DPOP_H

#include <stdint.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque, heap-owned ES256 DPoP key. */
typedef struct wf_oauth_dpop_key wf_oauth_dpop_key;

wf_status wf_oauth_dpop_key_generate(wf_oauth_dpop_key **out);
wf_status wf_oauth_dpop_key_import(const unsigned char private_key[32],
                                   wf_oauth_dpop_key **out);
wf_status wf_oauth_dpop_key_export(const wf_oauth_dpop_key *key,
                                   unsigned char private_key_out[32]);
void wf_oauth_dpop_key_free(wf_oauth_dpop_key *key);

/** Calculate the RFC 7638 base64url JWK thumbprint (jkt). */
wf_status wf_oauth_dpop_key_thumbprint(const wf_oauth_dpop_key *key,
                                       char thumbprint_out[44]);

typedef struct wf_oauth_dpop_proof_options {
    const char *http_method;
    const char *http_uri;
    const char *nonce;        /* optional server-provided DPoP nonce */
    const char *access_token; /* optional; produces the `ath` claim */
    const char *jti;          /* optional test/persistence hook; random if NULL */
    int64_t issued_at;        /* seconds since epoch; current time if <= 0 */
} wf_oauth_dpop_proof_options;

/**
 * Create an ES256 DPoP proof JWT. `*jwt_out` is heap-owned and must be freed
 * with wf_oauth_string_free.
 */
wf_status wf_oauth_dpop_proof_create(const wf_oauth_dpop_key *key,
                                     const wf_oauth_dpop_proof_options *options,
                                     char **jwt_out);

#define WF_OAUTH_CLIENT_ASSERTION_TYPE \
    "urn:ietf:params:oauth:client-assertion-type:jwt-bearer"

typedef struct wf_oauth_client_assertion_options {
    const char *client_id;
    const char *authorization_server_issuer;
    const char *key_id;
    const char *jti;   /* optional deterministic test hook */
    int64_t issued_at; /* current time when <= 0; assertion expires in 60s */
} wf_oauth_client_assertion_options;

/** Create an RFC 7523 ES256 private_key_jwt client assertion. */
wf_status wf_oauth_client_assertion_create(
    const wf_oauth_dpop_key *signing_key,
    const wf_oauth_client_assertion_options *options,
    char **jwt_out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_DPOP_H */
