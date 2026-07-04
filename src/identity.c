/**
 * identity.c — stubs for DID/handle resolution.
 *
 * Nothing here is implemented yet. wf_did_method_of is the one
 * function worth having early since it's pure string inspection and
 * everything else depends on knowing which branch to take.
 */

#include "wolfram/identity.h"

#include <stdlib.h>
#include <string.h>

wf_did_method wf_did_method_of(const char *did) {
    if (!did) return WF_DID_METHOD_UNKNOWN;

    if (strncmp(did, "did:plc:", 8) == 0) {
        return WF_DID_METHOD_PLC;
    }
    if (strncmp(did, "did:web:", 8) == 0) {
        return WF_DID_METHOD_WEB;
    }
    return WF_DID_METHOD_UNKNOWN;
}

wf_status wf_did_resolve(wf_xrpc_client *client,
                          const char *did,
                          wf_did_document *out) {
    (void)client;
    (void)did;
    (void)out;
    /* TODO: did:plc -> GET plc.directory/<did>
     *       did:web -> GET https://<host>/.well-known/did.json
     * Parse the returned DID document JSON for the PDS service
     * endpoint and signing key. Needs a JSON parser wired in first
     * (see AGENTS.md on dependency choices). */
    return WF_ERR_INVALID_ARG;
}

void wf_did_document_free(wf_did_document *doc) {
    if (!doc) return;
    free(doc->did);
    free(doc->pds_endpoint);
    free(doc->signing_key);
    doc->did = NULL;
    doc->pds_endpoint = NULL;
    doc->signing_key = NULL;
}

wf_status wf_handle_resolve(wf_xrpc_client *client,
                             const char *handle,
                             char **out_did) {
    (void)client;
    (void)handle;
    (void)out_did;
    /* TODO: DNS TXT lookup for _atproto.<handle>, falling back to
     * GET https://<handle>/.well-known/atproto-did. */
    return WF_ERR_INVALID_ARG;
}
