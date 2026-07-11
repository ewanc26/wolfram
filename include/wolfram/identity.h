/**
 * identity.h — DID and handle resolution.
 *
 * Scaffolding for did:plc and did:web resolution, plus handle → DID
 * resolution via DNS TXT (_atproto.*) and the well-known HTTP fallback.
 * Bodies are stubbed pending implementation — see src/identity.c.
 */

#ifndef WOLFRAM_IDENTITY_H
#define WOLFRAM_IDENTITY_H

#include "wolfram/xrpc.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_did_method {
    WF_DID_METHOD_UNKNOWN = 0,
    WF_DID_METHOD_PLC,
    WF_DID_METHOD_WEB,
} wf_did_method;

/** A resolved DID document, trimmed to the fields wolfram cares about. */
typedef struct wf_did_document {
    char *did;              /* e.g. "did:plc:ofrbh253gwicbkc5nktqepol" */
    char *pds_endpoint;     /* service endpoint for AtprotoPersonalDataServer */
    char *feedgen_endpoint; /* service endpoint for BskyFeedGenerator */
    char *signing_key;      /* normalized did:key signing key, if present */
    char *notif_endpoint;   /* BskyNotificationService endpoint, if present */
    wf_did_method method;
} wf_did_document;

/** One character-string from a DNS TXT response. */
typedef struct wf_dns_txt_chunk {
    const unsigned char *data;
    size_t length;
    /** Non-zero when this chunk begins a new TXT resource record. */
    int record_start;
} wf_dns_txt_chunk;

/** Identify which DID method a DID string uses, without resolving it. */
wf_did_method wf_did_method_of(const char *did);

/**
 * Resolve a DID (did:plc or did:web) to its document.
 *
 * `client` is used purely as an HTTP transport (its base URL is
 * ignored; PLC directory / did:web host are derived from `did`).
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_did_document_free.
 */
wf_status wf_did_resolve(wf_xrpc_client *client,
                          const char *did,
                          wf_did_document *out);

/**
 * Resolve a known atproto service by its canonical ID and expected type.
 * Unknown service types return WF_ERR_NOT_FOUND; use the by-ID API below for
 * extensions.
 */
wf_status wf_did_resolve_service(wf_xrpc_client *client, const char *did,
                                 const char *service_type, char **out_endpoint);

/* Resolve a service by canonical fragment (for example "#atproto_pds").
 * `service_type` optionally enforces an exact type. Relative and absolute IDs
 * in the DID document are accepted. Only valid HTTP(S) endpoints are returned. */
wf_status wf_did_resolve_service_by_id(wf_xrpc_client *client,
                                        const char *did,
                                        const char *service_id,
                                        const char *service_type,
                                        char **out_endpoint);

/**
 * Parse a DID document from its JSON representation.
 *
 * Useful for a DID document embedded in another response (for example the
 * `didDoc` field returned by com.atproto.server.createSession /
 * refreshSession), where no network fetch is needed. The same field subset as
 * wf_did_resolve is extracted (pds_endpoint, feedgen_endpoint, signing_key,
 * notif_endpoint) and `method` is inferred from the document's `id`.
 *
 * On WF_OK, `out` is populated and must be released with wf_did_document_free.
 */
wf_status wf_did_document_parse(const char *json, size_t json_len,
                                wf_did_document *out);

/** Release the owned strings inside a wf_did_document. */
void wf_did_document_free(wf_did_document *doc);

/**
 * Parse DNS TXT chunks according to the AT Protocol handle format.
 *
 * Chunks belonging to one resource record are concatenated. Exactly one
 * complete record must begin with `did=` and its value must begin with `did:`;
 * unrelated TXT records are ignored. On WF_OK, `*out_did` is heap allocated
 * and must be released with free(). This entry point is also useful to DNS
 * adapters that do not use wolfram's built-in resolver.
 */
wf_status wf_handle_parse_dns_txt(const wf_dns_txt_chunk *chunks,
                                  size_t chunk_count,
                                  char **out_did);

/**
 * Resolve a handle (e.g. "ewancroft.uk") to a DID string.
 *
 * Tries DNS TXT `_atproto.<handle>` first, then falls back to
 * `https://<handle>/.well-known/atproto-did`.
 *
 * `*out_did` is heap-allocated on success; caller must free() it.
 */
wf_status wf_handle_resolve(wf_xrpc_client *client,
                              const char *handle,
                              char **out_did);

/**
 * Account / identity-management operations for the com.atproto.identity
 * lexicon. These are thin XRPC wrappers over the directory/identity
 * endpoints that complement the resolution helpers above. The PLC write
 * helpers (build/sign/submit of a `plc_operation`) live in plc.h; the
 * request/submit helpers here delegate to those where appropriate.
 *
 * Every heap-allocated output has a matching `_free` function documented
 * next to it. The free functions are safe to call with NULL.
 */

/** Output of com.atproto.identity.getRecommendedDidCredentials. */
typedef struct wf_identity_recommended_did_credentials {
    char **rotation_keys;       /* NULL if none; array of did:key strings */
    size_t rotation_keys_count;
    char **also_known_as;       /* NULL if none; array of "at://..." strings */
    size_t also_known_as_count;
    char  *verification_methods; /* raw JSON object string, or NULL */
    char  *services;             /* raw JSON object string, or NULL */
} wf_identity_recommended_did_credentials;

/**
 * Fetch recommended DID credentials for the authenticated account.
 * Calls GET com.atproto.identity.getRecommendedDidCredentials.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_identity_recommended_did_credentials_free.
 */
wf_status wf_identity_get_recommended_did_credentials(
    wf_xrpc_client *client, wf_identity_recommended_did_credentials *out);

/** Free a wf_identity_recommended_did_credentials. Safe to call with NULL. */
void wf_identity_recommended_did_credentials_free(
    wf_identity_recommended_did_credentials *creds);

/**
 * Request a PLC operation signature token for `did`.
 * Delegates to wf_plc_request_signature (com.atproto.identity.
 * requestPlcOperationSignature). The `did` receives an email with a code
 * used as the `token` field of wf_identity_sign_plc_operation.
 */
wf_status wf_identity_request_plc_operation_signature(wf_xrpc_client *client,
                                                      const char *did);

/** Input for com.atproto.identity.signPlcOperation (server-side token signing). */
typedef struct wf_identity_sign_plc_operation_input {
    /**
     * Token received via wf_identity_request_plc_operation_signature. NULL or
     * empty omits the field.
     */
    const char *token;
    /** Rotation keys (array of did:key strings). NULL/0 -> omitted. */
    const char *const *rotation_keys;
    size_t             rotation_keys_count;
    /** alsoKnownAs entries (array of "at://..." strings). NULL/0 -> omitted. */
    const char *const *also_known_as;
    size_t             also_known_as_count;
    /**
     * verificationMethods map as a JSON object string, e.g.
     * "{\"atproto\":\"did:key:...\"}". NULL -> omitted.
     */
    const char *verification_methods_json;
    /**
     * services map as a JSON object string, e.g.
     * "{\"atproto_pds\":\"https://...\"}". NULL -> omitted.
     */
    const char *services_json;
} wf_identity_sign_plc_operation_input;

/** Output of com.atproto.identity.signPlcOperation. */
typedef struct wf_identity_sign_plc_operation_result {
    char *operation; /* the signed plc_operation JSON string */
} wf_identity_sign_plc_operation_result;

/**
 * Sign a PLC operation server-side using a token from
 * wf_identity_request_plc_operation_signature.
 * Calls POST com.atproto.identity.signPlcOperation.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_identity_sign_plc_operation_result_free.
 */
wf_status wf_identity_sign_plc_operation(
    wf_xrpc_client *client, const wf_identity_sign_plc_operation_input *input,
    wf_identity_sign_plc_operation_result *out);

/** Free a wf_identity_sign_plc_operation_result. Safe to call with NULL. */
void wf_identity_sign_plc_operation_result_free(
    wf_identity_sign_plc_operation_result *result);

/**
 * Submit a signed PLC operation to com.atproto.identity.submitPlcOperation.
 * `signed_op_json` must be the full signed operation JSON (e.g. from
 * wf_identity_sign_plc_operation or the plc module). Delegates to
 * wf_plc_submit_operation.
 */
wf_status wf_identity_submit_plc_operation(wf_xrpc_client *client,
                                           const char *signed_op_json);

/**
 * Update the authenticated account's handle.
 * Delegates to wf_plc_update_handle (com.atproto.identity.updateHandle).
 */
wf_status wf_identity_update_handle(wf_xrpc_client *client, const char *handle);

/** Input for com.atproto.identity.checkHandle. */
typedef struct wf_identity_check_handle_input {
    const char *handle;  /* required */
    const char *did;     /* optional; the DID the handle is expected to map to */
} wf_identity_check_handle_input;

/** Output of com.atproto.identity.checkHandle. */
typedef struct wf_identity_check_handle_result {
    int valid; /* 1 if the handle is available/valid, 0 otherwise; -1 if absent */
} wf_identity_check_handle_result;

/**
 * Check whether a handle is available / correctly configured.
 * Calls GET com.atproto.identity.checkHandle?handle=...[&did=...].
 *
 * On WF_OK, `out` is populated (out->valid reflects the `valid` response).
 */
wf_status wf_identity_check_handle(wf_xrpc_client *client,
                                   const wf_identity_check_handle_input *input,
                                   wf_identity_check_handle_result *out);

/**
 * Verify that a handle bi-directionally matches a DID. Resolves the handle to
 * a DID (wf_handle_resolve) and the DID to its document handle
 * (wf_did_resolve), returning WF_OK with *out_valid set to 1 only when both
 * directions agree. There is no com.atproto.identity.verifyHandle XRPC; this is
 * a local convenience built on the existing resolvers.
 *
 * On WF_OK, *out_valid is set to 1 (match) or 0 (mismatch).
 */
wf_status wf_identity_verify_handle(wf_xrpc_client *client, const char *handle,
                                    int *out_valid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_IDENTITY_H */
