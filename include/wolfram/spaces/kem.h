/**
 * spaces/kem.h — the per-request shared secret of a Spaces MAC (experimental;
 * build with WOLFRAM_BUILD_SPACES). See docs/spaces.md.
 *
 * The secret is HPKE secret export (RFC 9180 §6.2) with DHKEM(P-256,
 * HKDF-SHA256), HKDF-SHA256 and the export-only AEAD, `info` "atproto-mac",
 * an empty exporter context and 32 bytes of output. Nothing is encrypted.
 *
 * A syncer encapsulates against the repo author's `#atproto_kem` key and sends
 * the key share as the `keyid` of its MAC request; the repo host decapsulates
 * it. Use a new encapsulation for every request, retry and page.
 */

#ifndef WOLFRAM_SPACES_KEM_H
#define WOLFRAM_SPACES_KEM_H

#include "wolfram/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WF_SPACE_SECRET_BYTES 32

/**
 * Syncer side. `author_kem_key` is the author's `#atproto_kem` key as a P-256
 * did:key or bare multikey. On WF_OK, *out_key_share is the ephemeral public
 * key as a did:key (caller frees) and `secret` holds the shared secret; wipe it
 * once the response is checked. WF_ERR_INVALID_ARG for a key that is not P-256,
 * WF_ERR_PARSE for one that does not decode to a curve point.
 */
wf_status wf_space_kem_encapsulate(const char *author_kem_key,
                                   char **out_key_share,
                                   unsigned char secret[WF_SPACE_SECRET_BYTES]);

/**
 * Repo host side: derive the same secret from the requester's `key_share` with
 * the author's P-256 KEM key. Errors as for wf_space_kem_encapsulate; `secret`
 * is zeroed on any error.
 */
wf_status wf_space_kem_decapsulate(const wf_signing_key *kem_key,
                                   const char *key_share,
                                   unsigned char secret[WF_SPACE_SECRET_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SPACES_KEM_H */
