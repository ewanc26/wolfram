#ifndef WOLFRAM_REPO_CID_H
#define WOLFRAM_REPO_CID_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A content identifier (CIDv1, dag-cbor, sha2-256), stored raw. */
typedef struct wf_cid {
    unsigned char bytes[36];
    size_t len;
} wf_cid;

/** Render a CID as its base32 (CIDv1) string form. Caller frees. */
char *wf_cid_to_string(const wf_cid *cid);

/** Compute the CID of a DAG-CBOR-encoded block (CIDv1, dag-cbor, sha2-256). */
wf_status wf_cid_of_block(const unsigned char *cbor, size_t cbor_len,
                          wf_cid *out);

/** Check if two CIDs are equal. */
int cid_equal(const wf_cid *a, const wf_cid *b);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_CID_H */