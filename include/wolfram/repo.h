/**
 * repo.h — DAG-CBOR, CID, CAR, MST, commit, record operations.
 *
 * AT Protocol repos use DAG-CBOR (a canonical subset of CBOR) for
 * block encoding, content-addressed by CID and shipped as CAR files.
 * This module provides read/write DAG-CBOR decode, CID computation,
 * CAR parse/write, MST traversal/mutation, commit creation, and
 * record CRUD.
 *
 * Current state: DAG-CBOR decode/encode, CID, CAR parse/write, MST
 * (find/add/delete), commit parse/create, and record create/get are
 * implemented and tested.
 */

#ifndef WOLFRAM_REPO_H
#define WOLFRAM_REPO_H

#include <stddef.h>
#include <stdint.h>
#include "wolfram/crypto.h"
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

/**
 * Serialize a CBOR item tree as DAG-CBOR bytes.
 * The map keys must already be in DAG-CBOR sort order (by encoded bytes).
 * Returns a heap-allocated buffer; caller frees via free().
 * Sets *out_len to the byte count. Returns NULL on error.
 */
unsigned char *wf_cbor_serialize(const wf_cbor_item *item, size_t *out_len);

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

/**
 * Serialize a CAR to bytes.
 *
 * Builds the DAG-CBOR header with roots (tag 42) and writes
 * varint-delimited blocks. Returns a heap-allocated buffer.
 */
wf_status wf_car_write(const wf_car *car,
                        unsigned char **out, size_t *out_len);

/* ── Commit ────────────────────────────────────────────────── */

typedef struct wf_commit {
    char   did[256];
    int    version;
    wf_cid data;       /* MST root CID */
    char   rev[64];
    wf_cid prev;       /* zeroed if no prev */
    int    has_prev;
    wf_cid cid;        /* CID of this commit block (set by create) */
    unsigned char sig[64];
    size_t  sig_len;
} wf_commit;

/**
 * Parse a commit object from DAG-CBOR bytes.
 * The `sig` field (if present) is not decoded; it is skipped.
 */
wf_status wf_commit_parse(const unsigned char *cbor, size_t len,
                           wf_commit *out);

/**
 * Create a signed commit and add it to a CAR.
 *
 * Builds the commit DAG-CBOR (without sig), computes its CID, signs the
 * CID bytes with the given key, then re-serializes with the sig field
 * included. The final block is appended to `car` and its CID returned
 * via `out->cid`.
 */
wf_status wf_commit_create(const char *did, const char *rev,
                            const wf_cid *data,
                            const wf_cid *prev,
                            const wf_signing_key *key,
                            wf_car *car,
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
    unsigned       layer;     /* computed from first leaf's key depth */
    wf_cid         left;      /* left subtree CID (zeroed if none) */
    wf_mst_entry  *entries;
    size_t         count;
} wf_mst_node;

/** Parse an MST node from DAG-CBOR bytes. Entries have decompressed keys. */
wf_status wf_mst_node_parse(const unsigned char *cbor, size_t len,
                             const wf_cid *cid, wf_mst_node *out);

/** Build an MST node from raw components (no serialization yet). */
wf_status wf_mst_node_build(unsigned layer, const wf_cid *left,
                             wf_mst_entry *entries, size_t count,
                             wf_mst_node *out);

/**
 * Finalize an MST node: serialize to CBOR, compute CID, and add the
 * resulting block to the CAR. After this, node->cid is set.
 */
wf_status wf_mst_node_finalize(wf_mst_node *node, wf_car *car);

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

/**
 * Add a key-value pair to the MST, producing a new root CID.
 * New blocks are appended to `car`.
 */
wf_status wf_mst_add(wf_car *car, const wf_cid *root_cid,
                      const unsigned char *key, size_t key_len,
                      const wf_cid *value, wf_cid *new_root);

/**
 * Delete a key from the MST, producing a new root CID (or a zeroed
 * CID if the tree becomes empty). New blocks are appended to `car`.
 */
wf_status wf_mst_delete(wf_car *car, const wf_cid *root_cid,
                         const unsigned char *key, size_t key_len,
                         wf_cid *new_root);

/* ── Record operations ─────────────────────────────────────── */

/**
 * Create a record in a repo: encode record CBOR, add to MST, create
 * commit, and return the new commit and record CIDs.
 *
 * The record block is appended to `car`. The commit chain starts from
 * `prev_commit` (NULL for the first commit).
 */
wf_status wf_repo_create_record(wf_car *car,
                                 const wf_cid *prev_commit,
                                 const char *did,
                                 const char *collection,
                                 const char *rkey,
                                 const unsigned char *record_cbor,
                                 size_t record_cbor_len,
                                 const wf_signing_key *key,
                                 wf_cid *out_commit,
                                 wf_cid *out_record);

/**
 * Read a record from a parsed repo CAR by walking the MST.
 *
 * Parses the commit, follows the MST root, finds the record by its
 * collection/rkey key, and returns the record block data.
 */
wf_status wf_repo_get_record(wf_car *car,
                              const wf_cid *commit_cid,
                              const char *collection,
                              const char *rkey,
                              unsigned char **out_data,
                              size_t *out_len,
                              wf_cid *out_record_cid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_H */
