/**
 * repo.h — DAG-CBOR decode, CID, CAR.
 *
 * AT Protocol repos use DAG-CBOR (a canonical subset of CBOR) for
 * block encoding, content-addressed by CID and shipped as CAR files.
 * This module provides read-only DAG-CBOR decode, with CID and CAR
 * parsing building on top.
 *
 * Current state: DAG-CBOR decoder and CID computation are implemented
 * and tested. CAR parsing and MST are still stubbed.
 */

#ifndef WOLFRAM_REPO_H
#define WOLFRAM_REPO_H

#include <stddef.h>
#include <stdint.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── DAG-CBOR ──────────────────────────────────────────────── */

typedef enum wf_cbor_type {
    WF_CBOR_UNSIGNED = 0,
    WF_CBOR_NEGATIVE = 1,
    WF_CBOR_BYTES    = 2,
    WF_CBOR_STRING   = 3,
    WF_CBOR_ARRAY    = 4,
    WF_CBOR_MAP      = 5,
    WF_CBOR_SIMPLE   = 7,
} wf_cbor_type;

typedef struct wf_cbor_pair {
    struct wf_cbor_item *key;
    struct wf_cbor_item *value;
} wf_cbor_pair;

typedef struct wf_cbor_item {
    wf_cbor_type type;
    union {
        uint64_t                uinteger;
        uint64_t                neginteger;
        struct { unsigned char *data; size_t len; } bytes;
        struct { char          *str;  size_t len; } string;
        struct { struct wf_cbor_item **items; size_t count; } children;
        struct { struct wf_cbor_pair *pairs; size_t count; } map;
        int                     simple_value;
    };
} wf_cbor_item;

/**
 * Parse DAG-CBOR-encoded bytes into an item tree.
 *
 * Returns NULL on parse error or DAG-CBOR constraint violation
 * (floats, tags, indefinite-length, non-canonical integers,
 * unsorted map keys). The returned tree must be freed with
 * wf_cbor_free.
 */
wf_cbor_item *wf_cbor_parse(const unsigned char *data, size_t len);

/** Free a tree returned by wf_cbor_parse. Safe to call with NULL. */
void wf_cbor_free(wf_cbor_item *item);

/* ── CID ───────────────────────────────────────────────────── */

/** A content identifier (CIDv1, dag-cbor, sha2-256), stored raw. */
typedef struct wf_cid {
    unsigned char bytes[36];
    size_t        len;
} wf_cid;

/** Render a CID as its base32 (CIDv1) string form. Caller frees. */
char *wf_cid_to_string(const wf_cid *cid);

/**
 * Compute the CID of a DAG-CBOR-encoded block (CIDv1, dag-cbor, sha2-256).
 */
wf_status wf_cid_of_block(const unsigned char *cbor, size_t cbor_len,
                           wf_cid *out);

/* ── CAR ───────────────────────────────────────────────────── */

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

/** Find a block by CID in a parsed CAR. Returns NULL if not found. */
wf_car_block *wf_car_find_block(wf_car *car, const wf_cid *cid);

/* ── Commit ────────────────────────────────────────────────── */

typedef struct wf_commit {
    char   did[256];
    int    version;
    wf_cid data;       /* MST root CID */
    char   rev[64];
    wf_cid prev;       /* zeroed if no prev */
    int    has_prev;
} wf_commit;

/**
 * Parse a commit object from DAG-CBOR bytes.
 * The `sig` field (if present) is not decoded; it is skipped.
 */
wf_status wf_commit_parse(const unsigned char *cbor, size_t len,
                           wf_commit *out);

/* ── MST ───────────────────────────────────────────────────── */

/** Compute the MST layer of a key: SHA-256 leading-zero bits / 2. */
unsigned wf_mst_key_layer(const unsigned char *key, size_t key_len);

/** A decompressed entry (leaf) in an MST node. */
typedef struct wf_mst_entry {
    unsigned char *key;       /* full reconstructed key (heap) */
    size_t         key_len;
    wf_cid         value;     /* CID of the record */
    wf_cid         subtree;   /* right subtree CID (zeroed if none) */
} wf_mst_entry;

/** An MST node parsed from a DAG-CBOR block. */
typedef struct wf_mst_node {
    wf_cid         cid;
    wf_cid         left;      /* left subtree CID (zeroed if none) */
    wf_mst_entry  *entries;
    size_t         count;
} wf_mst_node;

/** Parse an MST node from DAG-CBOR bytes. Entries have decompressed keys. */
wf_status wf_mst_node_parse(const unsigned char *cbor, size_t len,
                             const wf_cid *cid, wf_mst_node *out);

/** Free an MST node and its entries. Safe to call with NULL. */
void wf_mst_node_free(wf_mst_node *node);

/**
 * Find a record by key in an MST loaded from a CAR file.
 * Traverses the tree recursively, loading nodes on demand.
 * Returns WF_OK and sets `out` if found, WF_ERR_NOT_FOUND otherwise.
 */
wf_status wf_mst_find(wf_car *car, const wf_cid *root_cid,
                       const unsigned char *key, size_t key_len,
                       wf_cid *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_H */
