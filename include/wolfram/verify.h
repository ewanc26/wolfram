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
 *
 * To verify a commit/record for a DID without the caller supplying the raw
 * signing key, install an injectable DID-key resolver via
 * wf_verify_set_key_resolver. The resolver returns the DID's signing key as a
 * `did:key:z...` / multibase string (the same format `wf_did_document.
 * signing_key` carries and `wf_verify_record_commit` accepts). This lets
 * verification fetch the signing key WITHOUT the verify module taking a hard
 * dependency on the transport layer. When no resolver is installed, the
 * resolve-and-verify entry points return an honest WF_ERR_INVALID_ARG rather
 * than fabricating a key — signatures are never accepted by default.
 */

#ifndef WOLFRAM_VERIFY_H
#define WOLFRAM_VERIFY_H

#include <stddef.h>
#include <stdint.h>

#include "wolfram/agent.h"
#include "wolfram/xrpc.h"

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

/**
 * Injectable DID-key resolver for commit/label verification.
 *
 * Verification needs the repo DID's signing key (a `did:key:z...` /
 * multibase string, identical in format to `wf_did_document.signing_key`
 * and to the `signing_key_multibase` accepted by wf_verify_record_commit).
 * By default wolfram performs NO network resolution: it relies on the caller
 * to supply the key. This callback lets a caller inject a resolver (e.g. a
 * DID-document lookup, an in-memory cache, or a test double) so verification
 * can fetch the signing key for a DID without the verify module taking a hard
 * dependency on the transport.
 *
 * The resolver is optional. When unset and verification is asked to resolve a
 * DID, wolfram returns an honest WF_ERR_INVALID_ARG rather than fabricating a
 * key — signatures are never accepted by default.
 *
 * @param did       The repo DID whose signing key is requested.
 * @param key_id    Optional verification-method id (may be NULL). Most DIDs
 *                  expose a single signing key; callers that resolve multiple
 *                  verification methods pass the id to disambiguate.
 * @param userdata  Opaque pointer supplied at registration time.
 * @param out_key   On WF_OK, set to a heap-allocated signing key string owned
 *                  by the caller (free() it). Left untouched on error.
 *
 * @return WF_OK with a non-NULL *out_key on success; WF_ERR_NOT_FOUND when the
 *         DID has no usable signing key; or another WF_ERR_* on failure.
 */
typedef wf_status (*wf_verify_key_resolver)(const char *did,
                                            const char *key_id,
                                            void *userdata,
                                            char **out_key);

/**
 * Install (or clear) the process-wide DID-key resolver used by
 * wf_verify_record_commit_resolved and wf_agent_verify_record.
 *
 * Pass NULL for `cb` to restore the default honest-error behavior (no
 * resolver). The resolver is stored as plain process-wide state: call this
 * once at startup before any verification runs; calling it concurrently with
 * verification is unsupported.
 */
void wf_verify_set_key_resolver(wf_verify_key_resolver cb, void *userdata);

/**
 * Convenience built-in resolver backed by wf_did_resolve (network transport).
 * Install it with wf_verify_set_key_resolver and pass your wf_xrpc_client* as
 * `userdata`; it resolves `did` and returns the document's canonical
 * `#atproto` signing key. `key_id` may be NULL, "atproto", "#atproto", or the
 * absolute `<did>#atproto`; any other key ID returns WF_ERR_NOT_FOUND.
 */
wf_status wf_verify_resolve_via_did(const char *did,
                                    const char *key_id,
                                    void *userdata,
                                    char **out_key);

/**
 * Resolve the signing key for `did` via the installed resolver, falling back
 * to a live wf_did_resolve over `client` when no resolver is installed and a
 * transport client is available. Used by the firehose commit verifier
 * (wf_sync_verify_commit) so the same injected resolver can power both the
 * record-commit and stream paths; when no resolver is set and `client` is
 * NULL it returns an honest WF_ERR_INVALID_ARG.
 */
wf_status wf_verify_resolve_signing_key(const char *did,
                                        const char *key_id,
                                        wf_xrpc_client *client,
                                        char **out_key);

/**
 * Verify a signed commit CAR using the injected resolver to obtain the DID's
 * signing key (instead of a caller-supplied key).
 *
 * `commit_cbor`/`commit_len` and `out_valid` are as in wf_verify_record_commit.
 * The DID embedded in the commit (used to anchor the signature check) must
 * match `did`; the resolver is queried for `did` to fetch the key.
 *
 * When no resolver is installed the function returns an honest
 * WF_ERR_INVALID_ARG (it cannot proceed without a key source) — never a
 * false pass.
 */
wf_status wf_verify_record_commit_resolved(const char *did,
                                            const uint8_t *commit_cbor,
                                            size_t commit_len,
                                            int *out_valid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_VERIFY_H */
