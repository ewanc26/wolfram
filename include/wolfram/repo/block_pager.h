#ifndef WOLFRAM_REPO_BLOCK_PAGER_H
#define WOLFRAM_REPO_BLOCK_PAGER_H

#include <stddef.h>
#include "wolfram/xrpc.h"
#include "wolfram/repo/cid.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Storage the consumer owns and Wolfram pages over.
 *
 *  Wolfram opens no database, file or socket to satisfy this interface:
 *  the consumer supplies the bytes' durable home (MetalBear's SQLite
 *  `blocks` table is the first customer) and Wolfram supplies the
 *  bounded working set held in front of it. Keep the flat `wf_car` when
 *  the whole repo is wanted in memory; reach for the pager when memory
 *  must track the working set instead of the repo.
 */
struct wf_block_store {
    /** Fetch the bytes for `cid`. Return WF_OK and set `*data` /
     *  `*data_len` to a heap buffer the pager takes over (it is freed
     *  with free()), or WF_ERR_NOT_FOUND when the CID is absent, or
     *  another wf_status for a storage failure. */
    wf_status (*get)(void *ctx, const wf_cid *cid, unsigned char **data,
                     size_t *data_len);

    /** Persist a block. `data` belongs to the caller and may be freed
     *  on return; the store must keep a copy. */
    wf_status (*put)(void *ctx, const wf_cid *cid, const unsigned char *data,
                     size_t data_len);

    /** Opaque consumer state, passed to every callback. May be NULL. */
    void *ctx;
};

/** A bounded LRU cache of repo blocks in front of a wf_block_store.
 *  Opaque. Host builds only for now: the source is excluded from
 *  embedded (WOLFRAM_BUILD_EMBEDDED) builds, so a console client cannot
 *  link against it by accident. Not thread-safe: the caller
 *  serialises calls on a pager, and store callbacks run on the
 *  caller's thread.
 *
 *  The pager does not verify that a block's bytes match its CID;
 *  content addressing is the writer's job (wf_cid_of_block /
 *  wf_cid_of_bytes) and the store trusts its caller the same way.
 */
typedef struct wf_block_pager wf_block_pager;

/** Create a pager with two ceilings, at most `max_blocks` resident
 *  blocks and at most `max_bytes` resident bytes. Either ceiling alone
 *  bounds the cache (pass 0 for the one not wanted); both zero is
 *  refused, as are a NULL store or a store missing a callback: an
 *  unbounded pager is just the resident CAR again. Returns NULL on
 *  allocation failure. */
wf_block_pager *wf_block_pager_new(const struct wf_block_store *store,
                                   size_t max_blocks, size_t max_bytes);

/** Fetch a block by CID, satisfying it from the LRU when it is hot and
 *  from the store (admitting the store's buffer to the LRU) when it
 *  is not. On WF_OK, `*data` / `*len` are borrowed and stay valid until
 *  the next call on this pager that lends memory or frees it (get,
 *  put, evict_all, free); copy anything that must outlive that.
 *  WF_ERR_INVALID_ARG for NULL arguments. Store failures pass through
 *  unchanged, WF_ERR_NOT_FOUND included. */
wf_status wf_block_pager_get(wf_block_pager *p, const wf_cid *cid,
                             const unsigned char **data, size_t *len);

/** Write through: persist the block via the store, then admit a copy
 *  to the LRU, evicting least-recently-used entries to stay under both
 *  ceilings. A block that alone busts an active byte ceiling is
 *  persisted but never resident, and an existing entry for the CID is
 *  replaced, or dropped when the new bytes bust a ceiling. The pager
 *  copies `data`; the caller keeps its buffer (and may reuse or free
 *  it on return). WF_ERR_ALLOC means the store's put already succeeded
 *  but the pager could not copy: stored, not cached. */
wf_status wf_block_pager_put(wf_block_pager *p, const wf_cid *cid,
                             const unsigned char *data, size_t len);

/** Blocks currently resident in the LRU. The scratch slot (at most
 *  one block too big ever to be resident, lent by get) is not counted. */
size_t wf_block_pager_count(const wf_block_pager *p);

/** Bytes currently resident in the LRU, on the same terms as
 *  wf_block_pager_count. */
size_t wf_block_pager_bytes(const wf_block_pager *p);

/** Drop every resident block, returning how many were dropped. Nothing
 *  is deleted from the store: eviction is about memory, not durability.
 *  For GC passes, account eviction and tests. */
size_t wf_block_pager_evict_all(wf_block_pager *p);

/** Free the pager and every block it holds. The store is untouched. */
void wf_block_pager_free(wf_block_pager *p);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_BLOCK_PAGER_H */
