/**
 * oauth.h — AT Protocol OAuth discovery and proof foundations.
 *
 * This module implements metadata validation, PKCE S256, and ES256 DPoP
 * proofs, PAR, authorization callback validation, and authorization-code
 * token exchange. Persistent session state remains a higher-level concern.
 * Network requests made by the discovery helpers are delegated to xrpc.c.
 */

#ifndef WOLFRAM_OAUTH_H
#define WOLFRAM_OAUTH_H

#include <stddef.h>
#include <stdint.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_oauth_string_list {
    char **items;
    size_t count;
} wf_oauth_string_list;

/** Owned RFC 9728 protected-resource metadata. */
typedef struct wf_oauth_resource_metadata {
    char *resource;
    wf_oauth_string_list authorization_servers;
    wf_oauth_string_list scopes_supported;
} wf_oauth_resource_metadata;

/** Owned RFC 8414 authorization-server metadata used by the atproto profile. */
typedef struct wf_oauth_server_metadata {
    char *issuer;
    char *authorization_endpoint;
    char *token_endpoint;
    char *pushed_authorization_request_endpoint;
    wf_oauth_string_list response_types_supported;
    wf_oauth_string_list grant_types_supported;
    wf_oauth_string_list code_challenge_methods_supported;
    wf_oauth_string_list token_endpoint_auth_methods_supported;
    wf_oauth_string_list token_endpoint_auth_signing_alg_values_supported;
    wf_oauth_string_list scopes_supported;
    wf_oauth_string_list dpop_signing_alg_values_supported;
    wf_oauth_string_list protected_resources;
    int authorization_response_iss_parameter_supported;
    int require_pushed_authorization_requests;
    int require_request_uri_registration;
    int require_request_uri_registration_present;
    int client_id_metadata_document_supported;
} wf_oauth_server_metadata;

/** Owned public-client metadata. JWKS contents remain encoded JSON. */
typedef struct wf_oauth_client_metadata {
    char *client_id;
    char *client_name;
    char *client_uri;
    char *scope;
    char *token_endpoint_auth_method;
    char *token_endpoint_auth_signing_alg;
    char *jwks_uri;
    char *jwks_json;
    char *application_type;
    wf_oauth_string_list redirect_uris;
    wf_oauth_string_list response_types;
    wf_oauth_string_list grant_types;
    int dpop_bound_access_tokens;
    int dpop_bound_access_tokens_present;
} wf_oauth_client_metadata;

/** Parse and validate an atproto protected-resource metadata document. */
wf_status wf_oauth_resource_metadata_parse(const char *json, size_t json_len,
                                            const char *expected_resource,
                                            wf_oauth_resource_metadata *out);

/** Parse and validate an atproto authorization-server metadata document. */
wf_status wf_oauth_server_metadata_parse(const char *json, size_t json_len,
                                          const char *expected_issuer,
                                          wf_oauth_server_metadata *out);

/**
 * Parse and validate a discoverable atproto client metadata document.
 * If expected_client_id is non-NULL, the document client_id must match it.
 */
wf_status wf_oauth_client_metadata_parse(const char *json, size_t json_len,
                                          const char *expected_client_id,
                                          wf_oauth_client_metadata *out);

void wf_oauth_resource_metadata_free(wf_oauth_resource_metadata *metadata);
void wf_oauth_server_metadata_free(wf_oauth_server_metadata *metadata);
void wf_oauth_client_metadata_free(wf_oauth_client_metadata *metadata);

/** Fetch and validate /.well-known/oauth-protected-resource via xrpc.c. */
wf_status wf_oauth_resource_metadata_get(wf_xrpc_client *transport,
                                          const char *resource,
                                          wf_oauth_resource_metadata *out);

/** Fetch and validate /.well-known/oauth-authorization-server via xrpc.c. */
wf_status wf_oauth_server_metadata_get(wf_xrpc_client *transport,
                                        const char *issuer,
                                        wf_oauth_server_metadata *out);

/** Fetch a client_id metadata document via xrpc.c and validate it. */
wf_status wf_oauth_client_metadata_get(wf_xrpc_client *transport,
                                        const char *client_id,
                                        wf_oauth_client_metadata *out);

/**
 * Discover and cross-check the authorization server for a protected resource.
 * Both outputs are owned and must be freed with their matching free functions.
 */
wf_status wf_oauth_discover(wf_xrpc_client *transport,
                            const char *resource,
                            wf_oauth_resource_metadata *resource_out,
                            wf_oauth_server_metadata *server_out);

#define WF_OAUTH_PKCE_VERIFIER_MAX 128
#define WF_OAUTH_PKCE_CHALLENGE_LEN 43

typedef struct wf_oauth_pkce {
    char verifier[WF_OAUTH_PKCE_VERIFIER_MAX + 1];
    char challenge[WF_OAUTH_PKCE_CHALLENGE_LEN + 1];
} wf_oauth_pkce;

/** Generate a fresh 32-octet PKCE verifier and its S256 challenge. */
wf_status wf_oauth_pkce_generate(wf_oauth_pkce *out);

/** Validate a caller-provided RFC 7636 verifier and derive its S256 challenge. */
wf_status wf_oauth_pkce_from_verifier(const char *verifier,
                                       wf_oauth_pkce *out);

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

/** Free a string returned by an OAuth API. Safe to call with NULL. */
void wf_oauth_string_free(char *value);

typedef struct wf_oauth_par_response {
    char *request_uri;
    int64_t expires_in;
} wf_oauth_par_response;

typedef struct wf_oauth_token_response {
    char *access_token;
    char *token_type;
    char *sub;
    char *scope;
    char *refresh_token;
    int64_t expires_in;
    int expires_in_present;
} wf_oauth_token_response;

typedef struct wf_oauth_par_request {
    const char *client_id;
    const char *redirect_uri;
    const char *scope;
    const char *state;
    const char *code_challenge;
    const char *login_hint; /* optional */
} wf_oauth_par_request;

wf_status wf_oauth_par_response_parse(const char *json, size_t json_len,
                                      wf_oauth_par_response *out);
wf_status wf_oauth_token_response_parse(const char *json, size_t json_len,
                                        wf_oauth_token_response *out);
void wf_oauth_par_response_free(wf_oauth_par_response *response);
void wf_oauth_token_response_free(wf_oauth_token_response *response);

/** Public-client PAR with one mandatory use_dpop_nonce retry. */
wf_status wf_oauth_par(wf_xrpc_client *transport, const char *endpoint,
                       const wf_oauth_dpop_key *key,
                       const wf_oauth_par_request *request,
                       wf_oauth_par_response *out);

/** Public-client authorization-code exchange with DPoP nonce retry. */
wf_status wf_oauth_exchange_code(wf_xrpc_client *transport,
                                 const char *token_endpoint,
                                 const wf_oauth_dpop_key *key,
                                 const char *client_id, const char *code,
                                 const char *redirect_uri,
                                 const char *code_verifier,
                                 wf_oauth_token_response *out);

/** Decoded parameters received at the client's redirect URI. */
typedef struct wf_oauth_callback_params {
    const char *response;          /* JARM response; unsupported when present */
    const char *state;
    const char *code;
    const char *issuer;            /* `iss` */
    const char *error;
    const char *error_description;
} wf_oauth_callback_params;

/** Owned validated callback outcome. Exactly one of code/error is populated. */
typedef struct wf_oauth_callback_result {
    char *state;
    char *code;
    char *issuer;
    char *error;
    char *error_description;
} wf_oauth_callback_result;

/**
 * Validate an authorization callback against its one-time state and issuer.
 * Callers must atomically consume expected_state from persistent storage before
 * using a successful result, preventing callback replay.
 */
wf_status wf_oauth_callback_validate(const wf_oauth_callback_params *params,
                                     const char *expected_state,
                                     const char *expected_issuer,
                                     int issuer_parameter_required,
                                     wf_oauth_callback_result *out);
void wf_oauth_callback_result_free(wf_oauth_callback_result *result);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_H */
