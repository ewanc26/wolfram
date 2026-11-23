/**
 * block_pager.c — a bounded LRU cache of repo blocks in front of a
 * consumer-supplied block store.
 *
 * The flat wf_car stays the default repo representation; this pager is
 * the interface consumers reach for when repo bytes are already durable
 * somewhere (MetalBear's SQLite blocks table is the first customer) and
 * memory must track the working set instead of the repo. Wolfram owns
 * the paging: CID hashing, LRU order, the ceilings and the copies; the
 * consumer owns where the bytes live, through the wf_block_store
 * callbacks in the header.
 *
 * Host only: excluded from embedded builds (see CMakeLists.txt), so a
 * console client cannot link against it by accident.
 */

#include "wolfram/repo/block_pager.h"

#include <stdlib.h>
#include <string.h>

typedef struct wf_pager_node {
    wf_cid cid;
    unsigned char *data; /* the node owns these bytes */
    size_t len;
    /* LRU list orders, MRU at p->mru, LRU at p->lru. */
    struct wf_pager_node *lru_prev;
    struct wf_pager_node *lru_next;
    /* Hash chain (separate chaining, never reordered). */
    struct wf_pager_node *chain_next;
} wf_pager_node;

struct wf_block_pager {
    const struct wf_block_store *store;
    size_t max_blocks; /* 0: no block ceiling */
    size_t max_bytes;  /* 0: no byte ceiling */
    size_t count;
    size_t bytes;
    wf_pager_node *mru; /* most recently used; NULL when empty */
    wf_pager_node *lru; /* least recently used; evicted first */
    wf_pager_node **buckets;
    size_t bucket_count;
    wf_pager_node *scratch; /* one lent-out block too big to be resident */
};

/* FNV-1a over the raw CID bytes: short, decent spread, wrapping by
 * design. Anything reasonable works; this one has no dependencies. */
static size_t cid_bucket(const wf_block_pager *p, const wf_cid *cid) {
    unsigned long h = 2166136261UL;
    size_t i;

    for (i = 0; i < cid->len; i++) {
        h ^= (unsigned long)cid->bytes[i];
        h *= 16777619UL;
    }
    return (size_t)(h % (unsigned long)p->bucket_count);
}

static wf_pager_node *find_node(const wf_block_pager *p, const wf_cid *cid) {
    wf_pager_node *n = p->buckets[cid_bucket(p, cid)];

    while (n != NULL && !cid_equal(&n->cid, cid))
        n = n->chain_next;
    return n;
}

/* Takes ownership of `data`; keeps the CID by value. */
static wf_pager_node *node_new(const wf_cid *cid, unsigned char *data,
                               size_t len) {
    wf_pager_node *n = malloc(sizeof(*n));

    if (n == NULL)
        return NULL;
    n->cid = *cid;
    n->data = data;
    n->len = len;
    n->lru_prev = NULL;
    n->lru_next = NULL;
    n->chain_next = NULL;
    return n;
}

static void drop_scratch(wf_block_pager *p) {
    if (p->scratch != NULL) {
        free(p->scratch->data);
        free(p->scratch);
        p->scratch = NULL;
    }
}

static void lru_unlink(wf_block_pager *p, wf_pager_node *n) {
    if (n->lru_prev != NULL)
        n->lru_prev->lru_next = n->lru_next;
    else
        p->mru = n->lru_next;
    if (n->lru_next != NULL)
        n->lru_next->lru_prev = n->lru_prev;
    else
        p->lru = n->lru_prev;
}

static void lru_push_mru(wf_block_pager *p, wf_pager_node *n) {
    n->lru_prev = NULL;
    n->lru_next = p->mru;
    if (p->mru != NULL)
        p->mru->lru_prev = n;
    else
        p->lru = n;
    p->mru = n;
}

/* Detach a resident node and free it: hash, LRU list, counters, in that
 * order. */
static void drop_node(wf_block_pager *p, wf_pager_node *n) {
    wf_pager_node **pp = &p->buckets[cid_bucket(p, &n->cid)];

    while (*pp != NULL && *pp != n)
        pp = &(*pp)->chain_next;
    if (*pp == n)
        *pp = n->chain_next;

    lru_unlink(p, n);
    p->count--;
    p->bytes -= n->len;
    free(n->data);
    free(n);
}

static void evict_to_fit(wf_block_pager *p) {
    while (p->lru != NULL &&
           ((p->max_blocks != 0 && p->count > p->max_blocks) ||
            (p->max_bytes != 0 && p->bytes > p->max_bytes)))
        drop_node(p, p->lru);
}

/* Make `n` resident: hash it in, make it the MRU, then evict LRU
 * entries until both ceilings hold. Callers check first that `n` fits
 * the byte ceiling alone (evict_to_fit could evict `n` itself
 * otherwise). */
static void admit(wf_block_pager *p, wf_pager_node *n) {
    size_t b = cid_bucket(p, &n->cid);

    n->chain_next = p->buckets[b];
    p->buckets[b] = n;
    lru_push_mru(p, n);
    p->count++;
    p->bytes += n->len;
    evict_to_fit(p);
}

wf_block_pager *wf_block_pager_new(const struct wf_block_store *store,
                                   size_t max_blocks, size_t max_bytes) {
    wf_block_pager *p;
    size_t expect;
    size_t buckets;

    if (store == NULL || store->get == NULL || store->put == NULL)
        return NULL;
    if (max_blocks == 0 && max_bytes == 0)
        return NULL;

    p = calloc((size_t)1, sizeof(*p));
    if (p == NULL)
        return NULL;
    p->store = store;
    p->max_blocks = max_blocks;
    p->max_bytes = max_bytes;

    /* Bucket count: keep chains short for the block ceiling when there
     * is one, else estimate a block per 256 bytes. Capped so no
     * ceiling can demand a silly table; long chains on pathological
     * ceilings are acceptable, the tables are never wrong. */
    if (max_blocks != 0)
        expect = max_blocks > 65536 ? 65536 : max_blocks;
    else
        expect = max_bytes / 256 + 1 > 65536 ? 65536 : max_bytes / 256 + 1;

    buckets = 32;
    while (buckets < expect * 2) /* expect <= 65536, so no overflow */
        buckets *= 2;

    p->buckets = calloc(buckets, sizeof(*p->buckets));
    if (p->buckets == NULL) {
        free(p);
        return NULL;
    }
    p->bucket_count = buckets;
    return p;
}

wf_status wf_block_pager_get(wf_block_pager *p, const wf_cid *cid,
                             const unsigned char **data, size_t *len) {
    wf_pager_node *n;
    unsigned char *buf;
    size_t buf_len;
    wf_status st;

    if (p == NULL || cid == NULL || data == NULL || len == NULL)
        return WF_ERR_INVALID_ARG;

    n = find_node(p, cid);
    if (n != NULL) {
        lru_unlink(p, n);
        lru_push_mru(p, n);
        *data = n->data;
        *len = n->len;
        return WF_OK;
    }

    st = p->store->get(p->store->ctx, cid, &buf, &buf_len);
    if (st != WF_OK)
        return st;
    if (buf == NULL)
        return WF_ERR_STATE; /* a store must lend a heap buffer */

    drop_scratch(p); /* any earlier lent-out scratch is now invalid */

    n = node_new(cid, buf, buf_len);
    if (n == NULL) {
        free(buf);
        return WF_ERR_ALLOC;
    }

    /* Too big ever to fit an active byte ceiling: it can never be
     * resident, so the scratch slot (not the LRU, not the counters)
     * holds it for the life of this loan. */
    if (p->max_bytes != 0 && buf_len > p->max_bytes)
        p->scratch = n;
    else
        admit(p, n);

    *data = buf;
    *len = buf_len;
    return WF_OK;
}

wf_status wf_block_pager_put(wf_block_pager *p, const wf_cid *cid,
                             const unsigned char *data, size_t len) {
    wf_pager_node *n;
    unsigned char *copy;

    if (p == NULL || cid == NULL || (data == NULL && len != 0))
        return WF_ERR_INVALID_ARG;

    /* Durability outranks residency: persist before anything else. */
    {
        wf_status st = p->store->put(p->store->ctx, cid, data, len);
        if (st != WF_OK)
            return st;
    }

    drop_scratch(p);

    /* Does the block alone bust an active byte ceiling? Then it can
     * never be resident, and any stale entry for this CID must not
     * stay resident either. */
    if (p->max_bytes != 0 && len > p->max_bytes) {
        n = find_node(p, cid);
        if (n != NULL)
            drop_node(p, n);
        return WF_OK;
    }

    copy = malloc(len != 0 ? len : (size_t)1);
    if (copy == NULL)
        return WF_ERR_ALLOC; /* stored, not cached */
    if (len != 0)
        memcpy(copy, data, len);

    n = find_node(p, cid);
    if (n != NULL) {
        /* Same CID again (in practice the same content: CIDs address
         * content). Replace the entry, keep it MRU, refit. */
        lru_unlink(p, n);
        p->bytes -= n->len;
        p->bytes += len;
        free(n->data);
        n->data = copy;
        n->len = len;
        lru_push_mru(p, n);
        evict_to_fit(p);
        return WF_OK;
    }

    n = node_new(cid, copy, len);
    if (n == NULL) {
        free(copy);
        return WF_ERR_ALLOC; /* stored, not cached */
    }
    admit(p, n);
    return WF_OK;
}

size_t wf_block_pager_count(const wf_block_pager *p) {
    return p == NULL ? 0 : p->count;
}

size_t wf_block_pager_bytes(const wf_block_pager *p) {
    return p == NULL ? 0 : p->bytes;
}

size_t wf_block_pager_evict_all(wf_block_pager *p) {
    size_t dropped = 0;

    if (p == NULL)
        return 0;
    while (p->lru != NULL) {
        drop_node(p, p->lru);
        dropped++;
    }
    drop_scratch(p);
    return dropped;
}

void wf_block_pager_free(wf_block_pager *p) {
    if (p == NULL)
        return;
    (void)wf_block_pager_evict_all(p); /* also drops the scratch slot */
    free(p->buckets);
    free(p);
}
