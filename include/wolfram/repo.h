/**
 * repo.h — repository state: MST, CBOR, CAR, CID.
 *
 * The AT Protocol repo is a Merkle Search Tree of DAG-CBOR-encoded
 * records, content-addressed by CID and shipped over the wire as CAR
 * files. This header scaffolds the pieces wolfram will eventually
 * need to read and write repos locally.
 *
 * Nothing here is implemented yet — see src/repo.c for stubs. Start
 * with CBOR decode (read-only) before touching MST balancing; it's
 * the highest-leverage piece and easiest to test in isolation.
 */

#ifndef WOLFRAM_REPO_H
#define WOLFRAM_REPO_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A content identifier (CIDv1, dag-cbor, sha2-256), stored raw. */
typedef struct wf_cid {
    unsigned char bytes[36]; /* multihash-prefixed digest, upper bound */
    size_t        len;
} wf_cid;

/** Render a CID as its base32 (CIDv1) string form. Caller frees. */
char *wf_cid_to_string(const wf_cid *cid);

/**
 * Compute the CID of a DAG-CBOR-encoded block.
 *
 * TODO: not yet implemented — needs a DAG-CBOR encoder and a
 * SHA-256 implementation before this can do anything real.
 */
wf_status wf_cid_of_block(const unsigned char *cbor, size_t cbor_len,
                           wf_cid *out);

/**
 * Parse a CAR (Content Addressable aRchive) byte stream into its
 * header and block list. Scaffolding only — see src/repo.c.
 */
typedef struct wf_car_block {
    wf_cid         cid;
    unsigned char *data;
    size_t         data_len;
} wf_car_block;

typedef struct wf_car {
    wf_cid         *roots;
    size_t          root_count;
    wf_car_block   *blocks;
    size_t          block_count;
} wf_car;

wf_status wf_car_parse(const unsigned char *car, size_t car_len, wf_car *out);
void      wf_car_free(wf_car *car);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_H */
