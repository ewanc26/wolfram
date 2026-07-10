/*
 * identity_typed.h — owned typed parsers for com.atproto.identity responses,
 * plus convenience agent wrappers for DID/handle/PLC management.
 *
 * Parsers own their outputs: every string is heap-allocated and every owned
 * struct has a `_free` that is safe when the struct is zeroed. On the first
 * error a parser frees any partial allocations and leaves `out` fully reset.
 * The agent wrappers issue the corresponding generated lex call
 * (`wf_lex_com_atproto_identity_*_main_call`) after syncing auth onto the
 * agent's primary XRPC client, then parse the body into the owned struct.
 *
 * The high-level `wf_agent_identity_rotate_handle` orchestrates the PLC
 * handle-rotation flow using plc.h (`wf_plc_*`) and the server-side
 * com.atproto.identity sign/submit procedures. It builds the rotation
 * operation locally (validation gate), then signs it via signPlcOperation and
 * submits it via submitPlcOperation. Signing is delegated to the PDS because
 * the agent holds no private signing key; the one-time `token` (delivered
 * out-of-band by email after a requestPlcOperationSignature call) authorizes
 * that sign and must be supplied by the caller. With a NULL/empty `token` the
 * function triggers requestPlcOperationSignature (emailing the token) and
 * returns an honest WF_ERR_INVALID_ARG rather than fabricating success.
 */

#ifndef WOLFRAM_IDENTITY_TYPED_H
#define WOLFRAM_IDENTITY_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* resolveHandle output: the DID a handle resolves to. */
typedef struct wf_identity_resolve_handle {
    char *did;
} wf_identity_resolve_handle;

/* A single verificationMethod entry from a DID document. */
typedef struct wf_identity_verification_method {
    char *id;
    char *type;
    char *controller;
    char *public_key_multibase;
} wf_identity_verification_method;

/* A single service entry from a DID document. `service_endpoint_json` keeps the
 * raw `serviceEndpoint` value (string or array) as an owned JSON string. */
typedef struct wf_identity_service {
    char *id;
    char *type;
    char *service_endpoint_json;
} wf_identity_service;

/* resolveDid output, decoded from the returned DID document (didDoc): the
 * primary handle (from alsoKnownAs[0] "at://..."), the verification methods,
 * and the services. */
typedef struct wf_identity_resolve_did {
    char *handle;
    wf_identity_verification_method *verification_methods;
    size_t                          verification_method_count;
    wf_identity_service            *services;
    size_t                          service_count;
} wf_identity_resolve_did;

/* getRecommendedDidCredentials output: recommended rotation keys, alsoKnownAs,
 * and the verificationMethods / services JSON objects (owned JSON strings). */
typedef struct wf_identity_recommended_credentials {
    char **rotation_keys;
    size_t rotation_key_count;
    char **also_known_as;
    size_t also_known_as_count;
    char  *verification_methods_json;
    char  *services_json;
} wf_identity_recommended_credentials;

/* signPlcOperation output: the signed PLC operation (owned JSON string). */
typedef struct wf_identity_signed_operation {
    char *operation_json;
} wf_identity_signed_operation;

/* resolveIdentity output: the verified DID, handle, and full DID document. */
typedef struct wf_identity_resolve_identity {
    char *did;
    char *handle;
    char *did_doc_json;
} wf_identity_resolve_identity;

/* ---- Parsers (own their outputs; full cleanup on the first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a resolveHandle JSON body ("did"). */
wf_status wf_identity_parse_resolve_handle(const char *json, size_t json_len,
                                           wf_identity_resolve_handle *out);

/* Parse a resolveDid JSON body ("didDoc"), extracting handle / verification
 * methods / services. */
wf_status wf_identity_parse_resolve_did(const char *json, size_t json_len,
                                        wf_identity_resolve_did *out);

/* Parse a getRecommendedDidCredentials JSON body. */
wf_status wf_identity_parse_get_recommended_did_credentials(
    const char *json, size_t json_len,
    wf_identity_recommended_credentials *out);

/* Parse a signPlcOperation JSON body ("operation"). */
wf_status wf_identity_parse_sign_plc_operation(
    const char *json, size_t json_len, wf_identity_signed_operation *out);

/* Parse a resolveIdentity JSON body ("did", "handle", "didDoc"). */
wf_status wf_identity_parse_resolve_identity(const char *json, size_t json_len,
                                             wf_identity_resolve_identity *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_identity_resolve_handle_free(wf_identity_resolve_handle *v);
void wf_identity_resolve_did_free(wf_identity_resolve_did *v);
void wf_identity_recommended_credentials_free(
    wf_identity_recommended_credentials *v);
void wf_identity_signed_operation_free(wf_identity_signed_operation *v);
void wf_identity_resolve_identity_free(wf_identity_resolve_identity *v);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding com.atproto.identity lex call against the
 * agent's PDS XRPC client (after syncing auth) and parses the body into `out`.
 * On success `out` is owned by the caller (free with the matching `_free`); on
 * error it is left reset. Required inputs are validated and return
 * WF_ERR_INVALID_ARG when NULL/empty. Procedure-only endpoints (updateHandle,
 * requestPlcOperationSignature, submitPlcOperation, refreshIdentity) take no
 * owned output. */

/* handle -> did (query). Note: a raw-JSON `wf_agent_resolve_handle(agent,
 * handle, char**)` already exists in agent.h, so the owned-struct variant is
 * suffixed to avoid a symbol collision. */
wf_status wf_agent_resolve_handle_typed(wf_agent *agent, const char *handle,
                                       wf_identity_resolve_handle *out);

/* did -> DID document fields (query). Note: a raw-JSON
 * `wf_agent_resolve_did(agent, did, wf_response*)` already exists in agent.h,
 * so the owned-struct variant is suffixed to avoid a symbol collision. */
wf_status wf_agent_resolve_did_typed(wf_agent *agent, const char *did,
                                     wf_identity_resolve_did *out);

/* Update the authenticated account's handle (procedure). Note: an identical
 * `wf_agent_update_handle(agent, new_handle)` already exists in agent.h, so
 * the owned-struct entry point is suffixed to avoid a symbol collision. */
wf_status wf_agent_update_handle_typed(wf_agent *agent, const char *new_handle);

/* Recommended DID credentials for the authenticated account (query). Note: a
 * raw-JSON `wf_agent_get_recommended_did_credentials(agent, wf_response*)`
 * already exists in agent.h, so the owned-struct variant is suffixed. */
wf_status wf_agent_get_recommended_did_credentials_typed(
    wf_agent *agent, wf_identity_recommended_credentials *out);

/* Request an email containing a PLC operation signature token (procedure).
 * Note: a raw-JSON `wf_agent_request_plc_operation_signature(agent,
 * wf_response*)` already exists in agent.h, suffixed to avoid collision. */
wf_status wf_agent_request_plc_operation_signature_typed(wf_agent *agent,
                                                        const char *did);

/* Sign a PLC operation (procedure). All value arrays/JSON are optional. Note:
 * a raw-JSON `wf_agent_sign_plc_operation(agent, json..., wf_response*)` already
 * exists in agent.h, suffixed to avoid collision. */
wf_status wf_agent_sign_plc_operation_typed(
    wf_agent *agent, const char *token,
    const char *const *rotation_keys, size_t rotation_keys_count,
    const char *const *also_known_as, size_t also_known_as_count,
    const char *verification_methods_json, const char *services_json,
    wf_identity_signed_operation *out);

/* Submit a signed PLC operation (procedure). `operation_json` is the full
 * signed operation JSON. Note: a raw-JSON `wf_agent_submit_plc_operation(agent,
 * json, wf_response*)` already exists in agent.h, suffixed to avoid collision. */
wf_status wf_agent_submit_plc_operation_typed(wf_agent *agent,
                                              const char *operation_json);

/* identifier (handle or DID) -> full identity (query). */
wf_status wf_agent_resolve_identity(wf_agent *agent, const char *identifier,
                                    wf_identity_resolve_identity *out);

/* Re-resolve an identity (procedure). `identifier` is a handle or DID. */
wf_status wf_agent_refresh_identity(wf_agent *agent, const char *identifier);

/* High-level handle rotation. Fetches the account's recommended DID
 * credentials, builds the handle-rotation PLC operation (validating it
 * locally), then signs it server-side via signPlcOperation and submits it via
 * submitPlcOperation.
 *
 * `token` is the one-time signature token delivered out-of-band by email after
 * a com.atproto.identity.requestPlcOperationSignature call. It must be supplied
 * by the caller: the function cannot read it. When `token` is NULL or empty the
 * function first calls requestPlcOperationSignature (emailing the token) and
 * returns WF_ERR_INVALID_ARG honestly instead of fabricating a signature.
 *
 * On WF_OK the rotation has been submitted to the PDS. Ownership: arguments are
 * borrowed; the function performs no heap output of its own. */
wf_status wf_agent_identity_rotate_handle(wf_agent *agent,
                                          const char *new_handle,
                                          const char *token);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_IDENTITY_TYPED_H */
