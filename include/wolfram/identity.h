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
    char *signing_key;      /* multibase-encoded verification key, if present */
    wf_did_method method;
} wf_did_document;

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

/** Release the owned strings inside a wf_did_document. */
void wf_did_document_free(wf_did_document *doc);

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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_IDENTITY_H */
