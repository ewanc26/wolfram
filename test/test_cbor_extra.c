#include "wolfram/repo/cbor.h"
#include "wolfram/repo/cid.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── item constructors (owned by caller; wf_cbor_free frees recursively) ── */
static wf_cbor_item *mk_uint(uint64_t v) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_UNSIGNED;
    i->uinteger = v;
    return i;
}
static wf_cbor_item *mk_nint(uint64_t v) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_NEGATIVE;
    i->neginteger = v;
    return i;
}
static wf_cbor_item *mk_str(const char *s) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_STRING;
    size_t l = strlen(s);
    i->string.str = malloc(l + 1);
    memcpy(i->string.str, s, l);
    i->string.str[l] = '\0';
    i->string.len = l;
    return i;
}
static wf_cbor_item *mk_bytes(const unsigned char *d, size_t n) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_BYTES;
    i->bytes.data = malloc(n ? n : 1);
    if (n) memcpy(i->bytes.data, d, n);
    i->bytes.len = n;
    return i;
}
static wf_cbor_item *mk_link(const unsigned char *d, size_t n) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_LINK;
    i->bytes.data = malloc(n ? n : 1);
    if (n) memcpy(i->bytes.data, d, n);
    i->bytes.len = n;
    return i;
}
static wf_cbor_item *mk_map2(wf_cbor_item *k1, wf_cbor_item *v1,
                             wf_cbor_item *k2, wf_cbor_item *v2) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_MAP;
    i->map.count = 2;
    i->map.pairs = malloc(2 * sizeof(wf_cbor_pair));
    i->map.pairs[0].key = k1;   i->map.pairs[0].value = v1;
    i->map.pairs[1].key = k2;   i->map.pairs[1].value = v2;
    return i;
}
static wf_cbor_item *mk_arr(wf_cbor_item **items, size_t n) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_ARRAY;
    i->children.count = n;
    i->children.items = malloc(n * sizeof(wf_cbor_item *));
    for (size_t j = 0; j < n; j++) i->children.items[j] = items[j];
    return i;
}

static void ser_eq(const wf_cbor_item *item,
                   const unsigned char *expected, size_t n) {
    size_t len = 0;
    unsigned char *buf = wf_cbor_serialize(item, &len);
    WF_CHECK(buf != NULL);
    if (buf) {
        WF_CHECK(len == n);
        WF_CHECK(memcmp(buf, expected, n) == 0);
        free(buf);
    }
}

/* ── serialize round-trips for each major type ── */
static void test_serialize_scalars(void) {
    ser_eq(mk_uint(0), (const unsigned char[]){0x00}, 1);
    ser_eq(mk_uint(23), (const unsigned char[]){0x17}, 1);
    ser_eq(mk_uint(24), (const unsigned char[]){0x18, 0x18}, 2);
    ser_eq(mk_uint(256), (const unsigned char[]){0x19, 0x01, 0x00}, 3);
    ser_eq(mk_nint(0), (const unsigned char[]){0x20}, 1);
    ser_eq(mk_nint(24), (const unsigned char[]){0x38, 0x18}, 2);
    ser_eq(mk_str("hi"), (const unsigned char[]){0x62, 'h', 'i'}, 3);
    unsigned char b[] = {0x01, 0x02, 0x03};
    ser_eq(mk_bytes(b, 3), (const unsigned char[]){0x43, 0x01, 0x02, 0x03}, 4);
}

/* ── serialize a map reorders keys into ascending byte order ── */
static void test_serialize_map_sorted(void) {
    wf_cbor_item *m = mk_map2(mk_str("b"), mk_uint(1),
                              mk_str("a"), mk_uint(2));
    unsigned char exp[] = {0xa2, 0x61, 'a', 0x02, 0x61, 'b', 0x01};
    ser_eq(m, exp, sizeof(exp));
    wf_cbor_free(m);
}

/* ── serialize an array of mixed types round-trips ── */
static void test_serialize_array_mixed(void) {
    wf_cbor_item *items[] = { mk_uint(1), mk_str("x") };
    wf_cbor_item *arr = mk_arr(items, 2);
    unsigned char exp[] = {0x82, 0x01, 0x61, 'x'};
    ser_eq(arr, exp, sizeof(exp));
    wf_cbor_free(arr);
}

/* ── serialize a CID link (tag 42 + historical 0x00 prefix) ── */
static void test_serialize_link(void) {
    unsigned char cid[36] = {0x01, 0x71, 0x12, 0x20};
    wf_cbor_item *l = mk_link(cid, 36);
    unsigned char exp[41];
    exp[0] = 0xD8; exp[1] = 0x2A; exp[2] = 0x58; exp[3] = 0x25; exp[4] = 0x00;
    memcpy(exp + 5, cid, 36);
    ser_eq(l, exp, sizeof(exp));
    wf_cbor_free(l);
}

/* ── serialize then re-parse yields equal structure (canonical round-trip) ── */
static void test_roundtrip_parse(void) {
    /* input canonical bytes for {"a":1,"b":2} */
    unsigned char in[] = {0xa2, 0x61, 'a', 0x01, 0x61, 'b', 0x02};
    wf_cbor_item *p = wf_cbor_parse(in, sizeof(in));
    WF_CHECK(p != NULL);
    WF_CHECK(p->type == WF_CBOR_MAP);
    WF_CHECK(p->map.count == 2);
    WF_CHECK(p->map.pairs[0].key->string.str[0] == 'a');
    WF_CHECK(p->map.pairs[0].value->uinteger == 1);
    WF_CHECK(p->map.pairs[1].key->string.str[0] == 'b');
    WF_CHECK(p->map.pairs[1].value->uinteger == 2);

    size_t len = 0;
    unsigned char *out = wf_cbor_serialize(p, &len);
    WF_CHECK(out != NULL);
    WF_CHECK(len == sizeof(in));
    WF_CHECK(memcmp(out, in, sizeof(in)) == 0);
    free(out);
    wf_cbor_free(p);
}

/* ── additional invalid parse cases ── */
static void test_parse_invalid_extra(void) {
    /* non-canonical 1-byte uint (24 encoded using single byte) */
    unsigned char bad_uint[] = {0x18, 0x00};
    WF_CHECK(wf_cbor_parse(bad_uint, sizeof(bad_uint)) == NULL);

    /* invalid CBOR tag other than 42 */
    unsigned char bad_tag[] = {0xD8, 0x2B, 0x40};
    WF_CHECK(wf_cbor_parse(bad_tag, sizeof(bad_tag)) == NULL);

    /* tag 42 but missing leading 0x00 byte on the CID */
    unsigned char bad_cid[] = {0xD8, 0x2A, 0x58, 0x24, 0x01, 0x71, 0x12, 0x20};
    WF_CHECK(wf_cbor_parse(bad_cid, sizeof(bad_cid)) == NULL);

    /* non-string map key */
    unsigned char bad_key[] = {0xA1, 0x01, 0x02};
    WF_CHECK(wf_cbor_parse(bad_key, sizeof(bad_key)) == NULL);

    /* empty / null input */
    WF_CHECK(wf_cbor_parse(NULL, 0) == NULL);
    WF_CHECK(wf_cbor_parse((const unsigned char *)"", 0) == NULL);
}

/* ── CID helpers round-trips ── */
static void test_cid_roundtrip(void) {
    /* known vector from bluesky-social/atproto car-file-fixtures */
    unsigned char cbor[] = {0xa1, 0x64, 't', 'e', 's', 't',
                            0x64, 'r', 'o', 'o', 't'};
    wf_cid cid = {{0}, 0};
    WF_CHECK(wf_cid_of_block(cbor, sizeof(cbor), &cid) == WF_OK);
    WF_CHECK(cid.len == 36);
    WF_CHECK(cid.bytes[0] == 0x01);
    WF_CHECK(cid.bytes[1] == 0x71);

    char *str = wf_cid_to_string(&cid);
    WF_CHECK(str != NULL);
    WF_CHECK(strcmp(str,
        "bafyreiapldaco7m23c7qzc4w42r7kxmcswm64nkindtuh4vwztrpoe7m5m") == 0);

    wf_cid parsed = {{0}, 0};
    WF_CHECK(wf_cid_from_string(str, &parsed) == WF_OK);
    WF_CHECK(cid_equal(&cid, &parsed));
    free(str);

    /* generic block -> to_string -> from_string preserves the CID */
    unsigned char block[] = {0x01};
    wf_cid c2 = {{0}, 0};
    WF_CHECK(wf_cid_of_block(block, sizeof(block), &c2) == WF_OK);
    char *s2 = wf_cid_to_string(&c2);
    WF_CHECK(s2 != NULL);
    wf_cid c3 = {{0}, 0};
    WF_CHECK(wf_cid_from_string(s2, &c3) == WF_OK);
    WF_CHECK(cid_equal(&c2, &c3));
    free(s2);
}

static void test_cid_from_string_invalid(void) {
    wf_cid out = {{0}, 0};
    WF_CHECK(wf_cid_from_string("notacid", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_cid_from_string("bafyrei", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_cid_from_string(NULL, &out) == WF_ERR_INVALID_ARG);
}

int main(void) {
    test_serialize_scalars();
    test_serialize_map_sorted();
    test_serialize_array_mixed();
    test_serialize_link();
    test_roundtrip_parse();
    test_parse_invalid_extra();
    test_cid_roundtrip();
    test_cid_from_string_invalid();
    WF_TEST_SUMMARY();
}
