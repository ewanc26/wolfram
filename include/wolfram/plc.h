/**
 * plc.h — DID PLC operation build / sign / submit helpers.
 *
 * Implements the write side of the did:plc directory protocol: assembling a
 * `plc_operation` JSON document, signing it (over its DAG-CBOR SHA-256, the
 * canonical form the PLC registry expects), and submitting it via the
 * com.atproto.identity XRPC procedures. All network I/O goes through the
 * wf_xrpc_* transport; all hashing uses OpenSSL SHA-256; all signing uses the
 * existing wf_sign / wf_verify primitives in crypto.h.
 *
 * A signed PLC operation has the shape:
 *
 *   {
 *     "type": "plc_operation",
 *     "rotationKeys": ["did:key:..."],
 *     "verificationMethods": { "atproto": "did:key:..." },
 *     "services": { "atproto_pds": "https://..." },
 *     "alsoKnownAs": ["at://handle.example"],
 *     "prev": "<CID string>" | null,
 *     "sig": { "<signer did:key>": "<base64url sig>" }
 *   }
 *
 * The signature is computed over the SHA-256 of the operation's canonical
 * DAG-CBOR encoding with the `sig` field omitted, matching the reference
 * atproto implementation (rsky/indigo).
 *
 * Every heap-allocated output has a matching `_free` documented next to it.
 * Output strings are owned by the caller.
 */

#ifndef WOLFRAM_PLC_H
#define WOLFRAM_PLC_H

#include "wolfram/crypto.h"
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A set of updates applied to build a new PLC operation. */
typedef struct wf_plc_operation_update {
    /** Rotation keys (array of did:key strings). NULL/0 -> empty array. */
    const char *const *rotation_keys;
    size_t             rotation_keys_count;
    /**
     * verificationMethods map as a JSON object string, e.g.
     * "{\"atproto\":\"did:key:...\"}". NULL -> empty object.
     */
    const char *verification_methods_json;
    /**
     * services map as a JSON object string, e.g.
     * "{\"atproto_pds\":\"https://...\"}". NULL -> empty object.
     */
    const char *services_json;
    /** alsoKnownAs entries (array of "at://..." strings). NULL/0 -> empty. */
    const char *const *also_known_as;
    size_t             also_known_as_count;
    /**
     * Previous operation CID string, or NULL for a genesis operation.
     * For non-genesis updates this must be the CID of the current operation
     * as published in the PLC directory.
     */
    const char *prev;
} wf_plc_operation_update;

/**
 * Assemble an unsigned `plc_operation` JSON from the given update.
 *
 * On WF_OK, *out_json is a heap-allocated, NUL-terminated JSON string and
 * must be released with wf_plc_operation_free.
 */
wf_status wf_plc_operation_build(const wf_plc_operation_update *update,
                                 char **out_json);

/** Free a JSON string owned by wf_plc_operation_build / wf_plc_operation_sign. */
void wf_plc_operation_free(char *json);

/**
 * Sign an unsigned operation JSON with `key`.
 *
 * The signer's did:key is derived from `key` (the key must be populated) and
 * used both as the signing identity and as the `sig` map key. The signature
 * is the base64url (no padding) encoding of the 64-byte compact ECDSA
 * signature over the operation's canonical DAG-CBOR SHA-256.
 *
 * On WF_OK, *out_signed_json is a heap-allocated, NUL-terminated JSON string
 * (the input with a `sig` map attached) and must be released with
 * wf_plc_operation_free.
 */
wf_status wf_plc_operation_sign(const char *op_json,
                                const wf_signing_key *key,
                                char **out_signed_json);

/**
 * Verify a signed operation's signature.
 *
 * Recomputes the canonical DAG-CBOR SHA-256 (with `sig` omitted) and verifies
 * it against the public key named by the (first) `sig` map key using
 * wf_verify. On WF_OK, *out_signer_didkey is the heap-allocated did:key whose
 * signature was verified and must be released with free().
 */
wf_status wf_plc_operation_verify(const char *signed_json,
                                  char **out_signer_didkey);

/**
 * High-level: resolve the DID document, build the operation diff from the
 * provided update, sign it, and return the signed operation JSON.
 *
 * `update->prev` must contain the CID of the account's current published
 * operation (obtained from the PLC directory); pass NULL only for a genesis
 * operation. The DID is resolved via wf_did_resolve to confirm it is a
 * resolvable did:plc before building.
 *
 * On WF_OK, *out_signed_json must be released with wf_plc_operation_free.
 */
wf_status wf_plc_sign_operation(wf_xrpc_client *client,
                                const char *did,
                                const wf_plc_operation_update *update,
                                const wf_signing_key *key,
                                char **out_signed_json);

/**
 * Submit a signed operation to com.atproto.identity.submitPlcOperation.
 * `signed_op_json` must be the full signed operation JSON.
 */
wf_status wf_plc_submit_operation(wf_xrpc_client *client,
                                  const char *signed_op_json);

/**
 * Request a PLC operation signature token from
 * com.atproto.identity.requestPlcOperationSignature. The `did` receives an
 * email with a code used as the `token` field of a signPlcOperation call.
 */
wf_status wf_plc_request_signature(wf_xrpc_client *client, const char *did);

/**
 * Transport-level wrapper over com.atproto.identity.updateHandle. Updates the
 * authenticated account's handle and (for did:plc accounts) the PLC document.
 * Requires auth to be set on `client`.
 */
wf_status wf_plc_update_handle(wf_xrpc_client *client, const char *handle);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_PLC_H */
