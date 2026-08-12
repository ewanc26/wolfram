#ifndef WOLFRAM_REPO_MST_H
#define WOLFRAM_REPO_MST_H

#include <stddef.h>
#include "wolfram/xrpc.h"
#include "wolfram/repo/cid.h"
#include "wolfram/repo/car.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum MST nesting depth accepted when walking / counting / proving /
 * loading an MST from untrusted CAR data. Every recursive MST descent in
 * this SDK checks depth against it.
 *
 * A legitimate tree cannot nest anywhere near this deep: atproto stores a
 * node's height in a single uint8 (max 255), and wolfram's own key-layer
 * derivation caps at 128 (wf_mst_key_layer counts at most 4 leading-zero
 * steps per hash byte). A pure-left chain of distinct blocks, on the other
 * hand, can be arbitrarily deep in a well-formed CAR -- one block per
 * level, each a few dozen bytes -- so a cap keyed to block count (e.g.
 * `depth > car->block_count`) is no bound on stack usage at all: it only
 * fires on cycles. This constant is what actually limits how deep the
 * recursion can go before rejecting the tree as malformed, independent of
 * how many tiny blocks an attacker stuffs into the CAR.
 */
#define WF_MST_MAX_DEPTH 1024

/** A decompressed entry (leaf) in an MST node. */
typedef struct wf_mst_entry {
    unsigned char *key;
    size_t key_len;
    wf_cid value;
    wf_cid subtree;
} wf_mst_entry;

/** An MST node parsed from a DAG-CBOR block. */
typedef struct wf_mst_node {
    wf_cid cid;
    unsigned layer;
    wf_cid left;
    wf_mst_entry *entries;
    size_t count;
} wf_mst_node;

unsigned wf_mst_key_layer(const unsigned char *key, size_t key_len);
wf_status wf_mst_node_parse(const unsigned char *cbor, size_t len,
                            const wf_cid *cid, wf_mst_node *out);
wf_status wf_mst_node_build(unsigned layer, const wf_cid *left,
                            wf_mst_entry *entries, size_t count,
                            wf_mst_node *out);
wf_status wf_mst_node_finalize(wf_mst_node *node, wf_car *car);
void wf_mst_node_free(wf_mst_node *node);
/**
 * Look up `key` in the MST rooted at `root_cid`, walking blocks in `car`.
 *
 * This does not recompute block hashes -- it trusts that each block's
 * declared CID in `car` actually matches its bytes. Only call this on a
 * `car` that has already passed integrity verification (wf_repo_verify /
 * wf_repo_diff_verify), or on a server's own locally-authored CAR. A `car`
 * populated straight from a remote fetch (e.g. wf_sync_get_record /
 * wf_sync_get_blocks) without that check first could have a block whose
 * bytes were substituted under the same declared CID, and this function
 * would silently return the substituted value.
 */
wf_status wf_mst_find(wf_car *car, const wf_cid *root_cid,
                      const unsigned char *key, size_t key_len, wf_cid *out);
wf_status wf_mst_add(wf_car *car, const wf_cid *root_cid,
                     const unsigned char *key, size_t key_len,
                     const wf_cid *value, wf_cid *new_root);
wf_status wf_mst_delete(wf_car *car, const wf_cid *root_cid,
                        const unsigned char *key, size_t key_len,
                        wf_cid *new_root);

/** A leaf entry (record key + value CID) returned by MST traversal. */
typedef struct wf_mst_leaf {
    unsigned char *key;
    size_t key_len;
    wf_cid value;
} wf_mst_leaf;

/**
 * Callback invoked for each leaf visited during a walk. Returning a non-WF_OK
 * status aborts the walk and propagates that error to the caller.
 */
typedef wf_status (*wf_mst_walk_cb)(void *ctx, const unsigned char *key,
                                    size_t key_len, const wf_cid *value);

/**
 * In-order depth-first walk over every leaf with key >= `from_key` (or every
 * leaf when `from_key` is NULL), in ascending MST key order. The walk visits
 * left subtrees, then each entry's subtree, then the entry leaf itself.
 */
wf_status wf_mst_walk_from(wf_car *car, const wf_cid *root,
                           const unsigned char *from_key, size_t from_key_len,
                           wf_mst_walk_cb cb, void *ctx);

/**
 * Collect every leaf (key + value CID) in sorted order.
 * Ownership: *out is caller-owned; free it with wf_mst_leaf_list_free.
 */
wf_status wf_mst_list(wf_car *car, const wf_cid *root, wf_mst_leaf **out,
                      size_t *out_count);
void wf_mst_leaf_list_free(wf_mst_leaf *list, size_t count);

/**
 * Collect every leaf whose key begins with `collection`+"/" (i.e. records of
 * a given collection) in sorted order.
 * Ownership: *out is caller-owned; free it with wf_mst_leaf_list_free.
 */
wf_status wf_mst_paths(wf_car *car, const wf_cid *root,
                       const unsigned char *collection, size_t collection_len,
                       wf_mst_leaf **out, size_t *out_count);

/**
 * Collect every CID referenced by the MST — node CIDs, leaf value CIDs, and
 * intermediate subtree CIDs — each collected exactly once (the MST is a DAG).
 * Ownership: *out is caller-owned; free it with wf_mst_cid_list_free.
 */
wf_status wf_mst_get_all_cids(wf_car *car, const wf_cid *root, wf_cid **out,
                              size_t *out_count);
void wf_mst_cid_list_free(wf_cid *list, size_t count);

/**
 * Collect the set of MST node CIDs whose subtree contains at least one leaf
 * in [from_key, to_key) — a valid (over-approximating) covering proof for the
 * key range, as used by com.atproto.sync record/list queries. Pass NULL for
 * `to_key` to cover through the end of the tree.
 * Ownership: *out is caller-owned; free it with wf_mst_cid_list_free.
 */
wf_status wf_mst_get_covering_proof(wf_car *car, const wf_cid *root,
                                    const unsigned char *from_key,
                                    size_t from_key_len,
                                    const unsigned char *to_key,
                                    size_t to_key_len, wf_cid **out,
                                    size_t *out_count);

/**
 * The MST node path from `root` down to where `key` lives, present or not --
 * an inclusion or non-inclusion proof for a single key, matching the
 * reference's MST.cidsForPath (mst/mst.ts, used by com.atproto.sync.getRecord
 * via repo/sync/provider.ts's getRecords). Every node visited is included in
 * *out_node_cids, unlike wf_mst_get_covering_proof's range pruning: an empty
 * subtree pointer just ends the walk without omitting the node it ended at.
 *
 * *out_leaf_cid is zeroed (len == 0) when `key` is absent; otherwise it names
 * the leaf's value CID, which the caller must also include in the proof
 * alongside *out_node_cids.
 * Ownership: *out_node_cids is caller-owned; free it with wf_mst_cid_list_free.
 */
wf_status wf_mst_cids_for_path(wf_car *car, const wf_cid *root_cid,
                               const unsigned char *key, size_t key_len,
                               wf_cid **out_node_cids, size_t *out_node_count,
                               wf_cid *out_leaf_cid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_MST_H */