/**
 * oauth.h — AT Protocol OAuth discovery and proof foundations.
 *
 * This module implements metadata validation, PKCE S256, and ES256 DPoP
 * proofs, PAR, authorization callback validation, token exchange, authenticated
 * clients, and serializable authorization/session state. Store coordination
 * and higher-level flow orchestration remain application concerns.
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

/** Borrowed client authentication configuration for OAuth endpoint calls. */
typedef struct wf_oauth_client_auth {
    const char *client_id;
    const char *authorization_server_issuer; /* required for private_key_jwt */
    const wf_oauth_dpop_key *signing_key;     /* NULL selects `none` */
    const char *key_id;                       /* required with signing_key */
} wf_oauth_client_auth;

/** Cross-check client authentication against discovered metadata. */
wf_status wf_oauth_client_auth_validate(
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *auth);

wf_status wf_oauth_par_response_parse(const char *json, size_t json_len,
                                      wf_oauth_par_response *out);
wf_status wf_oauth_token_response_parse(const char *json, size_t json_len,
                                        wf_oauth_token_response *out);
/** Validate that a parsed token response remains bound to an expected DID. */
wf_status wf_oauth_token_response_validate_subject(
    const wf_oauth_token_response *response, const char *expected_sub);
void wf_oauth_par_response_free(wf_oauth_par_response *response);
void wf_oauth_token_response_free(wf_oauth_token_response *response);

/** Public-client PAR with one mandatory use_dpop_nonce retry. */
wf_status wf_oauth_par(wf_xrpc_client *transport, const char *endpoint,
                       const wf_oauth_dpop_key *key,
                       const wf_oauth_par_request *request,
                       wf_oauth_par_response *out);
wf_status wf_oauth_par_with_auth(
    wf_xrpc_client *transport, const char *endpoint,
    const wf_oauth_dpop_key *dpop_key, const wf_oauth_client_auth *auth,
    const wf_oauth_par_request *request, wf_oauth_par_response *out);

/** Public-client authorization-code exchange with DPoP nonce retry. */
wf_status wf_oauth_exchange_code(wf_xrpc_client *transport,
                                 const char *token_endpoint,
                                 const wf_oauth_dpop_key *key,
                                 const char *client_id, const char *code,
                                 const char *redirect_uri,
                                 const char *code_verifier,
                                 wf_oauth_token_response *out);
wf_status wf_oauth_exchange_code_with_auth(
    wf_xrpc_client *transport, const char *token_endpoint,
    const wf_oauth_dpop_key *dpop_key, const wf_oauth_client_auth *auth,
    const char *code, const char *redirect_uri, const char *code_verifier,
    wf_oauth_token_response *out);

/**
 * Exchange a public client's refresh token. The returned AT Protocol subject
 * must match expected_sub; callers should verify that DID's issuer before use.
 */
wf_status wf_oauth_refresh(wf_xrpc_client *transport,
                           const char *token_endpoint,
                           const wf_oauth_dpop_key *key,
                           const char *client_id,
                           const char *refresh_token,
                           const char *expected_sub,
                           wf_oauth_token_response *out);
wf_status wf_oauth_refresh_with_auth(
    wf_xrpc_client *transport, const char *token_endpoint,
    const wf_oauth_dpop_key *dpop_key, const wf_oauth_client_auth *auth,
    const char *refresh_token, const char *expected_sub,
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

/** Owned state required to finish an authorization redirect flow. */
typedef struct wf_oauth_authorization_state {
    char *issuer;
    char *code_verifier;
    char *app_state; /* optional */
    char *client_auth_key_id; /* optional; private_key_jwt when populated */
    int64_t expires_at; /* Unix seconds */
    wf_oauth_dpop_key *dpop_key;
} wf_oauth_authorization_state;

/**
 * Create a deep-owned pending state. `ttl_seconds` must be positive; callers
 * normally use 600 seconds, matching the maintained atproto browser client.
 */
wf_status wf_oauth_authorization_state_create(
    const char *issuer, const wf_oauth_pkce *pkce,
    const wf_oauth_dpop_key *dpop_key, const char *app_state,
    const char *client_auth_key_id, int64_t now, int64_t ttl_seconds,
    wf_oauth_authorization_state *out);

/** Serialize state, including its private DPoP JWK, to owned JSON. */
wf_status wf_oauth_authorization_state_serialize(
    const wf_oauth_authorization_state *state, char **json_out);

/** Restore and validate unexpired state. `now` is Unix seconds. */
wf_status wf_oauth_authorization_state_parse(
    const char *json, size_t json_len, int64_t now,
    wf_oauth_authorization_state *out);
void wf_oauth_authorization_state_free(wf_oauth_authorization_state *state);

/** Owned durable OAuth token session, corresponding to atproto TokenSet. */
typedef struct wf_oauth_session_state {
    char *issuer;
    char *subject;
    char *audience;
    char *scope;
    char *access_token;
    char *refresh_token; /* optional */
    char *client_auth_key_id; /* optional; private_key_jwt when populated */
    int64_t expires_at;  /* Unix seconds; zero when unspecified */
    wf_oauth_dpop_key *dpop_key;
} wf_oauth_session_state;

wf_status wf_oauth_session_state_create(
    const char *issuer, const char *audience,
    const wf_oauth_dpop_key *dpop_key,
    const wf_oauth_token_response *token_response,
    const char *client_auth_key_id, int64_t now,
    wf_oauth_session_state *out);
wf_status wf_oauth_session_state_serialize(
    const wf_oauth_session_state *session, char **json_out);
wf_status wf_oauth_session_state_parse(
    const char *json, size_t json_len, const char *expected_subject,
    wf_oauth_session_state *out);
void wf_oauth_session_state_free(wf_oauth_session_state *session);

typedef struct wf_oauth_authorization_begin_options {
    const char *redirect_uri;
    const char *scope;      /* optional; defaults to client metadata scope */
    const char *login_hint; /* optional handle or DID */
    const char *app_state;  /* optional application-owned state */
    int64_t now;            /* current Unix seconds */
    int64_t state_ttl;      /* defaults to 600 when <= 0 */
} wf_oauth_authorization_begin_options;

/** Owned output of the authorization flow's initial PAR transition. */
typedef struct wf_oauth_authorization_begin_result {
    char *authorization_url;
    char *state;
    char *state_json; /* persist atomically under `state` before redirecting */
} wf_oauth_authorization_begin_result;

/**
 * Generate PKCE/state/DPoP material, submit PAR, and construct the browser URL.
 * The caller owns `state_json` persistence and must consume it on callback.
 */
wf_status wf_oauth_authorization_begin(
    wf_xrpc_client *transport,
    const wf_oauth_server_metadata *server,
    const wf_oauth_client_metadata *client,
    const wf_oauth_client_auth *client_auth,
    const wf_oauth_authorization_begin_options *options,
    wf_oauth_authorization_begin_result *out);
void wf_oauth_authorization_begin_result_free(
    wf_oauth_authorization_begin_result *result);

/** Construct the browser URL for an already-created PAR request URI. */
wf_status wf_oauth_authorization_url_create(
    const char *authorization_endpoint, const char *client_id,
    const char *request_uri, char **url_out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_H */
