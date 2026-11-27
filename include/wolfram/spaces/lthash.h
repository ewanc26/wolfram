/**
 * spaces/lthash.h — the LtHash set hash that summarises a space repo
 * (experimental; build with WOLFRAM_BUILD_SPACES). See docs/spaces.md.
 *
 * Each record contributes one element, the bytes of
 * `{collection}/{rkey}/{cid}`. The element is expanded to 2048 bytes with
 * BLAKE3 and added into 1024 little-endian 16-bit lanes modulo 65536, so the
 * state depends only on which elements are present. A commit's `data` is the
 * SHA-256 of the state.
 */

#ifndef WOLFRAM_SPACES_LTHASH_H
#define WOLFRAM_SPACES_LTHASH_H

#include <stddef.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WF_SPACE_LTHASH_BYTES 2048
#define WF_SPACE_DIGEST_BYTES 32

/** The full state. Plain bytes: a host persists and restores it as is. */
typedef struct wf_space_lthash {
    unsigned char state[WF_SPACE_LTHASH_BYTES];
} wf_space_lthash;

/** Reset to the empty set (all zeroes). */
void wf_space_lthash_init(wf_space_lthash *hash);

/** True when the state is that of the empty set. */
int wf_space_lthash_is_empty(const wf_space_lthash *hash);

/** Add one element of `len` bytes. */
wf_status wf_space_lthash_add(wf_space_lthash *hash, const void *element,
                              size_t len);

/** Remove one element. Removing what was never added is not detected. */
wf_status wf_space_lthash_remove(wf_space_lthash *hash, const void *element,
                                 size_t len);

/** SHA-256 of the state, the value a commit's `data` carries. */
wf_status wf_space_lthash_digest(const wf_space_lthash *hash,
                                 unsigned char out[WF_SPACE_DIGEST_BYTES]);

/**
 * Apply one record operation. `prev` is the CID string being replaced or
 * deleted and `cid` the one being written: a create passes only `cid`, a delete
 * only `prev`, an update both. Passing neither is WF_ERR_INVALID_ARG. The
 * collection and record key are validated; a CID must be non-empty and free of
 * '/'. On any error the state is unchanged.
 */
wf_status wf_space_lthash_apply(wf_space_lthash *hash, const char *collection,
                                const char *rkey, const char *prev,
                                const char *cid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SPACES_LTHASH_H */
