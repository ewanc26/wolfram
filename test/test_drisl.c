/**
 * test_drisl.c — DRISL canonical CBOR compliance suite (offline).
 *
 * DRISL (Deterministic Representation for Interoperable Structures and Links)
 * is the AT Protocol profile of DAG-CBOR. This suite asserts the four
 * determinism rules at the byte level:
 *   (1) map keys sorted in ascending bytewise order;
 *   (2) integers in shortest-form major-type + argument encoding;
 *   (3) NaN/Infinity (floats) rejected;
 *   (4) CIDs encoded as CBOR tag 42 with the historical 0x00 prefix.
 *
 * No external dependencies; builds a wf_cbor_item tree via the public struct
 * and checks the exact bytes produced by wf_cbor_serialize.
 */

#include "wolfram/repo.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── minimal item builders (public struct only) ─────────── */

static wf_cbor_item *mk_str(const char *s) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->type = WF_CBOR_STRING;
    it->string.len = strlen(s);
    it->string.str = malloc(it->string.len ? it->string.len : 1);
    if (it->string.str && it->string.len)
        memcpy(it->string.str, s, it->string.len);
    return it;
}

static wf_cbor_item *mk_uint(uint64_t v) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->type = WF_CBOR_UNSIGNED;
    it->uinteger = v;
    return it;
}

static wf_cbor_item *mk_nint(uint64_t v) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->type = WF_CBOR_NEGATIVE;
    it->neginteger = v; /* -1 is stored as 0, matching CBOR encoding */
    return it;
}

static wf_cbor_item *mk_map2(wf_cbor_item *k0, wf_cbor_item *v0,
                             wf_cbor_item *k1, wf_cbor_item *v1) {
    wf_cbor_item *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->type = WF_CBOR_MAP;
    m->map.count = 2;
    m->map.pairs = calloc(2, sizeof(wf_cbor_pair));
    if (!m->map.pairs) { free(m); return NULL; }
    m->map.pairs[0].key = k0;   m->map.pairs[0].value = v0;
    m->map.pairs[1].key = k1;   m->map.pairs[1].value = v1;
    return m;
}

int main(void) {
    /* ── (1) sorted map keys ──
     * Build {"b": 2, "a": 1} with keys out of order; DRISL requires the
     * serialized form to be {"a": 1, "b": 2}. */
    {
        wf_cbor_item *map = mk_map2(mk_str("b"), mk_uint(2),
                                    mk_str("a"), mk_uint(1));
        WF_CHECK(map != NULL);
        unsigned char *out = NULL;
        size_t out_len = 0;
        if (map) out = wf_cbor_serialize(map, &out_len);
        static const unsigned char expect[] = {
            0xA2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x02
        };
        WF_CHECK(out != NULL);
        WF_CHECK(out_len == sizeof(expect));
        WF_CHECK(out && memcmp(out, expect, sizeof(expect)) == 0);
        /* And the first key emitted must be "a", not "b". */
        WF_CHECK(out && out[1] == 0x61 && out[2] == 'a');
        free(out);
        if (map) wf_cbor_free(map);
    }

    /* ── (2) shortest-form integers ── */
    {
        struct { uint64_t v; int neg; unsigned char bytes[9]; size_t n; } t[] = {
            {   0, 0, {0x00}, 1 },
            {  23, 0, {0x17}, 1 },
            {  24, 0, {0x18, 0x18}, 2 },
            { 255, 0, {0x18, 0xFF}, 2 },
            { 256, 0, {0x19, 0x01, 0x00}, 3 },
            {   0, 1, {0x20}, 1 },   /* -1 */
            {  23, 1, {0x37}, 1 },   /* -24 */
            {  24, 1, {0x38, 0x18}, 2 }, /* -25 */
        };
        for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
            wf_cbor_item *it = t[i].neg ? mk_nint(t[i].v) : mk_uint(t[i].v);
            WF_CHECK(it != NULL);
            unsigned char *out = NULL;
            size_t out_len = 0;
            if (it) out = wf_cbor_serialize(it, &out_len);
            WF_CHECK(out != NULL);
            WF_CHECK(out_len == t[i].n);
            WF_CHECK(out && memcmp(out, t[i].bytes, t[i].n) == 0);
            free(out);
            if (it) wf_cbor_free(it);
        }

        /* Non-canonical overlong encodings must NOT be produced: 256 is
         * emitted with a 2-byte argument (0x19 0x01 0x00), not 5 bytes. */
        wf_cbor_item *big = mk_uint(256);
        size_t ln = 0;
        unsigned char *out = wf_cbor_serialize(big, &ln);
        WF_CHECK(big && out && ln == 3 && out[0] == 0x19);
        free(out);
        if (big) wf_cbor_free(big);
    }

    /* ── (4) CID tag 42 round-trip ──
     * Reuse the test_repo.c fixture: tag 42 + 0x00 prefix + 36-byte CID. */
    {
        unsigned char cid[36];
        memset(cid, 0, sizeof(cid));
        cid[0] = 0x01; cid[1] = 0x71; cid[2] = 0x12; cid[3] = 0x20;

        unsigned char link[41] = {0xD8, 0x2A, 0x58, 0x25, 0x00};
        memcpy(link + 5, cid, sizeof(cid));

        wf_cbor_item *item = wf_cbor_parse(link, sizeof(link));
        WF_CHECK(item && item->type == WF_CBOR_LINK);
        WF_CHECK(item && item->bytes.len == sizeof(cid) &&
                 memcmp(item->bytes.data, cid, sizeof(cid)) == 0);

        unsigned char *out = NULL;
        size_t out_len = 0;
        if (item) out = wf_cbor_serialize(item, &out_len);

        /* Must be tag 42 (0xD8 0x2A) followed by the exact input bytes. */
        WF_CHECK(out && out_len == sizeof(link));
        WF_CHECK(out && out[0] == 0xD8 && out[1] == 0x2A);
        WF_CHECK(out && memcmp(out, link, sizeof(link)) == 0);

        free(out);
        if (item) wf_cbor_free(item);
    }

    /* ── (3) NaN / Infinity rejection ──
     * wolfram's wf_cbor_item has no float type, so the encoder can never
     * emit a float; the decoder (which feeds the encoder) rejects them.
     * Assert NaN and Infinity parse as invalid (NULL). */
    {
        unsigned char nan[] = {0xFA, 0x7F, 0xC0, 0x00, 0x00};
        unsigned char inf[] = {0xFA, 0x7F, 0x80, 0x00, 0x00};
        WF_CHECK(wf_cbor_parse(nan, sizeof(nan)) == NULL);
        WF_CHECK(wf_cbor_parse(inf, sizeof(inf)) == NULL);
    }

    WF_TEST_SUMMARY();
}
