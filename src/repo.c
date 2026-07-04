/**
 * repo.c — stubs for MST/CBOR/CAR/CID handling.
 *
 * Start here, in roughly this order, when picking this back up:
 *   1. DAG-CBOR decode (read-only) — needed before anything else can
 *      be tested against real repo data.
 *   2. CID computation over a decoded block (needs SHA-256; consider
 *      a small vendored implementation rather than pulling in a full
 *      TLS library just for hashing).
 *   3. CAR parsing — mostly a varint-length-prefixed block reader once
 *      CBOR decode exists.
 *   4. MST traversal/verification — depends on all of the above.
 */

#include "wolfram/repo.h"

#include <stdlib.h>

char *wf_cid_to_string(const wf_cid *cid) {
    (void)cid;
    /* TODO: base32 (RFC4648, lowercase, no padding) encode with the
     * "b" multibase prefix, per CIDv1 string representation. */
    return NULL;
}

wf_status wf_cid_of_block(const unsigned char *cbor, size_t cbor_len,
                           wf_cid *out) {
    (void)cbor;
    (void)cbor_len;
    (void)out;
    return WF_ERR_INVALID_ARG;
}

wf_status wf_car_parse(const unsigned char *car, size_t car_len, wf_car *out) {
    (void)car;
    (void)car_len;
    (void)out;
    return WF_ERR_INVALID_ARG;
}

void wf_car_free(wf_car *car) {
    if (!car) return;

    for (size_t i = 0; i < car->block_count; i++) {
        free(car->blocks[i].data);
    }
    free(car->blocks);
    free(car->roots);

    car->blocks = NULL;
    car->roots = NULL;
    car->block_count = 0;
    car->root_count = 0;
}
