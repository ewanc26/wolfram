/**
 * test_block_pager.c — the LRU pager over a consumer block store
 * (include/wolfram/repo/block_pager.h).
 *
 * A tiny in-memory store backs the pager through the real
 * wf_block_store callbacks: a linked list of CID → heap-copied bytes
 * with call counters, so hits, misses and evictions are observable
 * from outside the pager. CIDs come from wf_cid_of_bytes over pattern
 * buffers, except where the test needs a bare opaque key (the pager
 * never verifies a CID matches its bytes; that contract lives with the
 * writer), and those tests say so.
 */

#include "wolfram/repo/block_pager.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

typedef struct mem_rec {
    wf_cid cid;
    unsigned char *data;
    size_t len;
    struct mem_rec *next;
} mem_rec;

typedef struct mem_store {
    mem_rec *head;
    int get_calls;
    int put_calls;
} mem_store;

static wf_status store_get(void *ctx, const wf_cid *cid,
                           unsigned char **data, size_t *data_len) {
    mem_store *s = (mem_store *)ctx;
    mem_rec *r;
    unsigned char *copy;

    s->get_calls++;
    for (r = s->head; r != NULL; r = r->next) {
        if (!cid_equal(&r->cid, cid))
            continue;
        copy = malloc(r->len != 0 ? r->len : (size_t)1);
        if (copy == NULL)
            return WF_ERR_ALLOC;
        memcpy(copy, r->data, r->len);
        *data = copy;
        *data_len = r->len;
        return WF_OK;
    }
    return WF_ERR_NOT_FOUND;
}

static wf_status store_put(void *ctx, const wf_cid *cid,
                           const unsigned char *data, size_t data_len) {
    mem_store *s = (mem_store *)ctx;
    mem_rec *r;
    unsigned char *copy;

    s->put_calls++;
    copy = malloc(data_len != 0 ? data_len : (size_t)1);
    if (copy == NULL)
        return WF_ERR_ALLOC;
    if (data_len != 0)
        memcpy(copy, data, data_len);

    for (r = s->head; r != NULL; r = r->next) {
        if (cid_equal(&r->cid, cid)) {
            free(r->data);
            r->data = copy;
            r->len = data_len;
            return WF_OK;
        }
    }

    r = malloc(sizeof(*r));
    if (r == NULL) {
        free(copy);
        return WF_ERR_ALLOC;
    }
    r->cid = *cid;
    r->data = copy;
    r->len = data_len;
    r->next = s->head;
    s->head = r;
    return WF_OK;
}

static void store_free(mem_store *s) {
    while (s->head != NULL) {
        mem_rec *r = s->head;
        s->head = r->next;
        free(r->data);
        free(r);
    }
}

/* Fill a buffer with a per-seed pattern, so distinct seeds are
 * distinct content and therefore distinct CIDs. */
static void fill_pattern(unsigned char *buf, size_t len, int seed) {
    size_t i;

    for (i = 0; i < len; i++)
        buf[i] = (unsigned char)(seed + (int)i);
}

static wf_status make_cid(wf_cid *cid, int seed, size_t len) {
    unsigned char *buf = malloc(len != 0 ? len : (size_t)1);
    wf_status st;

    if (buf == NULL)
        return WF_ERR_ALLOC;
    fill_pattern(buf, len, seed);
    st = wf_cid_of_bytes(buf, len, cid);
    free(buf);
    return st;
}

/* A bare opaque key: not a hash of anything. Only for tests that need
 * one CID under two different payloads, impossible with real content
 * addressing and legal here because the pager never checks. */
static void opaque_key(wf_cid *cid, unsigned char tag) {
    memset(cid, 0, sizeof(*cid));
    cid->bytes[0] = tag;
    cid->len = 36;
}

static void test_unbounded_pager_refused(void) {
    mem_store s;
    struct wf_block_store bs;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;

    /* Both ceilings zero: that is the resident CAR, not a pager. */
    WF_CHECK(wf_block_pager_new(&bs, 0, 0) == NULL);
    /* No store, or a store missing a callback: nothing to page over. */
    WF_CHECK(wf_block_pager_new(NULL, 4, 1024) == NULL);
    bs.get = NULL;
    WF_CHECK(wf_block_pager_new(&bs, 4, 1024) == NULL);
    bs.get = store_get;
    bs.put = NULL;
    WF_CHECK(wf_block_pager_new(&bs, 4, 1024) == NULL);
    bs.put = store_put;
    store_free(&s);
}

static void test_bogus_arguments_rejected(void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid cid;
    unsigned char buf[8];
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 16, 4096);
    WF_CHECK(p != NULL);

    fill_pattern(buf, sizeof(buf), 1);
    WF_CHECK(make_cid(&cid, 1, sizeof(buf)) == WF_OK);

    WF_CHECK(wf_block_pager_get(NULL, &cid, &got, &got_len) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_block_pager_get(p, NULL, &got, &got_len) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_block_pager_get(p, &cid, NULL, &got_len) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_block_pager_get(p, &cid, &got, NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_block_pager_put(NULL, &cid, buf, sizeof(buf)) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_block_pager_put(p, NULL, buf, sizeof(buf)) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_block_pager_put(p, &cid, NULL, sizeof(buf)) ==
             WF_ERR_INVALID_ARG);
    /* NULL with length zero is an empty block, not an error. */
    WF_CHECK(wf_block_pager_put(p, &cid, NULL, 0) == WF_OK);

    WF_CHECK(wf_block_pager_count(NULL) == 0);
    WF_CHECK(wf_block_pager_bytes(NULL) == 0);
    WF_CHECK(wf_block_pager_evict_all(NULL) == 0);
    wf_block_pager_free(NULL); /* must not crash */

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_put_get_roundtrip_serves_from_cache(void) {
    enum { N = 3, LEN = 32 };
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid cids[N];
    unsigned char buf[LEN];
    size_t i;
    int seed;
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 16, 4096);
    WF_CHECK(p != NULL);

    for (seed = 100, i = 0; i < N; i++, seed++) {
        WF_CHECK(make_cid(&cids[i], seed, LEN) == WF_OK);
        fill_pattern(buf, sizeof(buf), seed);
        WF_CHECK(wf_block_pager_put(p, &cids[i], buf, sizeof(buf)) ==
                 WF_OK);
    }
    WF_CHECK(s.put_calls == N);
    WF_CHECK(s.get_calls == 0);
    WF_CHECK(wf_block_pager_count(p) == N);
    WF_CHECK(wf_block_pager_bytes(p) == N * LEN);

    /* Every read is served from the cache: the store is not asked. */
    for (seed = 100, i = 0; i < N; i++, seed++) {
        WF_CHECK(wf_block_pager_get(p, &cids[i], &got, &got_len) == WF_OK);
        WF_CHECK(got_len == LEN);
        fill_pattern(buf, sizeof(buf), seed);
        WF_CHECK(memcmp(got, buf, LEN) == 0);
    }
    WF_CHECK(s.get_calls == 0);

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_miss_goes_to_the_store(void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid cid;
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 16, 4096);
    WF_CHECK(p != NULL);

    WF_CHECK(make_cid(&cid, 55, 32) == WF_OK);
    WF_CHECK(wf_block_pager_get(p, &cid, &got, &got_len) ==
             WF_ERR_NOT_FOUND);
    WF_CHECK(s.get_calls == 1);
    WF_CHECK(wf_block_pager_count(p) == 0);

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_block_ceiling_evicts_least_recently_used(void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid ca, cb, cc;
    unsigned char buf[32];
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 2, 0); /* byte ceiling off */
    WF_CHECK(p != NULL);

    WF_CHECK(make_cid(&ca, 10, sizeof(buf)) == WF_OK);
    WF_CHECK(make_cid(&cb, 11, sizeof(buf)) == WF_OK);
    WF_CHECK(make_cid(&cc, 12, sizeof(buf)) == WF_OK);

    fill_pattern(buf, sizeof(buf), 10);
    WF_CHECK(wf_block_pager_put(p, &ca, buf, sizeof(buf)) == WF_OK);
    fill_pattern(buf, sizeof(buf), 11);
    WF_CHECK(wf_block_pager_put(p, &cb, buf, sizeof(buf)) == WF_OK);

    /* Touch A: B becomes the least recently used. */
    fill_pattern(buf, sizeof(buf), 10);
    WF_CHECK(wf_block_pager_get(p, &ca, &got, &got_len) == WF_OK);
    WF_CHECK(memcmp(got, buf, sizeof(buf)) == 0);

    /* Admitting C evicts B, not A. */
    fill_pattern(buf, sizeof(buf), 12);
    WF_CHECK(wf_block_pager_put(p, &cc, buf, sizeof(buf)) == WF_OK);
    WF_CHECK(wf_block_pager_count(p) == 2);
    WF_CHECK(s.get_calls == 0);
    WF_CHECK(wf_block_pager_get(p, &ca, &got, &got_len) == WF_OK);
    WF_CHECK(s.get_calls == 0); /* A survived: served from the cache */

    /* B was evicted, but the store still owns it: the get refetches. */
    fill_pattern(buf, sizeof(buf), 11);
    WF_CHECK(wf_block_pager_get(p, &cb, &got, &got_len) == WF_OK);
    WF_CHECK(memcmp(got, buf, sizeof(buf)) == 0);
    WF_CHECK(s.get_calls == 1);
    WF_CHECK(wf_block_pager_count(p) == 2);

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_byte_ceiling_holds(void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid cids[3];
    unsigned char buf[32];
    size_t i;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 0, 64); /* block ceiling off */
    WF_CHECK(p != NULL);

    for (i = 0; i < 3; i++)
        WF_CHECK(make_cid(&cids[i], (int)(20 + i), sizeof(buf)) == WF_OK);

    fill_pattern(buf, sizeof(buf), 20);
    WF_CHECK(wf_block_pager_put(p, &cids[0], buf, sizeof(buf)) == WF_OK);
    fill_pattern(buf, sizeof(buf), 21);
    WF_CHECK(wf_block_pager_put(p, &cids[1], buf, sizeof(buf)) == WF_OK);
    WF_CHECK(wf_block_pager_bytes(p) == 64);
    WF_CHECK(wf_block_pager_count(p) == 2);

    /* The third 32-byte block evicts the least recently used one. */
    fill_pattern(buf, sizeof(buf), 22);
    WF_CHECK(wf_block_pager_put(p, &cids[2], buf, sizeof(buf)) == WF_OK);
    WF_CHECK(wf_block_pager_bytes(p) == 64);
    WF_CHECK(wf_block_pager_count(p) == 2);
    WF_CHECK(s.get_calls == 0);

    /* The evicted block is still in the store. */
    {
        const unsigned char *got = NULL;
        size_t got_len = 0;
        WF_CHECK(wf_block_pager_get(p, &cids[0], &got, &got_len) == WF_OK);
        WF_CHECK(got_len == sizeof(buf));
        WF_CHECK(s.get_calls == 1);
    }

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_block_bigger_than_the_ceiling_is_persisted_not_retained(
    void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid big;
    unsigned char buf[100];
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 0, 64);
    WF_CHECK(p != NULL);

    WF_CHECK(make_cid(&big, 77, sizeof(buf)) == WF_OK);
    fill_pattern(buf, sizeof(buf), 77);
    WF_CHECK(wf_block_pager_put(p, &big, buf, sizeof(buf)) == WF_OK);
    /* Persisted, but never resident. */
    WF_CHECK(s.put_calls == 1);
    WF_CHECK(wf_block_pager_count(p) == 0);
    WF_CHECK(wf_block_pager_bytes(p) == 0);

    /* Still readable: lent from the scratch slot, counted nowhere. */
    fill_pattern(buf, sizeof(buf), 77);
    WF_CHECK(wf_block_pager_get(p, &big, &got, &got_len) == WF_OK);
    WF_CHECK(got_len == sizeof(buf));
    WF_CHECK(memcmp(got, buf, sizeof(buf)) == 0);
    WF_CHECK(wf_block_pager_count(p) == 0);
    WF_CHECK(s.get_calls == 1);

    /* The loan ends on the next call, and a re-get still works. */
    WF_CHECK(wf_block_pager_evict_all(p) == 0);
    fill_pattern(buf, sizeof(buf), 77);
    WF_CHECK(wf_block_pager_get(p, &big, &got, &got_len) == WF_OK);
    WF_CHECK(got_len == sizeof(buf));
    WF_CHECK(wf_block_pager_count(p) == 0);

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_put_copies_the_callers_buffer(void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid cid;
    unsigned char buf[16];
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 16, 4096);
    WF_CHECK(p != NULL);

    WF_CHECK(make_cid(&cid, 90, sizeof(buf)) == WF_OK);
    fill_pattern(buf, sizeof(buf), 90);
    WF_CHECK(wf_block_pager_put(p, &cid, buf, sizeof(buf)) == WF_OK);

    /* Clobber the caller's buffer: both the store and the pager must
     * hold copies, not aliases. */
    memset(buf, 0xFF, sizeof(buf));
    WF_CHECK(wf_block_pager_get(p, &cid, &got, &got_len) == WF_OK);
    WF_CHECK(got_len == sizeof(buf));
    {
        unsigned char expect[16];
        fill_pattern(expect, sizeof(expect), 90);
        WF_CHECK(memcmp(got, expect, sizeof(expect)) == 0);
    }

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_same_cid_replaced_in_place(void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid key;
    unsigned char small[16];
    unsigned char large[32];
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 16, 4096);
    WF_CHECK(p != NULL);

    /* A bare opaque key: real content addressing would give the same
     * CID only for identical bytes, and this test needs one key under
     * two payloads. The pager does not check; this is the test's
     * licence, not an invitation to callers. */
    opaque_key(&key, 0xA5);
    fill_pattern(small, sizeof(small), 31);
    WF_CHECK(wf_block_pager_put(p, &key, small, sizeof(small)) == WF_OK);
    WF_CHECK(wf_block_pager_count(p) == 1);

    fill_pattern(large, sizeof(large), 41);
    WF_CHECK(wf_block_pager_put(p, &key, large, sizeof(large)) == WF_OK);
    WF_CHECK(wf_block_pager_count(p) == 1); /* replaced, not added */
    WF_CHECK(wf_block_pager_bytes(p) == sizeof(large));
    WF_CHECK(s.put_calls == 2); /* both writes went through */

    WF_CHECK(wf_block_pager_get(p, &key, &got, &got_len) == WF_OK);
    WF_CHECK(got_len == sizeof(large));
    WF_CHECK(memcmp(got, large, sizeof(large)) == 0);

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_byte_ceiling_busting_replacement_drops_the_entry(void) {
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid key;
    unsigned char small[16];
    unsigned char huge[100];

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 16, 64);
    WF_CHECK(p != NULL);

    opaque_key(&key, 0x5A);
    fill_pattern(small, sizeof(small), 51);
    WF_CHECK(wf_block_pager_put(p, &key, small, sizeof(small)) == WF_OK);
    WF_CHECK(wf_block_pager_count(p) == 1);

    /* The same key written back too big to be resident: persisted, and
     * the stale entry must not linger under the old bytes. */
    fill_pattern(huge, sizeof(huge), 52);
    WF_CHECK(wf_block_pager_put(p, &key, huge, sizeof(huge)) == WF_OK);
    WF_CHECK(s.put_calls == 2);
    WF_CHECK(wf_block_pager_count(p) == 0);
    WF_CHECK(wf_block_pager_bytes(p) == 0);

    wf_block_pager_free(p);
    store_free(&s);
}

static void test_evict_all_empties_the_pager_not_the_store(void) {
    enum { N = 3, LEN = 24 };
    mem_store s;
    struct wf_block_store bs;
    wf_block_pager *p;
    wf_cid cids[N];
    unsigned char buf[LEN];
    size_t i;
    int seed;
    const unsigned char *got = NULL;
    size_t got_len = 0;

    memset(&s, 0, sizeof(s));
    bs.get = store_get;
    bs.put = store_put;
    bs.ctx = &s;
    p = wf_block_pager_new(&bs, 16, 4096);
    WF_CHECK(p != NULL);

    for (seed = 200, i = 0; i < N; i++, seed++) {
        WF_CHECK(make_cid(&cids[i], seed, LEN) == WF_OK);
        fill_pattern(buf, sizeof(buf), seed);
        WF_CHECK(wf_block_pager_put(p, &cids[i], buf, sizeof(buf)) ==
                 WF_OK);
    }
    WF_CHECK(wf_block_pager_count(p) == N);

    WF_CHECK(wf_block_pager_evict_all(p) == N);
    WF_CHECK(wf_block_pager_count(p) == 0);
    WF_CHECK(wf_block_pager_bytes(p) == 0);

    /* Everything is still in the store: refetching re-admits. */
    seed = 200;
    for (i = 0; i < N; i++, seed++) {
        WF_CHECK(make_cid(&cids[i], seed, LEN) == WF_OK);
        fill_pattern(buf, sizeof(buf), seed);
        WF_CHECK(wf_block_pager_get(p, &cids[i], &got, &got_len) == WF_OK);
        WF_CHECK(got_len == LEN);
        WF_CHECK(memcmp(got, buf, LEN) == 0);
    }
    WF_CHECK(wf_block_pager_count(p) == N);
    WF_CHECK(s.get_calls == N);

    wf_block_pager_free(p);
    store_free(&s);
}

int main(void) {
    test_unbounded_pager_refused();
    test_bogus_arguments_rejected();
    test_put_get_roundtrip_serves_from_cache();
    test_miss_goes_to_the_store();
    test_block_ceiling_evicts_least_recently_used();
    test_byte_ceiling_holds();
    test_block_bigger_than_the_ceiling_is_persisted_not_retained();
    test_put_copies_the_callers_buffer();
    test_same_cid_replaced_in_place();
    test_byte_ceiling_busting_replacement_drops_the_entry();
    test_evict_all_empties_the_pager_not_the_store();
    WF_TEST_SUMMARY();
}
