/**
 * test_mst_depth.c — regression coverage for unbounded MST recursion in
 * wf_mst_walk_node, wf_mst_all_cids_node, wf_mst_count_in_range, and
 * wf_mst_collect_proof (src/repo/mst.c), plus the load/verify helpers.
 *
 * An attacker can shape a CAR whose MST is a chain of DISTINCT blocks
 * linked purely through `left` -- each block an individually valid node
 * (`{"l": <cid>, "e": []}`), each a few dozen bytes -- and the chain can
 * be as deep as the CAR is long: every level lands on a distinct real
 * block, so the older `depth > car->block_count` guard never fires on it.
 * That left the recursion bounded only by the attacker's block count, so a
 * CAR of tens of thousands of tiny blocks drove the C stack that many
 * frames deep and exhausted it. The containment check in
 * wf_mst_get_all_cids catches cycles, not chains of distinct blocks.
 *
 * Fixed by capping depth at WF_MST_MAX_DEPTH (1024), ~8x deeper than any
 * legitimate tree can reach (atproto serializes a node's height in a
 * single uint8, and wf_mst_key_layer itself caps at 128). This test builds
 * a 1026-block chain -- one deeper than the cap -- and asserts every MST
 * entry point rejects it with WF_ERR_PARSE instead of recursing to the
 * bottom, plus a positive control: a single-node tree still walks fine.
 */

#include "wolfram/repo/car.h"
#include "wolfram/repo/mst.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

#define NBLOCKS (WF_MST_MAX_DEPTH + 2)

/* A CBOR link (CID tag 42) wrapping a 37-byte bytestring: 0x00 prefix plus
 * a valid 36-byte CID (version 1, dag-cbor, sha2-256). Returns bytes
 * written. */
static size_t emit_link(unsigned char *out, const unsigned char cid[36]) {
    out[0] = 0xd8;
    out[1] = 0x2a; /* tag 42 */
    out[2] = 0x58;
    out[3] = 0x25; /* bytes(37) */
    out[4] = 0x00; /* CIDv1 version byte */
    memcpy(out + 5, cid, 36);
    return 5 + 36;
}

/* Node with a left pointer and no entries: {"e": [], "l": <link>}. Keys in
 * canonical CBOR order (length, then bytewise): "e" sorts before "l", and
 * the decoder rejects out-of-order keys. */
static size_t emit_left_node(unsigned char *out, const unsigned char left[36]) {
    size_t n = 0;
    out[n++] = 0xa2;
    out[n++] = 0x61;
    out[n++] = 'e';
    out[n++] = 0x80;
    out[n++] = 0x61;
    out[n++] = 'l';
    n += emit_link(out + n, left);
    return n;
}

/* Empty node: {"e": []}. */
static size_t emit_empty_node(unsigned char *out) {
    out[0] = 0xa1;
    out[1] = 0x61;
    out[2] = 'e';
    out[3] = 0x80;
    return 4;
}

/* Fill cid[36] with a valid distinct CID for chain position i:
 * 0x01 0x71 0x12 0x20 (version/dag-cbor/sha2-256) plus the index. */
static void cid_for_index(unsigned char cid[36], size_t i) {
    static const unsigned char prefix[4] = {0x01, 0x71, 0x12, 0x20};
    memset(cid, 0, 36);
    memcpy(cid, prefix, sizeof(prefix));
    cid[35] = (unsigned char)i;
    cid[34] = (unsigned char)(i >> 8);
    cid[33] = (unsigned char)(i >> 16);
}

/* Build a CAR containing `nblocks` chain blocks (B0..B(n-1)) where Bi's
 * node has left = B(i+1) and the final block is empty. *buf is malloc'd;
 * *len holds its length. */
static void build_chain_car(unsigned char **buf, size_t *len, size_t nblocks) {
    unsigned char cids[NBLOCKS][36];
    for (size_t i = 0; i < nblocks; i++) cid_for_index(cids[i], i);

    /* Header: a2 65"roots" 81 <link to B0> 67"version" 01. Keys in
     * canonical CBOR order: "roots" (len 5) sorts before "version" (len 7). */
    unsigned char header[64];
    size_t hn = 0;
    header[hn++] = 0xa2;
    header[hn++] = 0x65;
    memcpy(header + hn, "roots", 5);
    hn += 5;
    header[hn++] = 0x81;
    hn += emit_link(header + hn, cids[0]);
    header[hn++] = 0x67;
    memcpy(header + hn, "version", 7);
    hn += 7;
    header[hn++] = 0x01;

    /* 1 varint byte for the header length, plus, per chain block, a 1-byte
     * section varint + 36-byte CID + node body. */
    size_t cap = 1 + hn + nblocks * (1 + 36 + 48);
    unsigned char *car = malloc(cap);
    if (!car) {
        *buf = NULL;
        *len = 0;
        return;
    }
    size_t p = 0;
    car[p++] = (unsigned char)hn; /* LEB128, hn <= 127 */
    memcpy(car + p, header, hn);
    p += hn;

    for (size_t i = 0; i < nblocks; i++) {
        unsigned char body[64];
        size_t bn = (i + 1 < nblocks) ? emit_left_node(body, cids[i + 1])
                                      : emit_empty_node(body);
        car[p++] = (unsigned char)(36 + bn); /* LEB128, <= 127 */
        memcpy(car + p, cids[i], 36);
        p += 36;
        memcpy(car + p, body, bn);
        p += bn;
    }

    *buf = car;
    *len = p;
}

static wf_status walk_cb(void *ctx, const unsigned char *key, size_t key_len,
                         const wf_cid *value) {
    (void)ctx;
    (void)key;
    (void)key_len;
    (void)value;
    return WF_OK;
}

static void test_chain_too_deep(void) {
    unsigned char *car_bytes = NULL;
    size_t car_len = 0;
    build_chain_car(&car_bytes, &car_len, NBLOCKS);
    WF_CHECK(car_bytes != NULL);

    wf_car car;
    WF_CHECK(wf_car_parse(car_bytes, car_len, &car) == WF_OK);
    free(car_bytes);

    unsigned char root_cid_bytes[36];
    cid_for_index(root_cid_bytes, 0);
    wf_cid root;
    memcpy(root.bytes, root_cid_bytes, 36);
    root.len = 36;

    /* Every MST entry point over this CAR must reject it as malformed at
     * the depth cap rather than recurse the full chain. Pre-fix these all
     * walked to the bottom and returned WF_OK. */
    wf_mst_leaf *leaves = NULL;
    size_t count = 0;
    WF_CHECK(wf_mst_list(&car, &root, &leaves, &count) == WF_ERR_PARSE);

    WF_CHECK(wf_mst_walk_from(&car, &root, NULL, 0, walk_cb, NULL) ==
             WF_ERR_PARSE);

    static const unsigned char coll[] = "x";
    WF_CHECK(wf_mst_paths(&car, &root, coll, 1, &leaves, &count) ==
             WF_ERR_PARSE);

    wf_cid *cids = NULL;
    WF_CHECK(wf_mst_get_all_cids(&car, &root, &cids, &count) == WF_ERR_PARSE);

    static const unsigned char from[] = "a";
    WF_CHECK(wf_mst_get_covering_proof(&car, &root, from, 1, NULL, 0, &cids,
                                       &count) == WF_ERR_PARSE);

    wf_car_free(&car);
}

static void test_single_node_still_works(void) {
    /* A CAR with one empty node must keep working: the cap must not reject
     * anything a legitimate tree can reach. */
    unsigned char *car_bytes = NULL;
    size_t car_len = 0;
    build_chain_car(&car_bytes, &car_len, 1);
    WF_CHECK(car_bytes != NULL);

    wf_car car;
    WF_CHECK(wf_car_parse(car_bytes, car_len, &car) == WF_OK);
    free(car_bytes);

    unsigned char root_cid_bytes[36];
    cid_for_index(root_cid_bytes, 0);
    wf_cid root;
    memcpy(root.bytes, root_cid_bytes, 36);
    root.len = 36;

    WF_CHECK(wf_mst_walk_from(&car, &root, NULL, 0, walk_cb, NULL) == WF_OK);

    wf_mst_leaf *leaves = NULL;
    size_t count = 0;
    WF_CHECK(wf_mst_list(&car, &root, &leaves, &count) == WF_OK);
    WF_CHECK(count == 0);
    wf_mst_leaf_list_free(leaves, count);

    wf_cid *cids = NULL;
    WF_CHECK(wf_mst_get_all_cids(&car, &root, &cids, &count) == WF_OK);
    WF_CHECK(count == 1); /* the root node itself */
    wf_mst_cid_list_free(cids, count);

    static const unsigned char from[] = "a";
    WF_CHECK(wf_mst_get_covering_proof(&car, &root, from, 1, NULL, 0, &cids,
                                       &count) == WF_OK);
    WF_CHECK(count == 0);
    wf_mst_cid_list_free(cids, count);

    wf_car_free(&car);
}

int main(void) {
    test_chain_too_deep();
    test_single_node_still_works();
    WF_TEST_SUMMARY();
}
