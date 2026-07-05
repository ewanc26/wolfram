/**
 * test_repo.c — unit tests for the DAG-CBOR decoder.
 *
 * Tests parsing, accessor, and error cases with hand-crafted and
 * known-good CBOR byte sequences. No external dependencies.
 */

#include "wolfram/repo.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* Helper: compare a parsed unsigned integer item. */
static int check_uint(const wf_cbor_item *item, uint64_t expected) {
    return item && item->type == WF_CBOR_UNSIGNED && item->uinteger == expected;
}

static int check_nint(const wf_cbor_item *item, uint64_t expected_neg) {
    return item && item->type == WF_CBOR_NEGATIVE && item->neginteger == expected_neg;
}

static int check_bytes(const wf_cbor_item *item,
                       const unsigned char *expected, size_t len) {
    return item && item->type == WF_CBOR_BYTES &&
           item->bytes.len == len &&
           memcmp(item->bytes.data, expected, len) == 0;
}

static int check_string(const wf_cbor_item *item, const char *expected) {
    size_t len = strlen(expected);
    return item && item->type == WF_CBOR_STRING &&
           item->string.len == len &&
           strcmp(item->string.str, expected) == 0;
}

static int check_simple(const wf_cbor_item *item, int which) {
    return item && item->type == WF_CBOR_SIMPLE &&
           item->simple_value == which;
}

static int check_array_len(const wf_cbor_item *item, size_t n) {
    return item && item->type == WF_CBOR_ARRAY &&
           item->children.count == n;
}

static int check_map_len(const wf_cbor_item *item, size_t n) {
    return item && item->type == WF_CBOR_MAP &&
           item->map.count == n;
}

int main(void) {
    /* ── unsigned integers ── */
    {
        unsigned char zero[] = {0x00};
        wf_cbor_item *item = wf_cbor_parse(zero, 1);
        WF_CHECK(check_uint(item, 0));
        wf_cbor_free(item);
    }
    {
        unsigned char one[] = {0x01};
        wf_cbor_item *item = wf_cbor_parse(one, 1);
        WF_CHECK(check_uint(item, 1));
        wf_cbor_free(item);
    }
    {
        unsigned char v23[] = {0x17};
        wf_cbor_item *item = wf_cbor_parse(v23, 1);
        WF_CHECK(check_uint(item, 23));
        wf_cbor_free(item);
    }
    {
        unsigned char v24[] = {0x18, 0x18};
        wf_cbor_item *item = wf_cbor_parse(v24, 2);
        WF_CHECK(check_uint(item, 24));
        wf_cbor_free(item);
    }
    {
        unsigned char v255[] = {0x18, 0xFF};
        wf_cbor_item *item = wf_cbor_parse(v255, 2);
        WF_CHECK(check_uint(item, 255));
        wf_cbor_free(item);
    }
    {
        unsigned char v256[] = {0x19, 0x01, 0x00};
        wf_cbor_item *item = wf_cbor_parse(v256, 3);
        WF_CHECK(check_uint(item, 256));
        wf_cbor_free(item);
    }
    {
        unsigned char v65535[] = {0x19, 0xFF, 0xFF};
        wf_cbor_item *item = wf_cbor_parse(v65535, 3);
        WF_CHECK(check_uint(item, 65535));
        wf_cbor_free(item);
    }
    {
        unsigned char v65536[] = {0x1A, 0x00, 0x01, 0x00, 0x00};
        wf_cbor_item *item = wf_cbor_parse(v65536, 5);
        WF_CHECK(check_uint(item, 65536));
        wf_cbor_free(item);
    }
    {
        unsigned char vU32max[] = {0x1A, 0xFF, 0xFF, 0xFF, 0xFF};
        wf_cbor_item *item = wf_cbor_parse(vU32max, 5);
        WF_CHECK(check_uint(item, 4294967295ULL));
        wf_cbor_free(item);
    }
    {
        unsigned char vU64big[] = {0x1B, 0x00, 0x00, 0x00, 0x01,
                                   0x00, 0x00, 0x00, 0x00};
        wf_cbor_item *item = wf_cbor_parse(vU64big, 9);
        WF_CHECK(check_uint(item, 4294967296ULL));
        wf_cbor_free(item);
    }

    /* ── negative integers ── */
    {
        unsigned char m1[] = {0x20};
        wf_cbor_item *item = wf_cbor_parse(m1, 1);
        WF_CHECK(check_nint(item, 0));
        wf_cbor_free(item);
    }
    {
        unsigned char m24[] = {0x37};
        wf_cbor_item *item = wf_cbor_parse(m24, 1);
        WF_CHECK(check_nint(item, 23));
        wf_cbor_free(item);
    }
    {
        unsigned char m25[] = {0x38, 0x18};
        wf_cbor_item *item = wf_cbor_parse(m25, 2);
        WF_CHECK(check_nint(item, 24));
        wf_cbor_free(item);
    }

    /* ── byte strings ── */
    {
        unsigned char empty[] = {0x40};
        wf_cbor_item *item = wf_cbor_parse(empty, 1);
        WF_CHECK(check_bytes(item, NULL, 0));
        wf_cbor_free(item);
    }
    {
        unsigned char three[] = {0x43, 0x01, 0x02, 0x03};
        wf_cbor_item *item = wf_cbor_parse(three, 4);
        unsigned char expect[] = {0x01, 0x02, 0x03};
        WF_CHECK(check_bytes(item, expect, 3));
        wf_cbor_free(item);
    }

    /* ── text strings ── */
    {
        unsigned char empty[] = {0x60};
        wf_cbor_item *item = wf_cbor_parse(empty, 1);
        WF_CHECK(check_string(item, ""));
        wf_cbor_free(item);
    }
    {
        unsigned char hello[] = {0x65, 'h', 'e', 'l', 'l', 'o'};
        wf_cbor_item *item = wf_cbor_parse(hello, 6);
        WF_CHECK(check_string(item, "hello"));
        wf_cbor_free(item);
    }

    /* ── arrays ── */
    {
        unsigned char empty[] = {0x80};
        wf_cbor_item *item = wf_cbor_parse(empty, 1);
        WF_CHECK(check_array_len(item, 0));
        wf_cbor_free(item);
    }
    {
        unsigned char three[] = {0x83, 0x01, 0x02, 0x03};
        wf_cbor_item *item = wf_cbor_parse(three, 4);
        WF_CHECK(check_array_len(item, 3));
        WF_CHECK(check_uint(item->children.items[0], 1));
        WF_CHECK(check_uint(item->children.items[1], 2));
        WF_CHECK(check_uint(item->children.items[2], 3));
        wf_cbor_free(item);
    }

    /* ── maps ── */
    {
        unsigned char empty[] = {0xA0};
        wf_cbor_item *item = wf_cbor_parse(empty, 1);
        WF_CHECK(check_map_len(item, 0));
        wf_cbor_free(item);
    }
    {
        /* {"test": "root"} — matches atproto test vector */
        unsigned char one[] = {0xA1, 0x64, 't', 'e', 's', 't',
                               0x64, 'r', 'o', 'o', 't'};
        wf_cbor_item *item = wf_cbor_parse(one, 11);
        WF_CHECK(check_map_len(item, 1));
        WF_CHECK(check_string(item->map.pairs[0].key, "test"));
        WF_CHECK(check_string(item->map.pairs[0].value, "root"));
        wf_cbor_free(item);
    }
    {
        /* {1: "a", 2: "b"} — sorted by key (already sorted) */
        unsigned char sorted[] = {0xA2, 0x01, 0x61, 0x61,
                                  0x02, 0x61, 0x62};
        wf_cbor_item *item = wf_cbor_parse(sorted, 7);
        WF_CHECK(check_map_len(item, 2));
        WF_CHECK(check_uint(item->map.pairs[0].key, 1));
        WF_CHECK(check_string(item->map.pairs[0].value, "a"));
        WF_CHECK(check_uint(item->map.pairs[1].key, 2));
        WF_CHECK(check_string(item->map.pairs[1].value, "b"));
        wf_cbor_free(item);
    }

    /* ── simple values ── */
    {
        unsigned char false_[] = {0xF4};
        wf_cbor_item *item = wf_cbor_parse(false_, 1);
        WF_CHECK(check_simple(item, 0));
        wf_cbor_free(item);
    }
    {
        unsigned char true_[] = {0xF5};
        wf_cbor_item *item = wf_cbor_parse(true_, 1);
        WF_CHECK(check_simple(item, 1));
        wf_cbor_free(item);
    }
    {
        unsigned char null_[] = {0xF6};
        wf_cbor_item *item = wf_cbor_parse(null_, 1);
        WF_CHECK(check_simple(item, 2));
        wf_cbor_free(item);
    }

    /* ── nested structures ── */
    {
        /* [1, [2, 3], {"a": 4}] */
        unsigned char nested[] = {0x83, 0x01,
                                  0x82, 0x02, 0x03,
                                  0xA1, 0x61, 0x61, 0x04};
        wf_cbor_item *item = wf_cbor_parse(nested, sizeof(nested));
        WF_CHECK(check_array_len(item, 3));
        WF_CHECK(check_uint(item->children.items[0], 1));
        WF_CHECK(check_array_len(item->children.items[1], 2));
        WF_CHECK(check_uint(item->children.items[1]->children.items[0], 2));
        WF_CHECK(check_uint(item->children.items[1]->children.items[1], 3));
        WF_CHECK(check_map_len(item->children.items[2], 1));
        WF_CHECK(check_string(item->children.items[2]->map.pairs[0].key, "a"));
        WF_CHECK(check_uint(item->children.items[2]->map.pairs[0].value, 4));
        wf_cbor_free(item);
    }

    /* ── error cases ── */

    /* NULL / empty input */
    WF_CHECK(wf_cbor_parse(NULL, 0) == NULL);
    WF_CHECK(wf_cbor_parse(NULL, 5) == NULL);
    WF_CHECK(wf_cbor_parse("", 0) == NULL);

    /* Non-canonical integer: 0 encoded as 0x18 0x00 */
    {
        unsigned char noncanon[] = {0x18, 0x00};
        WF_CHECK(wf_cbor_parse(noncanon, 2) == NULL);
    }

    /* Non-canonical: 256 encoded as 0x1A 0x00 0x00 0x01 0x00 */
    {
        unsigned char noncanon[] = {0x1A, 0x00, 0x00, 0x01, 0x00};
        WF_CHECK(wf_cbor_parse(noncanon, 5) == NULL);
    }

    /* Float (not allowed in DAG-CBOR) */
    {
        unsigned char float_[] = {0xFA, 0x3F, 0x80, 0x00, 0x00};
        WF_CHECK(wf_cbor_parse(float_, 5) == NULL);
    }

    /* Tag (not allowed in DAG-CBOR) */
    {
        unsigned char tag[] = {0xC1, 0x01};
        WF_CHECK(wf_cbor_parse(tag, 2) == NULL);
    }

    /* Undefined (simple value 23) */
    {
        unsigned char undef[] = {0xF7};
        WF_CHECK(wf_cbor_parse(undef, 1) == NULL);
    }

    /* Simple value 0 (not allowed in DAG-CBOR) */
    {
        unsigned char simple0[] = {0xE0};
        WF_CHECK(wf_cbor_parse(simple0, 1) == NULL);
    }

    /* Reserved additional info 28 */
    {
        unsigned char reserved[] = {0x1C};
        WF_CHECK(wf_cbor_parse(reserved, 1) == NULL);
    }

    /* Unsorted map keys: {2: "b", 1: "a"} */
    {
        unsigned char unsorted[] = {0xA2, 0x02, 0x61, 0x62,
                                    0x01, 0x61, 0x61};
        WF_CHECK(wf_cbor_parse(unsorted, 7) == NULL);
    }

    /* Trailing data after root item */
    {
        unsigned char trailing[] = {0x01, 0x02};
        WF_CHECK(wf_cbor_parse(trailing, 2) == NULL);
    }

    /* Truncated bytes */
    {
        unsigned char truncated[] = {0x43, 0x01, 0x02};
        WF_CHECK(wf_cbor_parse(truncated, 3) == NULL);
    }

    /* wf_cbor_free(NULL) must be safe */
    wf_cbor_free(NULL);

    /* ── CID computation ── */

    /* Test vector from bluesky-social/atproto car-file-fixtures.json.
     * Input: CBOR {"test": "root"} encoded as:
     *   a1 64 74 65 73 74 64 72 6f 6f 74
     * Expected CID string: bafyreiapldaco7m23c7qzc4w42r7kxmcswm64nkindtuh4vwztrpoe7m5m
     */
    {
        unsigned char cbor[] = {0xa1, 0x64, 't', 'e', 's', 't',
                                0x64, 'r', 'o', 'o', 't'};
        wf_cid cid = {{0}, 0};
        wf_status status = wf_cid_of_block(cbor, sizeof(cbor), &cid);
        WF_CHECK(status == WF_OK);
        WF_CHECK(cid.len == 36);
        WF_CHECK(cid.bytes[0] == 0x01);  /* CIDv1 */
        WF_CHECK(cid.bytes[1] == 0x71);  /* dag-cbor */

        char *str = wf_cid_to_string(&cid);
        WF_CHECK(str != NULL);
        WF_CHECK(strcmp(str, "bafyreiapldaco7m23c7qzc4w42r7kxmcswm64nkindtuh4vwztrpoe7m5m") == 0);
        free(str);
    }

    /* wf_cid_of_block rejects NULL/empty */
    {
        wf_cid cid = {{0}, 0};
        WF_CHECK(wf_cid_of_block(NULL, 5, &cid) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_cid_of_block((unsigned char*)"", 0, &cid) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_cid_of_block((unsigned char*)"\x01", 1, NULL) == WF_ERR_INVALID_ARG);
    }

    /* wf_cid_to_string rejects NULL / zeroed CID */
    WF_CHECK(wf_cid_to_string(NULL) == NULL);
    {
        wf_cid empty = {{0}, 0};
        WF_CHECK(wf_cid_to_string(&empty) == NULL);
    }

    /* ── CAR parse ── */

    /* Build a CAR for {"test": "root"} and verify round-trip */
    {
        unsigned char block_data[] = {0xa1, 0x64, 't', 'e', 's', 't',
                                      0x64, 'r', 'o', 'o', 't'};

        wf_cid block_cid = {{0}, 0};
        WF_CHECK(wf_cid_of_block(block_data, sizeof(block_data),
                                  &block_cid) == WF_OK);

        /* Build header CBOR: {"version": 1, "roots": [tag42(cid_bytes)]}
         * DAG-CBOR requires sorted map keys: "roots" < "version" */
        unsigned char hdr_cbor[256];
        size_t hp = 0;
        hdr_cbor[hp++] = 0xA2;                              /* map(2) */
        hdr_cbor[hp++] = 0x65;                               /* text(5) "roots" */
        memcpy(hdr_cbor + hp, "roots", 5); hp += 5;
        hdr_cbor[hp++] = 0x81;                               /* array(1) */
        hdr_cbor[hp++] = 0xD8; hdr_cbor[hp++] = 0x2A;       /* tag(42) */
        hdr_cbor[hp++] = 0x58; hdr_cbor[hp++] = 0x24;       /* bytes(36) */
        memcpy(hdr_cbor + hp, block_cid.bytes, 36); hp += 36;
        hdr_cbor[hp++] = 0x67;                               /* text(7) "version" */
        memcpy(hdr_cbor + hp, "version", 7); hp += 7;
        hdr_cbor[hp++] = 0x01;                               /* unsigned(1) */

        /* Full CAR: [varint hdr_len] [header] [varint blk_total] [cid] [data] */
        unsigned char car[512];
        size_t cp = 0;
        car[cp++] = (unsigned char)hp;                     /* varint */
        memcpy(car + cp, hdr_cbor, hp); cp += hp;

        size_t blk_total = 36 + sizeof(block_data);
        car[cp++] = (unsigned char)blk_total;              /* varint */
        memcpy(car + cp, block_cid.bytes, 36); cp += 36;
        memcpy(car + cp, block_data, sizeof(block_data)); cp += sizeof(block_data);

        wf_car parsed;
        memset(&parsed, 0, sizeof(parsed));
        WF_CHECK(wf_car_parse(car, cp, &parsed) == WF_OK);
        WF_CHECK(parsed.root_count == 1);
        WF_CHECK(memcmp(&parsed.roots[0], &block_cid, sizeof(wf_cid)) == 0);
        WF_CHECK(parsed.block_count == 1);
        WF_CHECK(memcmp(&parsed.blocks[0].cid, &block_cid, sizeof(wf_cid)) == 0);
        WF_CHECK(parsed.blocks[0].data_len == sizeof(block_data));
        WF_CHECK(memcmp(parsed.blocks[0].data, block_data,
                        sizeof(block_data)) == 0);
        wf_car_free(&parsed);
    }

    /* CAR error cases */
    WF_CHECK(wf_car_parse(NULL, 0, NULL) == WF_ERR_INVALID_ARG);
    {
        wf_car c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_car_parse((unsigned char*) "", 0, &c) == WF_ERR_INVALID_ARG);
    }

    /* Truncated header */
    {
        unsigned char junk[] = {0x10, 0x01, 0x02};
        wf_car c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_car_parse(junk, sizeof(junk), &c) == WF_ERR_INVALID_ARG);
    }

    /* wf_car_free(NULL) must be safe */
    wf_car_free(NULL);
    {
        wf_car zeroed;
        memset(&zeroed, 0, sizeof(zeroed));
        wf_car_free(&zeroed);
    }

    WF_TEST_SUMMARY();
}
