#ifndef WOLFRAM_OAUTH_METADATA_H
#define WOLFRAM_OAUTH_METADATA_H

#include <stddef.h>

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
    char *revocation_endpoint;
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

/** Free a string returned by an OAuth API. Safe to call with NULL. */
void wf_oauth_string_free(char *value);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_METADATA_H */
