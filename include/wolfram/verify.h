/**
 * verify.h — end-to-end record commit signature verification.
 *
 * The "trust" primitive for a client: given a repo's signing key and the
 * CAR bytes containing a signed commit, verify that the commit signature
 * is authentic. A convenience agent wrapper resolves the DID, fetches the
 * record + commit CAR, and verifies.
 *
 * This module reuses the existing verification core in `wf_sync_verify_commit`
 * (i.e. `wf_repo_verify`, which performs the actual signature + MST-link
 * check over a repository CAR) and adapts it to accept an already-resolved
 * signing key — no DID resolution or network is required for the primitive.
 */

#ifndef WOLFRAM_VERIFY_H
#define WOLFRAM_VERIFY_H

#include <stddef.h>
#include <stdint.h>

#include "wolfram/agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verify the signature over a signed commit against a signing key.
 *
 * `commit_cbor` is the repository CAR (the unit `wf_sync_verify_commit` /
 * `wf_repo_verify` operate on): it must contain the signed commit block and
 * the MST blocks it references. The repo DID is derived from the commit
 * itself and used to anchor verification to the expected author.
 *
 * `signing_key_multibase` is the multibase/did:key verification key (the
 * same format `wf_did_document.signing_key` carries after DID resolution).
 *
 * On WF_OK, *out_valid is set to 1 when the signature (over the commit minus
 * its `sig`) verifies with the key, and 0 otherwise. A non-WF_OK status is
 * returned on malformed input or verification failure.
 *
 * Returns WF_ERR_INVALID_ARG when `signing_key_multibase` or `commit_cbor`
 * is NULL/empty, or `out_valid` is NULL.
 */
wf_status wf_verify_record_commit(const char *signing_key_multibase,
                                  const uint8_t *commit_cbor,
                                  size_t commit_len,
                                  int *out_valid);

/**
 * Convenience agent wrapper that verifies a record's commit signature
 * end-to-end: resolves the repo DID to obtain its signing key, fetches the
 * record and its commit CAR, then verifies the commit signature.
 *
 * Gated: returns WF_ERR_INVALID_ARG immediately when `did`, `collection`, or
 * `rkey` is NULL/empty (so the validation path is exercisable offline). The
 * live resolution + fetch path is guarded behind WF_TEST_LIVE / the
 * WF_TEST_LIVE environment variable.
 *
 * On success *out_valid is set to 1 (commit authentic) or 0 (not).
 */
wf_status wf_agent_verify_record(wf_agent *agent,
                                 const char *did,
                                 const char *collection,
                                 const char *rkey,
                                 int *out_valid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_VERIFY_H */
