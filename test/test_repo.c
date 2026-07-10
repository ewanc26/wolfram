/**
 * test_repo.c — unit tests for the DAG-CBOR decoder.
 *
 * Tests parsing, accessor, and error cases with hand-crafted and
 * known-good CBOR byte sequences. No external dependencies.
 */

#include "wolfram/repo.h"
#include "test.h"

#include <stdio.h>
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

static int check_link(const wf_cbor_item *item,
                      const unsigned char *expected, size_t len) {
    return item && item->type == WF_CBOR_LINK &&
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

static int check_cid(const wf_cid *left, const wf_cid *right) {
    return left && right && left->len == right->len && left->len > 0 &&
           memcmp(left->bytes, right->bytes, left->len) == 0;
}

/* Generate an 8-byte key whose MST layer equals `layer` (test helper). */
static int find_key_with_layer(unsigned layer, unsigned long long salt,
                               unsigned char *out) {
    unsigned long long s = salt ? salt : 1;
    for (int attempt = 0; attempt < 5000000; attempt++) {
        unsigned char k[8];
        memcpy(k, &s, 8);
        if (wf_mst_key_layer(k, 8) == layer) {
            memcpy(out, k, 8);
            return 1;
        }
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    return 0;
}

/* Count leaves visited during an MST walk. */
static wf_status mst_walk_count_cb(void *ctx, const unsigned char *key,
                                   size_t key_len, const wf_cid *value) {
    (void)key; (void)key_len; (void)value;
    size_t *c = ctx;
    (*c)++;
    return WF_OK;
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
        /* DAG-CBOR map keys must be strings. */
        unsigned char sorted[] = {0xA2, 0x01, 0x61, 0x61,
                                  0x02, 0x61, 0x62};
        WF_CHECK(wf_cbor_parse(sorted, 7) == NULL);
    }

    /* ── simple values ── */
    {
        unsigned char false_[] = {0xF4};
        wf_cbor_item *item = wf_cbor_parse(false_, 1);
        WF_CHECK(check_simple(item, 20));
        wf_cbor_free(item);
    }
    {
        unsigned char true_[] = {0xF5};
        wf_cbor_item *item = wf_cbor_parse(true_, 1);
        WF_CHECK(check_simple(item, 21));
        wf_cbor_free(item);
    }
    {
        unsigned char null_[] = {0xF6};
        wf_cbor_item *item = wf_cbor_parse(null_, 1);
        WF_CHECK(check_simple(item, 22));
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

    /* atproto's permissive decoder coerces undefined to null. */
    {
        unsigned char undef[] = {0xF7};
        wf_cbor_item *item = wf_cbor_parse(undef, 1);
        WF_CHECK(check_simple(item, 22));
        wf_cbor_free(item);
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

    /* Duplicate keys are invalid even when adjacent. */
    {
        unsigned char duplicate[] = {0xA2, 0x61, 'a', 0x01,
                                     0x61, 'a', 0x02};
        WF_CHECK(wf_cbor_parse(duplicate, sizeof(duplicate)) == NULL);
    }

    /* Invalid UTF-8 is forbidden in DAG-CBOR strings. */
    {
        unsigned char invalid_utf8[] = {0x61, 0xFF};
        WF_CHECK(wf_cbor_parse(invalid_utf8, sizeof(invalid_utf8)) == NULL);
    }

    /* CID links require tag 42 and the historical leading zero byte. */
    {
        unsigned char cid[36] = {0x01, 0x71, 0x12, 0x20};
        unsigned char link[41] = {0xD8, 0x2A, 0x58, 0x25, 0x00};
        memcpy(link + 5, cid, sizeof(cid));
        wf_cbor_item *item = wf_cbor_parse(link, sizeof(link));
        WF_CHECK(check_link(item, cid, sizeof(cid)));
        wf_cbor_free(item);

        link[4] = 0x01;
        WF_CHECK(wf_cbor_parse(link, sizeof(link)) == NULL);
    }

    /* Indefinite containers are not canonical DAG-CBOR. */
    {
        unsigned char indefinite[] = {0x9F, 0x01, 0xFF};
        WF_CHECK(wf_cbor_parse(indefinite, sizeof(indefinite)) == NULL);
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
        hdr_cbor[hp++] = 0x58; hdr_cbor[hp++] = 0x25;       /* bytes(37) */
        hdr_cbor[hp++] = 0x00;
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

    /* CAR write round-trip: parse → write → parse */
    {
        unsigned char block_data[] = {0xa1, 0x64, 't', 'e', 's', 't',
                                      0x64, 'r', 'o', 'o', 't'};
        wf_cid block_cid = {{0}, 0};
        WF_CHECK(wf_cid_of_block(block_data, sizeof(block_data),
                                  &block_cid) == WF_OK);

        unsigned char hdr[256]; size_t hp = 0;
        hdr[hp++] = 0xA2; hdr[hp++] = 0x65;
        memcpy(hdr + hp, "roots", 5); hp += 5;
        hdr[hp++] = 0x81;
        hdr[hp++] = 0xD8; hdr[hp++] = 0x2A;
        hdr[hp++] = 0x58; hdr[hp++] = 0x25;
        hdr[hp++] = 0x00;
        memcpy(hdr + hp, block_cid.bytes, 36); hp += 36;
        hdr[hp++] = 0x67;
        memcpy(hdr + hp, "version", 7); hp += 7;
        hdr[hp++] = 0x01;

        unsigned char car[512]; size_t cp = 0;
        car[cp++] = (unsigned char)hp;
        memcpy(car + cp, hdr, hp); cp += hp;
        car[cp++] = (unsigned char)(36 + sizeof(block_data));
        memcpy(car + cp, block_cid.bytes, 36); cp += 36;
        memcpy(car + cp, block_data, sizeof(block_data));
        cp += sizeof(block_data);

        wf_car parsed;
        memset(&parsed, 0, sizeof(parsed));
        WF_CHECK(wf_car_parse(car, cp, &parsed) == WF_OK);

        unsigned char *rewritten = NULL;
        size_t rewritten_len = 0;
        WF_CHECK(wf_car_write(&parsed, &rewritten, &rewritten_len) == WF_OK);
        WF_CHECK(rewritten_len == cp);
        WF_CHECK(memcmp(rewritten, car, cp) == 0);

        wf_car reparsed;
        memset(&reparsed, 0, sizeof(reparsed));
        WF_CHECK(wf_car_parse(rewritten, rewritten_len, &reparsed) == WF_OK);
        WF_CHECK(reparsed.root_count == parsed.root_count);
        WF_CHECK(reparsed.block_count == parsed.block_count);
        wf_car_free(&reparsed);
        free(rewritten);

        wf_car_free(&parsed);
    }

    /* wf_car_write invalid args */
    WF_CHECK(wf_car_write(NULL, NULL, NULL) == WF_ERR_INVALID_ARG);

    /* ── MST key layer ── */

    /* Known test vectors from atproto repo/mst */
    WF_CHECK(wf_mst_key_layer((unsigned char*)"2653ae71", 8) == 0);
    WF_CHECK(wf_mst_key_layer((unsigned char*)"asdf", 4) == 0);
    WF_CHECK(wf_mst_key_layer((unsigned char*)"blue", 4) == 1);
    WF_CHECK(wf_mst_key_layer((unsigned char*)"88bfafc7", 8) == 2);
    WF_CHECK(wf_mst_key_layer((unsigned char*)"2a92d355", 8) == 4);
    WF_CHECK(wf_mst_key_layer((unsigned char*)"884976f5", 8) == 6);
    WF_CHECK(wf_mst_key_layer((unsigned char*)"app.bsky.feed.post/454397e440ec", 31) == 4);
    WF_CHECK(wf_mst_key_layer((unsigned char*)"app.bsky.feed.post/9adeb165882c", 31) == 8);

    /* ── MST node parse ── */

    /* Empty node: {"l": null, "e": []} — DAG-CBOR (sorted keys: "e" < "l") */
    {
        unsigned char empty[] = {0xA2, 0x61, 0x65, 0x80, 0x61, 0x6C, 0xF6};
        wf_mst_node node;
        WF_CHECK(wf_mst_node_parse(empty, sizeof(empty), NULL, &node) == WF_OK);
        WF_CHECK(node.count == 0);
        WF_CHECK(node.left.len == 0);
        wf_mst_node_free(&node);
    }

    /* Node with 1 entry: full-key "test", value CID is all-zeros, no subtrees */
    {
        unsigned char cid_bytes[36] = {0};
        cid_bytes[0] = 0x01; cid_bytes[1] = 0x71;
        cid_bytes[2] = 0x12; cid_bytes[3] = 0x20;

        /* CBOR: {"e": [{"k": h'74657374', "p": 0, "v": tag42(cid), "t": null}], "l": null}
         * Sorted entry keys: "k" < "p" < "t" < "v" */
        unsigned char entry[200];
        size_t ep = 0;
        entry[ep++] = 0xA4;                              /* map(4) */
        entry[ep++] = 0x61; entry[ep++] = 0x6B;          /* text(1) "k" */
        entry[ep++] = 0x44;                               /* bytes(4) "test" */
        memcpy(entry + ep, "test", 4); ep += 4;
        entry[ep++] = 0x61; entry[ep++] = 0x70;          /* text(1) "p" */
        entry[ep++] = 0x00;                               /* unsigned(0) */
        entry[ep++] = 0x61; entry[ep++] = 0x74;          /* text(1) "t" */
        entry[ep++] = 0xF6;                               /* null */
        entry[ep++] = 0x61; entry[ep++] = 0x76;          /* text(1) "v" */
        entry[ep++] = 0xD8; entry[ep++] = 0x2A;          /* tag(42) */
        entry[ep++] = 0x58; entry[ep++] = 0x25;          /* bytes(37) */
        entry[ep++] = 0x00;
        memcpy(entry + ep, cid_bytes, 36); ep += 36;

        unsigned char node_cbor[300];
        size_t np = 0;
        node_cbor[np++] = 0xA2;                          /* map(2) */
        node_cbor[np++] = 0x61; node_cbor[np++] = 0x65;  /* text(1) "e" */
        node_cbor[np++] = 0x81;                           /* array(1) */
        memcpy(node_cbor + np, entry, ep); np += ep;
        node_cbor[np++] = 0x61; node_cbor[np++] = 0x6C;  /* text(1) "l" */
        node_cbor[np++] = 0xF6;                           /* null */

        wf_mst_node node;
        WF_CHECK(wf_mst_node_parse(node_cbor, np, NULL, &node) == WF_OK);
        WF_CHECK(node.count == 1);
        WF_CHECK(node.left.len == 0);
        WF_CHECK(node.entries[0].key_len == 4);
        WF_CHECK(memcmp(node.entries[0].key, "test", 4) == 0);
        WF_CHECK(node.entries[0].value.len == 36);
        WF_CHECK(memcmp(node.entries[0].value.bytes, cid_bytes, 36) == 0);
        WF_CHECK(node.entries[0].subtree.len == 0);
        wf_mst_node_free(&node);
    }

    /* ── Commit parse ── */

    {
        unsigned char cid_bytes[36];
        memset(cid_bytes, 0, 36);
        cid_bytes[0] = 0x01; cid_bytes[1] = 0x71;
        cid_bytes[2] = 0x12; cid_bytes[3] = 0x20;
        cid_bytes[4] = 0xAA;  /* distinguish data vs prev */

        /* Commit CBOR: {"data": tag42(cid), "did": "did:plc:test", "prev": tag42(other), "rev": "3jui7kd54zh2y", "version": 3}
         * Sorted by CBOR-encoded bytes: "did"(0x63) < "rev"(0x63) < "data"(0x64) < "prev"(0x64) < "version"(0x67) */
        unsigned char prev_cid[36];
        memcpy(prev_cid, cid_bytes, 36);
        prev_cid[4] = 0xBB;

        unsigned char commit_cbor[500];
        size_t cp2 = 0;
        commit_cbor[cp2++] = 0xA5;                       /* map(5) */

        commit_cbor[cp2++] = 0x63;                        /* text(3) "did" */
        memcpy(commit_cbor + cp2, "did", 3); cp2 += 3;
        commit_cbor[cp2++] = 0x6D;                        /* text(13) "did:plc:test" */
        memcpy(commit_cbor + cp2, "did:plc:test", 13); cp2 += 13;

        commit_cbor[cp2++] = 0x63;                        /* text(3) "rev" */
        memcpy(commit_cbor + cp2, "rev", 3); cp2 += 3;
        commit_cbor[cp2++] = 0x6D;                        /* text(13) "3jui7kd54zh2y" */
        memcpy(commit_cbor + cp2, "3jui7kd54zh2y", 13); cp2 += 13;

        commit_cbor[cp2++] = 0x64;                        /* text(4) "data" */
        memcpy(commit_cbor + cp2, "data", 4); cp2 += 4;
        commit_cbor[cp2++] = 0xD8; commit_cbor[cp2++] = 0x2A; /* tag(42) */
        commit_cbor[cp2++] = 0x58; commit_cbor[cp2++] = 0x25; /* bytes(37) */
        commit_cbor[cp2++] = 0x00;
        memcpy(commit_cbor + cp2, cid_bytes, 36); cp2 += 36;

        commit_cbor[cp2++] = 0x64;                        /* text(4) "prev" */
        memcpy(commit_cbor + cp2, "prev", 4); cp2 += 4;
        commit_cbor[cp2++] = 0xD8; commit_cbor[cp2++] = 0x2A;
        commit_cbor[cp2++] = 0x58; commit_cbor[cp2++] = 0x25;
        commit_cbor[cp2++] = 0x00;
        memcpy(commit_cbor + cp2, prev_cid, 36); cp2 += 36;

        commit_cbor[cp2++] = 0x67;                        /* text(7) "version" */
        memcpy(commit_cbor + cp2, "version", 7); cp2 += 7;
        commit_cbor[cp2++] = 0x03;                        /* unsigned(3) */

        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        WF_CHECK(wf_commit_parse(commit_cbor, cp2, &commit) == WF_OK);
        WF_CHECK(strcmp(commit.did, "did:plc:test") == 0);
        WF_CHECK(commit.version == 3);
        WF_CHECK(strcmp(commit.rev, "3jui7kd54zh2y") == 0);
        WF_CHECK(memcmp(commit.data.bytes, cid_bytes, 36) == 0);
        WF_CHECK(commit.has_prev == 1);
        WF_CHECK(memcmp(commit.prev.bytes, prev_cid, 36) == 0);
    }

    /* ── CAR block find ── */

    {
        wf_cid test_cid = {{0}, 0};
        test_cid.bytes[0] = 0x01; test_cid.bytes[1] = 0x71;
        test_cid.bytes[2] = 0x12; test_cid.bytes[3] = 0x20;
        test_cid.len = 36;

        wf_car car;
        car.root_count = 0;
        car.roots = NULL;
        car.block_count = 1;
        car.blocks = malloc(sizeof(wf_car_block));
        car.blocks[0].cid = test_cid;
        car.blocks[0].data = NULL;
        car.blocks[0].data_len = 0;

        wf_car_block *found = wf_car_find_block(&car, &test_cid);
        WF_CHECK(found != NULL);
        WF_CHECK(found->cid.len == 36);
        WF_CHECK(memcmp(found->cid.bytes, test_cid.bytes, 36) == 0);

        wf_cid missing = {{0}, 0};
        missing.bytes[0] = 0x01; missing.len = 1;
        WF_CHECK(wf_car_find_block(&car, &missing) == NULL);

        free(car.blocks);
        car.blocks = NULL;
    }

    /* ── MST find in minimal CAR ── */

    /* Build a CAR with a single MST node containing {"test": "root"} as the record.
     * The MST node: {"e": [{"k": h"test", "p": 0, "v": <record_cid>, "t": null}], "l": null}
     * The record: CBOR {"test": "root"}
     * Commit points to the MST root. */
    {
        unsigned char record_data[] = {0xa1, 0x64, 't', 'e', 's', 't',
                                        0x64, 'r', 'o', 'o', 't'};
        wf_cid record_cid = {{0}, 0};
        WF_CHECK(wf_cid_of_block(record_data, sizeof(record_data), &record_cid) == WF_OK);

        /* MST node CBOR (same as previous test) */
        unsigned char entry[200], ep = 0;
        entry[ep++] = 0xA4;
        entry[ep++] = 0x61; entry[ep++] = 0x6B;
        entry[ep++] = 0x44;
        memcpy(entry + ep, "test", 4); ep += 4;
        entry[ep++] = 0x61; entry[ep++] = 0x70;
        entry[ep++] = 0x00;
        entry[ep++] = 0x61; entry[ep++] = 0x74;
        entry[ep++] = 0xF6;
        entry[ep++] = 0x61; entry[ep++] = 0x76;
        entry[ep++] = 0xD8; entry[ep++] = 0x2A;
        entry[ep++] = 0x58; entry[ep++] = 0x25;
        entry[ep++] = 0x00;
        memcpy(entry + ep, record_cid.bytes, 36); ep += 36;

        unsigned char node_cbor[300], np = 0;
        node_cbor[np++] = 0xA2;
        node_cbor[np++] = 0x61; node_cbor[np++] = 0x65;
        node_cbor[np++] = 0x81;
        memcpy(node_cbor + np, entry, ep); np += ep;
        node_cbor[np++] = 0x61; node_cbor[np++] = 0x6C;
        node_cbor[np++] = 0xF6;

        wf_cid mst_cid = {{0}, 0};
        WF_CHECK(wf_cid_of_block(node_cbor, np, &mst_cid) == WF_OK);

        /* Build commit (sorted by CBOR-encoded bytes: "did", "rev", "data", "version") */
        unsigned char commit_cbor[500], cp2 = 0;
        commit_cbor[cp2++] = 0xA4;                           /* map(4) */

        commit_cbor[cp2++] = 0x63;                           /* text(3) "did" */
        memcpy(commit_cbor + cp2, "did", 3); cp2 += 3;
        commit_cbor[cp2++] = 0x6D;
        memcpy(commit_cbor + cp2, "did:plc:test", 13); cp2 += 13;

        commit_cbor[cp2++] = 0x63;                           /* text(3) "rev" */
        memcpy(commit_cbor + cp2, "rev", 3); cp2 += 3;
        commit_cbor[cp2++] = 0x6D;
        memcpy(commit_cbor + cp2, "3jui7kd54zh2y", 13); cp2 += 13;

        commit_cbor[cp2++] = 0x64;                           /* text(4) "data" */
        memcpy(commit_cbor + cp2, "data", 4); cp2 += 4;
        commit_cbor[cp2++] = 0xD8; commit_cbor[cp2++] = 0x2A;
        commit_cbor[cp2++] = 0x58; commit_cbor[cp2++] = 0x25;
        commit_cbor[cp2++] = 0x00;
        memcpy(commit_cbor + cp2, mst_cid.bytes, 36); cp2 += 36;

        commit_cbor[cp2++] = 0x67;                           /* text(7) "version" */
        memcpy(commit_cbor + cp2, "version", 7); cp2 += 7;
        commit_cbor[cp2++] = 0x03;

        wf_cid commit_cid = {{0}, 0};
        WF_CHECK(wf_cid_of_block(commit_cbor, cp2, &commit_cid) == WF_OK);

        /* Build CAR: header + 3 blocks (MST node, record, commit) */
        unsigned char hdr_cbor[256], hp = 0;
        hdr_cbor[hp++] = 0xA2;
        hdr_cbor[hp++] = 0x65; memcpy(hdr_cbor + hp, "roots", 5); hp += 5;
        hdr_cbor[hp++] = 0x81;
        hdr_cbor[hp++] = 0xD8; hdr_cbor[hp++] = 0x2A;
        hdr_cbor[hp++] = 0x58; hdr_cbor[hp++] = 0x25;
        hdr_cbor[hp++] = 0x00;
        memcpy(hdr_cbor + hp, commit_cid.bytes, 36); hp += 36;
        hdr_cbor[hp++] = 0x67; memcpy(hdr_cbor + hp, "version", 7); hp += 7;
        hdr_cbor[hp++] = 0x01;

        unsigned char car[1024];
        size_t cpos = 0;
        car[cpos++] = (unsigned char)hp;
        memcpy(car + cpos, hdr_cbor, hp); cpos += hp;

        /* Block 1: MST node */
        car[cpos++] = (unsigned char)(36 + np);
        memcpy(car + cpos, mst_cid.bytes, 36); cpos += 36;
        memcpy(car + cpos, node_cbor, np); cpos += np;

        /* Block 2: record */
        car[cpos++] = (unsigned char)(36 + sizeof(record_data));
        memcpy(car + cpos, record_cid.bytes, 36); cpos += 36;
        memcpy(car + cpos, record_data, sizeof(record_data)); cpos += sizeof(record_data);

        /* Block 3: commit */
        /* 36 + cp2 is 128 after the required CID link prefix. */
        car[cpos++] = 0x80;
        car[cpos++] = 0x01;
        memcpy(car + cpos, commit_cid.bytes, 36); cpos += 36;
        memcpy(car + cpos, commit_cbor, cp2); cpos += cp2;

        wf_car parsed;
        memset(&parsed, 0, sizeof(parsed));
        wf_status parsed_status = wf_car_parse(car, cpos, &parsed);
        WF_CHECK(parsed_status == WF_OK);
        if (parsed_status != WF_OK) return 1;

        /* Parse commit to find MST root */
        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        wf_car_block *commit_block = wf_car_find_block(&parsed, &commit_cid);
        WF_CHECK(commit_block != NULL);
        if (!commit_block) { wf_car_free(&parsed); return 1; }
        WF_CHECK(wf_commit_parse(commit_block->data, commit_block->data_len, &commit) == WF_OK);
        WF_CHECK(memcmp(&commit.data, &mst_cid, sizeof(wf_cid)) == 0);

        /* Find "test" in MST */
        wf_cid found;
        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&parsed, &commit.data, (unsigned char*)"test", 4, &found) == WF_OK);
        WF_CHECK(memcmp(&found, &record_cid, sizeof(wf_cid)) == 0);

        /* Find missing key */
        WF_CHECK(wf_mst_find(&parsed, &commit.data, (unsigned char*)"nope", 4, &found) == WF_ERR_NOT_FOUND);

        wf_car_free(&parsed);
    }

    /* ── MST mutation ── */

    /* Build and finalize an empty node, then parse it back */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid empty_left = {{0}, 0};
        wf_mst_node node;
        memset(&node, 0, sizeof(node));
        WF_CHECK(wf_mst_node_build(0, &empty_left, NULL, 0, &node) == WF_OK);
        WF_CHECK(wf_mst_node_finalize(&node, &car) == WF_OK);
        WF_CHECK(node.cid.len == 36);
        WF_CHECK(car.block_count == 1);

        wf_mst_node parsed;
        memset(&parsed, 0, sizeof(parsed));
        wf_car_block *block = wf_car_find_block(&car, &node.cid);
        WF_CHECK(block != NULL);
        WF_CHECK(wf_mst_node_parse(block->data, block->data_len,
                                    &node.cid, &parsed) == WF_OK);
        WF_CHECK(parsed.count == 0);
        WF_CHECK(parsed.left.len == 0);
        wf_mst_node_free(&parsed);
        wf_mst_node_free(&node);
        wf_car_free(&car);
    }

    /* Build and finalize a node with one entry, round-trip */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid value_cid = {{0}, 0};
        value_cid.bytes[0] = 0x01; value_cid.bytes[1] = 0x71;
        value_cid.bytes[2] = 0x12; value_cid.bytes[3] = 0x20;
        value_cid.len = 36;

        wf_mst_entry entry;
        memset(&entry, 0, sizeof(entry));
        unsigned char *k = malloc(4);
        memcpy(k, "test", 4);
        entry.key = k;
        entry.key_len = 4;
        entry.value = value_cid;

        wf_cid empty_left = {{0}, 0};
        wf_mst_node node;
        WF_CHECK(wf_mst_node_build(0, &empty_left, &entry, 1, &node) == WF_OK);

        /* entry.key is now NULL (transferred to node) */
        WF_CHECK(entry.key == NULL);

        WF_CHECK(wf_mst_node_finalize(&node, &car) == WF_OK);
        WF_CHECK(car.block_count == 1);

        wf_mst_node parsed;
        memset(&parsed, 0, sizeof(parsed));
        wf_car_block *block = wf_car_find_block(&car, &node.cid);
        WF_CHECK(block != NULL);
        WF_CHECK(wf_mst_node_parse(block->data, block->data_len,
                                    &node.cid, &parsed) == WF_OK);
        WF_CHECK(parsed.count == 1);
        WF_CHECK(parsed.entries[0].key_len == 4);
        WF_CHECK(memcmp(parsed.entries[0].key, "test", 4) == 0);
        WF_CHECK(memcmp(&parsed.entries[0].value, &value_cid, sizeof(wf_cid)) == 0);
        wf_mst_node_free(&parsed);
        wf_mst_node_free(&node);
        wf_car_free(&car);
    }

    /* Add a single key to an empty tree */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid value_cid = {{0}, 0};
        value_cid.bytes[0] = 0x01; value_cid.bytes[1] = 0x71;
        value_cid.bytes[2] = 0x12; value_cid.bytes[3] = 0x20;
        value_cid.len = 36;

        wf_cid root = {{0}, 0};
        wf_cid new_root;
        memset(&new_root, 0, sizeof(new_root));

        WF_CHECK(wf_mst_add(&car, &root,
                             (unsigned char*)"asdf", 4,
                             &value_cid, &new_root) == WF_OK);
        WF_CHECK(new_root.len == 36);

        /* Find it back */
        wf_cid found;
        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &new_root,
                              (unsigned char*)"asdf", 4,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &value_cid, sizeof(wf_cid)) == 0);

        /* Missing key */
        WF_CHECK(wf_mst_find(&car, &new_root,
                              (unsigned char*)"nope", 4,
                              &found) == WF_ERR_NOT_FOUND);

        wf_car_free(&car);
    }

    /* Add two keys at the same layer (layer 0) to an initially empty tree */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid val_a = {{0}, 0};
        val_a.bytes[0] = 0x01; val_a.bytes[1] = 0x71;
        val_a.bytes[2] = 0x12; val_a.bytes[3] = 0x20;
        val_a.bytes[4] = 0xAA;
        val_a.len = 36;

        wf_cid val_b = {{0}, 0};
        val_b.bytes[0] = 0x01; val_b.bytes[1] = 0x71;
        val_b.bytes[2] = 0x12; val_b.bytes[3] = 0x20;
        val_b.bytes[4] = 0xBB;
        val_b.len = 36;

        /* Known layer-0 keys from test vectors: "2653ae71" and "asdf" */
        wf_cid root = {{0}, 0};
        wf_cid root_a;
        memset(&root_a, 0, sizeof(root_a));
        WF_CHECK(wf_mst_add(&car, &root,
                             (unsigned char*)"asdf", 4,
                             &val_a, &root_a) == WF_OK);

        wf_cid root_b;
        memset(&root_b, 0, sizeof(root_b));
        WF_CHECK(wf_mst_add(&car, &root_a,
                             (unsigned char*)"2653ae71", 8,
                             &val_b, &root_b) == WF_OK);

        /* Both keys should be findable */
        wf_cid found;
        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &root_b,
                              (unsigned char*)"asdf", 4,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &val_a, sizeof(wf_cid)) == 0);

        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &root_b,
                              (unsigned char*)"2653ae71", 8,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &val_b, sizeof(wf_cid)) == 0);

        wf_car_free(&car);
    }

    /* Add key at higher layer (key_layer > node.layer): "blue" (layer 1) to "asdf" (layer 0) */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid val_asdf = {{0}, 0};
        val_asdf.bytes[0] = 0x01; val_asdf.bytes[1] = 0x71;
        val_asdf.bytes[2] = 0x12; val_asdf.bytes[3] = 0x20; val_asdf.bytes[4] = 0xAA;
        val_asdf.len = 36;

        wf_cid val_blue = {{0}, 0};
        val_blue.bytes[0] = 0x01; val_blue.bytes[1] = 0x71;
        val_blue.bytes[2] = 0x12; val_blue.bytes[3] = 0x20; val_blue.bytes[4] = 0xBB;
        val_blue.len = 36;

        WF_CHECK(wf_mst_key_layer((unsigned char*)"asdf", 4) == 0);
        WF_CHECK(wf_mst_key_layer((unsigned char*)"blue", 4) == 1);

        wf_cid root = {{0}, 0};
        wf_cid after_asdf;
        memset(&after_asdf, 0, sizeof(after_asdf));
        WF_CHECK(wf_mst_add(&car, &root,
                             (unsigned char*)"asdf", 4,
                             &val_asdf, &after_asdf) == WF_OK);

        /* Now add "blue" (layer 1) — triggers add_at_higher_layer */
        wf_cid after_blue;
        memset(&after_blue, 0, sizeof(after_blue));
        WF_CHECK(wf_mst_add(&car, &after_asdf,
                             (unsigned char*)"blue", 4,
                             &val_blue, &after_blue) == WF_OK);

        /* Both keys findable */
        wf_cid found;
        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &after_blue,
                              (unsigned char*)"asdf", 4,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &val_asdf, sizeof(wf_cid)) == 0);

        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &after_blue,
                              (unsigned char*)"blue", 4,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &val_blue, sizeof(wf_cid)) == 0);

        wf_car_free(&car);
    }

    /* Add key at lower layer (key_layer < node.layer): "asdf" (layer 0) to "blue" (layer 1) root */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid val_blue = {{0}, 0};
        val_blue.bytes[0] = 0x01; val_blue.bytes[1] = 0x71;
        val_blue.bytes[2] = 0x12; val_blue.bytes[3] = 0x20; val_blue.bytes[4] = 0xAA;
        val_blue.len = 36;

        wf_cid val_asdf = {{0}, 0};
        val_asdf.bytes[0] = 0x01; val_asdf.bytes[1] = 0x71;
        val_asdf.bytes[2] = 0x12; val_asdf.bytes[3] = 0x20; val_asdf.bytes[4] = 0xBB;
        val_asdf.len = 36;

        wf_cid root = {{0}, 0};
        wf_cid after_blue;
        memset(&after_blue, 0, sizeof(after_blue));
        WF_CHECK(wf_mst_add(&car, &root,
                             (unsigned char*)"blue", 4,
                             &val_blue, &after_blue) == WF_OK);

        /* Now add "asdf" (layer 0) — triggers add_at_lower_layer */
        wf_cid after_asdf;
        memset(&after_asdf, 0, sizeof(after_asdf));
        WF_CHECK(wf_mst_add(&car, &after_blue,
                             (unsigned char*)"asdf", 4,
                             &val_asdf, &after_asdf) == WF_OK);

        /* Both keys findable */
        wf_cid found;
        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &after_asdf,
                              (unsigned char*)"blue", 4,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &val_blue, sizeof(wf_cid)) == 0);

        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &after_asdf,
                              (unsigned char*)"asdf", 4,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &val_asdf, sizeof(wf_cid)) == 0);

        wf_car_free(&car);
    }

    /* ── MST delete ── */

    /* Delete from single-entry tree → tree becomes empty */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid val = {{0}, 0};
        val.bytes[0] = 0x01; val.bytes[1] = 0x71;
        val.bytes[2] = 0x12; val.bytes[3] = 0x20; val.bytes[4] = 0xAA;
        val.len = 36;

        wf_cid root = {{0}, 0};
        wf_cid after_add;
        memset(&after_add, 0, sizeof(after_add));
        WF_CHECK(wf_mst_add(&car, &root,
                             (unsigned char*)"asdf", 4,
                             &val, &after_add) == WF_OK);

        wf_cid after_del;
        memset(&after_del, 0, sizeof(after_del));
        WF_CHECK(wf_mst_delete(&car, &after_add,
                                (unsigned char*)"asdf", 4,
                                &after_del) == WF_OK);
        WF_CHECK(after_del.len == 0); /* tree is empty */

        wf_car_free(&car);
    }

    /* Delete a key, then verify it's gone and other keys remain */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid val_a = {{0}, 0};
        val_a.bytes[0] = 0x01; val_a.bytes[1] = 0x71;
        val_a.bytes[2] = 0x12; val_a.bytes[3] = 0x20; val_a.bytes[4] = 0xAA;
        val_a.len = 36;

        wf_cid val_b = {{0}, 0};
        val_b.bytes[0] = 0x01; val_b.bytes[1] = 0x71;
        val_b.bytes[2] = 0x12; val_b.bytes[3] = 0x20; val_b.bytes[4] = 0xBB;
        val_b.len = 36;

        wf_cid root = {{0}, 0};
        wf_cid after_a;
        memset(&after_a, 0, sizeof(after_a));
        WF_CHECK(wf_mst_add(&car, &root,
                             (unsigned char*)"asdf", 4,
                             &val_a, &after_a) == WF_OK);

        wf_cid after_both;
        memset(&after_both, 0, sizeof(after_both));
        WF_CHECK(wf_mst_add(&car, &after_a,
                             (unsigned char*)"2653ae71", 8,
                             &val_b, &after_both) == WF_OK);

        /* Delete "asdf" */
        wf_cid after_del;
        memset(&after_del, 0, sizeof(after_del));
        WF_CHECK(wf_mst_delete(&car, &after_both,
                                (unsigned char*)"asdf", 4,
                                &after_del) == WF_OK);
        WF_CHECK(after_del.len > 0); /* tree not empty */

        /* "asdf" should be gone */
        wf_cid found;
        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &after_del,
                              (unsigned char*)"asdf", 4,
                              &found) == WF_ERR_NOT_FOUND);

        /* "2653ae71" should remain */
        memset(&found, 0, sizeof(found));
        WF_CHECK(wf_mst_find(&car, &after_del,
                              (unsigned char*)"2653ae71", 8,
                              &found) == WF_OK);
        WF_CHECK(memcmp(&found, &val_b, sizeof(wf_cid)) == 0);

        wf_car_free(&car);
    }

    /* Delete a layer-3 leaf whose layer-1 neighbors both have non-empty
     * layer-0 boundary subtrees.  Layer-2 structural parents force recursive
     * appendMerge across three layers; compare against a canonical rebuild. */
    {
        static const unsigned wanted_layers[] = {0, 1, 0, 3, 0, 1, 0};
        char keys[7][32];
        size_t matched = 0;
        for (unsigned i = 0; i < 1000000 && matched < 7; i++) {
            char candidate[32];
            int n = snprintf(candidate, sizeof(candidate), "merge-%06u", i);
            unsigned layer = wf_mst_key_layer((unsigned char *)candidate,
                                               (size_t)n);
            if (layer == wanted_layers[matched]) {
                memcpy(keys[matched], candidate, (size_t)n + 1);
                matched++;
            } else if (layer == wanted_layers[0]) {
                memcpy(keys[0], candidate, (size_t)n + 1);
                matched = 1;
            } else {
                matched = 0;
            }
        }
        WF_CHECK(matched == 7);

        wf_cid values[7];
        memset(values, 0, sizeof(values));
        for (size_t i = 0; i < 7; i++) {
            values[i].bytes[0] = 0x01;
            values[i].bytes[1] = 0x71;
            values[i].bytes[2] = 0x12;
            values[i].bytes[3] = 0x20;
            values[i].bytes[4] = (unsigned char)(0xA0 + i);
            values[i].len = 36;
        }

        wf_car car;
        memset(&car, 0, sizeof(car));
        wf_cid root = {{0}, 0};
        for (size_t i = 0; i < 7; i++) {
            wf_cid next = {{0}, 0};
            WF_CHECK(wf_mst_add(&car, &root, (unsigned char *)keys[i],
                                strlen(keys[i]), &values[i], &next) == WF_OK);
            root = next;
        }

        wf_cid after_delete = {{0}, 0};
        WF_CHECK(wf_mst_delete(&car, &root, (unsigned char *)keys[3],
                               strlen(keys[3]), &after_delete) == WF_OK);

        wf_cid rebuilt = {{0}, 0};
        for (size_t i = 0; i < 7; i++) {
            if (i == 3) continue;
            wf_cid next = {{0}, 0};
            WF_CHECK(wf_mst_add(&car, &rebuilt, (unsigned char *)keys[i],
                                strlen(keys[i]), &values[i], &next) == WF_OK);
            rebuilt = next;
        }
        WF_CHECK(after_delete.len == rebuilt.len);
        WF_CHECK(memcmp(after_delete.bytes, rebuilt.bytes, rebuilt.len) == 0);

        wf_cid found = {{0}, 0};
        WF_CHECK(wf_mst_find(&car, &after_delete,
                             (unsigned char *)keys[3], strlen(keys[3]),
                             &found) == WF_ERR_NOT_FOUND);
        for (size_t i = 0; i < 7; i++) {
            if (i == 3) continue;
            memset(&found, 0, sizeof(found));
            WF_CHECK(wf_mst_find(&car, &after_delete,
                                 (unsigned char *)keys[i], strlen(keys[i]),
                                 &found) == WF_OK);
            WF_CHECK(memcmp(&found, &values[i], sizeof(found)) == 0);
        }
        wf_car_free(&car);
    }

    /* Delete non-existent key */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid val = {{0}, 0};
        val.bytes[0] = 0x01; val.bytes[1] = 0x71;
        val.bytes[2] = 0x12; val.bytes[3] = 0x20; val.bytes[4] = 0xAA;
        val.len = 36;

        wf_cid root = {{0}, 0};
        wf_cid after_add;
        memset(&after_add, 0, sizeof(after_add));
        WF_CHECK(wf_mst_add(&car, &root,
                             (unsigned char*)"asdf", 4,
                             &val, &after_add) == WF_OK);

        wf_cid after_del;
        memset(&after_del, 0, sizeof(after_del));
        WF_CHECK(wf_mst_delete(&car, &after_add,
                                (unsigned char*)"nope", 4,
                                &after_del) == WF_ERR_NOT_FOUND);

        wf_car_free(&car);
    }

    /* Delete from empty tree */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));
        wf_cid root = {{0}, 0};
        wf_cid result;
        memset(&result, 0, sizeof(result));
        WF_CHECK(wf_mst_delete(&car, &root,
                                (unsigned char*)"asdf", 4,
                                &result) == WF_ERR_NOT_FOUND);
        wf_car_free(&car);
    }

    /* Invalid args */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));
        wf_cid root = {{0}, 0};
        wf_cid result;
        WF_CHECK(wf_mst_delete(NULL, &root,
                                (unsigned char*)"asdf", 4,
                                &result) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_mst_delete(&car, NULL,
                                (unsigned char*)"asdf", 4,
                                &result) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_mst_delete(&car, &root,
                                NULL, 4,
                                &result) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_mst_delete(&car, &root,
                                (unsigned char*)"asdf", 0,
                                &result) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_mst_delete(&car, &root,
                                (unsigned char*)"asdf", 4,
                                NULL) == WF_ERR_INVALID_ARG);
        wf_car_free(&car);
    }

    /* ── MST traversal helpers ── */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        const char *keys[] = {
            "app.bsky.feed.post/aaa",
            "app.bsky.feed.post/bbb",
            "app.bsky.feed.post/ccc",
            "app.bsky.graph.follow/xxx",
            "app.bsky.graph.follow/yyy",
            "chat.bsky.convo/zzz"
        };
        size_t nkeys = sizeof(keys) / sizeof(keys[0]);

        wf_cid root = {{0}, 0};
        wf_cid cur = root;
        for (size_t i = 0; i < nkeys; i++) {
            /* Use a real CIDv1 (dag-cbor/sha2-256) as the leaf value so the
             * MST serializer emits a well-formed link. */
            char blob[8];
            snprintf(blob, sizeof(blob), "val%zu", i);
            wf_cid val = {{0}, 0};
            WF_CHECK(wf_cid_of_block((unsigned char *)blob, strlen(blob),
                                     &val) == WF_OK);
            wf_cid next;
            memset(&next, 0, sizeof(next));
            WF_CHECK(wf_mst_add(&car, &cur, (unsigned char *)keys[i],
                                strlen(keys[i]), &val, &next) == WF_OK);
            cur = next;
        }

        /* list: every leaf, in ascending key order */
        wf_mst_leaf *leaves = NULL;
        size_t n = 0;
        WF_CHECK(wf_mst_list(&car, &cur, &leaves, &n) == WF_OK);
        WF_CHECK(n == nkeys);
        for (size_t i = 1; i < n; i++) {
            size_t min = leaves[i - 1].key_len < leaves[i].key_len
                         ? leaves[i - 1].key_len : leaves[i].key_len;
            int cmp = memcmp(leaves[i - 1].key, leaves[i].key, min);
            if (cmp == 0)
                cmp = (int)leaves[i - 1].key_len - (int)leaves[i].key_len;
            WF_CHECK(cmp < 0);
        }
        /* value CIDs round-trip through list() */
        for (size_t i = 0; i < n; i++) {
            char blob[8];
            snprintf(blob, sizeof(blob), "val%zu", i);
            wf_cid expected = {{0}, 0};
            WF_CHECK(wf_cid_of_block((unsigned char *)blob, strlen(blob),
                                     &expected) == WF_OK);
            WF_CHECK(check_cid(&leaves[i].value, &expected));
        }
        wf_mst_leaf_list_free(leaves, n);

        /* paths: only the feed.post collection */
        wf_mst_leaf *feed = NULL;
        size_t nf = 0;
        const char *coll = "app.bsky.feed.post";
        WF_CHECK(wf_mst_paths(&car, &cur, (unsigned char *)coll, strlen(coll),
                              &feed, &nf) == WF_OK);
        WF_CHECK(nf == 3);
        for (size_t i = 0; i < nf; i++) {
            size_t clen = strlen(coll);
            WF_CHECK(feed[i].key_len > clen &&
                     feed[i].key[clen] == '/' &&
                     memcmp(feed[i].key, coll, clen) == 0);
        }
        wf_mst_leaf_list_free(feed, nf);

        /* walk_from: starts at "app.bsky.feed.post/bbb" (inclusive) */
        size_t walked = 0;
        WF_CHECK(wf_mst_walk_from(&car, &cur,
                                  (unsigned char *)"app.bsky.feed.post/bbb",
                                  strlen("app.bsky.feed.post/bbb"),
                                  mst_walk_count_cb, &walked) == WF_OK);
        WF_CHECK(walked == 5); /* bbb, ccc, graph x2, chat */

        /* walk_from: NULL lower bound walks everything */
        size_t all = 0;
        WF_CHECK(wf_mst_walk_from(&car, &cur, NULL, 0,
                                  mst_walk_count_cb, &all) == WF_OK);
        WF_CHECK(all == nkeys);

        /* get_all_cids: every value CID plus node CIDs */
        wf_cid *cids = NULL;
        size_t nc = 0;
        WF_CHECK(wf_mst_get_all_cids(&car, &cur, &cids, &nc) == WF_OK);
        WF_CHECK(nc > nkeys);
        for (size_t i = 0; i < nkeys; i++) {
            char blob[8];
            snprintf(blob, sizeof(blob), "val%zu", i);
            wf_cid expected = {{0}, 0};
            WF_CHECK(wf_cid_of_block((unsigned char *)blob, strlen(blob),
                                     &expected) == WF_OK);
            int found_val = 0;
            for (size_t j = 0; j < nc; j++)
                if (check_cid(&cids[j], &expected)) { found_val = 1; break; }
            WF_CHECK(found_val);
        }
        wf_mst_cid_list_free(cids, nc);

        /* get_covering_proof: feed.post collection range only */
        wf_cid *proof = NULL;
        size_t np = 0;
        WF_CHECK(wf_mst_get_covering_proof(&car, &cur,
                        (unsigned char *)"app.bsky.feed.post/",
                        strlen("app.bsky.feed.post/"),
                        (unsigned char *)"app.bsky.feed.post0",
                        strlen("app.bsky.feed.post0"),
                        &proof, &np) == WF_OK);
        WF_CHECK(np > 0);
        wf_mst_cid_list_free(proof, np);

        /* empty tree: traversal yields nothing without error */
        wf_cid empty_root = {{0}, 0};
        wf_mst_leaf *e = NULL; size_t en = 0;
        WF_CHECK(wf_mst_list(&car, &empty_root, &e, &en) == WF_OK);
        WF_CHECK(en == 0);
        wf_mst_leaf_list_free(e, en);
        size_t ew = 0;
        WF_CHECK(wf_mst_walk_from(&car, &empty_root, NULL, 0,
                                  mst_walk_count_cb, &ew) == WF_OK);
        WF_CHECK(ew == 0);

        wf_car_free(&car);
    }

    /* ── Signed commit creation ── */

    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));

        wf_status s = wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key);
        if (s == WF_OK) {
            wf_car car;
            memset(&car, 0, sizeof(car));

            wf_cid mst_root = {{0}, 0};
            mst_root.bytes[0] = 0x01; mst_root.bytes[1] = 0x71;
            mst_root.bytes[2] = 0x12; mst_root.bytes[3] = 0x20;
            mst_root.len = 36;

            wf_commit commit;
            memset(&commit, 0, sizeof(commit));
            WF_CHECK(wf_commit_create("did:plc:test", "3jui7kd54zh2y",
                                       &mst_root, NULL, &key,
                                       &car, &commit) == WF_OK);
            WF_CHECK(commit.cid.len == 36);
            WF_CHECK(commit.sig_len == 64);
            WF_CHECK(strcmp(commit.did, "did:plc:test") == 0);
            WF_CHECK(strcmp(commit.rev, "3jui7kd54zh2y") == 0);
            WF_CHECK(memcmp(&commit.data, &mst_root, sizeof(wf_cid)) == 0);
            WF_CHECK(commit.has_prev == 0);
            WF_CHECK(commit.version == 3);
            WF_CHECK(car.block_count == 1);

            /* Parse the block back */
            wf_commit parsed;
            memset(&parsed, 0, sizeof(parsed));
            wf_car_block *block = wf_car_find_block(&car, &commit.cid);
            WF_CHECK(block != NULL);
            WF_CHECK(wf_commit_parse(block->data, block->data_len,
                                      &parsed) == WF_OK);
            WF_CHECK(strcmp(parsed.did, "did:plc:test") == 0);
            WF_CHECK(strcmp(parsed.rev, "3jui7kd54zh2y") == 0);
            WF_CHECK(parsed.version == 3);

            wf_car_free(&car);
        }
        /* If keygen fails (no libsecp256k1), the test is skipped gracefully */
    }

    /* Commit with prev CID */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));

        wf_status s = wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key);
        if (s == WF_OK) {
            wf_car car;
            memset(&car, 0, sizeof(car));

            wf_cid mst_root = {{0}, 0};
            mst_root.bytes[0] = 0x01; mst_root.bytes[1] = 0x71;
            mst_root.bytes[2] = 0x12; mst_root.bytes[3] = 0x20;
            mst_root.len = 36;

            wf_cid prev_cid = {{0}, 0};
            prev_cid.bytes[0] = 0x01; prev_cid.bytes[1] = 0x71;
            prev_cid.bytes[2] = 0x12; prev_cid.bytes[3] = 0x20;
            prev_cid.bytes[4] = 0xAA;
            prev_cid.len = 36;

            wf_commit commit;
            memset(&commit, 0, sizeof(commit));
            WF_CHECK(wf_commit_create("did:plc:test", "3jui7kd54zh2y",
                                       &mst_root, &prev_cid, &key,
                                       &car, &commit) == WF_OK);
            WF_CHECK(commit.has_prev == 1);
            WF_CHECK(memcmp(&commit.prev, &prev_cid, sizeof(wf_cid)) == 0);

            wf_car_free(&car);
        }
    }

    /* ── Signed commit creation (P-256) ── */

    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));

        wf_status s = wf_signing_key_generate(WF_KEY_TYPE_P256, &key);
        WF_CHECK(s == WF_OK);
        WF_CHECK(key.type == WF_KEY_TYPE_P256);

        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid mst_root = {{0}, 0};
        mst_root.bytes[0] = 0x01; mst_root.bytes[1] = 0x71;
        mst_root.bytes[2] = 0x12; mst_root.bytes[3] = 0x20;
        mst_root.len = 36;

        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        WF_CHECK(wf_commit_create("did:plc:test", "3jui7kd54zh2y",
                                   &mst_root, NULL, &key,
                                   &car, &commit) == WF_OK);
        WF_CHECK(commit.cid.len == 36);
        WF_CHECK(commit.sig_len == 64);
        WF_CHECK(strcmp(commit.did, "did:plc:test") == 0);
        WF_CHECK(strcmp(commit.rev, "3jui7kd54zh2y") == 0);
        WF_CHECK(memcmp(&commit.data, &mst_root, sizeof(wf_cid)) == 0);
        WF_CHECK(commit.has_prev == 0);
        WF_CHECK(commit.version == 3);
        WF_CHECK(car.block_count == 1);

        /* Parse the block back */
        wf_commit parsed;
        memset(&parsed, 0, sizeof(parsed));
        wf_car_block *block = wf_car_find_block(&car, &commit.cid);
        WF_CHECK(block != NULL);
        WF_CHECK(wf_commit_parse(block->data, block->data_len,
                                  &parsed) == WF_OK);
        WF_CHECK(strcmp(parsed.did, "did:plc:test") == 0);
        WF_CHECK(strcmp(parsed.rev, "3jui7kd54zh2y") == 0);
        WF_CHECK(parsed.version == 3);

        wf_car_free(&car);
    }

    /* Commit with prev CID (P-256) */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));

        wf_status s = wf_signing_key_generate(WF_KEY_TYPE_P256, &key);
        WF_CHECK(s == WF_OK);

        wf_car car;
        memset(&car, 0, sizeof(car));

        wf_cid mst_root = {{0}, 0};
        mst_root.bytes[0] = 0x01; mst_root.bytes[1] = 0x71;
        mst_root.bytes[2] = 0x12; mst_root.bytes[3] = 0x20;
        mst_root.len = 36;

        wf_cid prev_cid = {{0}, 0};
        prev_cid.bytes[0] = 0x01; prev_cid.bytes[1] = 0x71;
        prev_cid.bytes[2] = 0x12; prev_cid.bytes[3] = 0x20;
        prev_cid.bytes[4] = 0xAA;
        prev_cid.len = 36;

        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        WF_CHECK(wf_commit_create("did:plc:test", "3jui7kd54zh2y",
                                   &mst_root, &prev_cid, &key,
                                   &car, &commit) == WF_OK);
        WF_CHECK(commit.has_prev == 1);
        WF_CHECK(memcmp(&commit.prev, &prev_cid, sizeof(wf_cid)) == 0);

        wf_car_free(&car);
    }

    /* Invalid args */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        wf_cid cid = {{0}, 0};
        wf_car car;
        memset(&car, 0, sizeof(car));
        wf_commit commit;

        WF_CHECK(wf_commit_create(NULL, "rev", &cid, NULL, &key,
                                   &car, &commit) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_commit_create("did", NULL, &cid, NULL, &key,
                                   &car, &commit) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_commit_create("did", "rev", NULL, NULL, &key,
                                   &car, &commit) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_commit_create("did", "rev", &cid, NULL, NULL,
                                   &car, &commit) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_commit_create("did", "rev", &cid, NULL, &key,
                                   NULL, &commit) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_commit_create("did", "rev", &cid, NULL, &key,
                                   &car, NULL) == WF_ERR_INVALID_ARG);
    }

    /* ── Record creation and retrieval ── */

    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        wf_status ks = wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key);
        WF_CHECK(ks == WF_OK);

        wf_car car;
        memset(&car, 0, sizeof(car));

        /* Create a record: {test: 123} -> CBOR */
        unsigned char record[] = {0xA1, 0x64, 't', 'e', 's', 't',
                                  0x18, 0x7B};
        wf_cid out_commit, out_record;
        memset(&out_commit, 0, sizeof(out_commit));
        memset(&out_record, 0, sizeof(out_record));

        WF_CHECK(wf_repo_create_record(&car, NULL, "did:plc:test",
                                        "com.example.posts",
                                        "3jui7kd54zh2y",
                                        record, sizeof(record),
                                        &key, &out_commit, &out_record) == WF_OK);
        WF_CHECK(out_commit.len == 36);
        WF_CHECK(out_record.len == 36);
        WF_CHECK(car.block_count >= 2);

        /* Get the record back */
        unsigned char *got_data = NULL;
        size_t got_len = 0;
        wf_cid got_cid;
        memset(&got_cid, 0, sizeof(got_cid));
        WF_CHECK(wf_repo_get_record(&car, &out_commit,
                                     "com.example.posts", "3jui7kd54zh2y",
                                     &got_data, &got_len, &got_cid) == WF_OK);
        WF_CHECK(got_len == sizeof(record));
        WF_CHECK(memcmp(got_data, record, sizeof(record)) == 0);
        WF_CHECK(memcmp(&got_cid, &out_record, sizeof(wf_cid)) == 0);
        free(got_data);

        wf_car_free(&car);
    }

    /* Record with previous commit */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key) == WF_OK);

        wf_car car;
        memset(&car, 0, sizeof(car));

        unsigned char record[] = {0xA1, 0x64, 't', 'e', 's', 't',
                                  0x18, 0x7B};
        wf_cid commit1, rec1;
        memset(&commit1, 0, sizeof(commit1));
        memset(&rec1, 0, sizeof(rec1));
        WF_CHECK(wf_repo_create_record(&car, NULL, "did:plc:test",
                                        "com.example.posts", "rec1",
                                        record, sizeof(record),
                                        &key, &commit1, &rec1) == WF_OK);

        unsigned char record2[] = {0xA1, 0x64, 'n', 'a', 'm', 'e',
                                   0x63, 'b', 'o', 'b'};
        wf_cid commit2, rec2;
        memset(&commit2, 0, sizeof(commit2));
        memset(&rec2, 0, sizeof(rec2));
        WF_CHECK(wf_repo_create_record(&car, &commit1, "did:plc:test",
                                        "com.example.posts", "rec2",
                                        record2, sizeof(record2),
                                        &key, &commit2, &rec2) == WF_OK);

        /* Both records retrievable */
        unsigned char *data = NULL; size_t dlen = 0; wf_cid c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_repo_get_record(&car, &commit2,
                                     "com.example.posts", "rec1",
                                     &data, &dlen, &c) == WF_OK);
        WF_CHECK(dlen == sizeof(record));
        WF_CHECK(memcmp(data, record, sizeof(record)) == 0);
        free(data); data = NULL;

        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_repo_get_record(&car, &commit2,
                                     "com.example.posts", "rec2",
                                     &data, &dlen, &c) == WF_OK);
        WF_CHECK(dlen == sizeof(record2));
        WF_CHECK(memcmp(data, record2, sizeof(record2)) == 0);
        free(data);

        wf_car_free(&car);
    }

    /* Record invalid args */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        wf_car car;
        memset(&car, 0, sizeof(car));

        WF_CHECK(wf_repo_create_record(NULL, NULL, NULL, NULL, NULL,
                                        NULL, 0, NULL, NULL, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_repo_get_record(NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL) == WF_ERR_INVALID_ARG);
    }

    /* ── Record update ── */

    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key) == WF_OK);
        wf_car car;
        memset(&car, 0, sizeof(car));
        unsigned char original[] = {0xA1, 0x61, 'v', 0x01};
        unsigned char replacement[] = {0xA1, 0x61, 'v', 0x02};
        wf_cid commit1, record1, commit2, record2;

        WF_CHECK(wf_repo_create_record(&car, NULL, "did:plc:test",
                                        "com.example.posts", "rec1",
                                        original, sizeof(original), &key,
                                        &commit1, &record1) == WF_OK);
        WF_CHECK(wf_repo_update_record(&car, &commit1, "did:plc:test",
                                        "com.example.posts", "rec1",
                                        replacement, sizeof(replacement), &key,
                                        &commit2, &record2) == WF_OK);
        WF_CHECK(memcmp(record1.bytes, record2.bytes, record1.len) != 0);

        unsigned char *data = NULL;
        size_t data_len = 0;
        wf_cid found;
        WF_CHECK(wf_repo_get_record(&car, &commit2, "com.example.posts",
                                     "rec1", &data, &data_len, &found) == WF_OK);
        WF_CHECK(data_len == sizeof(replacement));
        WF_CHECK(memcmp(data, replacement, sizeof(replacement)) == 0);
        WF_CHECK(found.len == record2.len &&
                 memcmp(found.bytes, record2.bytes, found.len) == 0);
        free(data);

        WF_CHECK(wf_repo_update_record(&car, &commit2, "did:plc:test",
                                        "com.example.posts", "missing",
                                        replacement, sizeof(replacement), &key,
                                        &commit1, &record1) == WF_ERR_NOT_FOUND);
        WF_CHECK(wf_repo_update_record(NULL, NULL, NULL, NULL, NULL,
                                        NULL, 0, NULL, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_car_free(&car);
    }

    /* ── Record deletion ── */

    /* Create a record, then delete it */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key) == WF_OK);

        wf_car car;
        memset(&car, 0, sizeof(car));

        unsigned char record[] = {0xA1, 0x64, 't', 'e', 's', 't',
                                  0x18, 0x7B};
        wf_cid commit1, rec1;
        memset(&commit1, 0, sizeof(commit1));
        memset(&rec1, 0, sizeof(rec1));
        WF_CHECK(wf_repo_create_record(&car, NULL, "did:plc:test",
                                        "com.example.posts", "rec1",
                                        record, sizeof(record),
                                        &key, &commit1, &rec1) == WF_OK);

        /* Delete the record */
        wf_cid commit2;
        memset(&commit2, 0, sizeof(commit2));
        WF_CHECK(wf_repo_delete_record(&car, &commit1, "did:plc:test",
                                         "com.example.posts", "rec1",
                                         &key, &commit2) == WF_OK);
        WF_CHECK(commit2.len == 36);

        /* Record should be gone */
        unsigned char *data = NULL; size_t dlen = 0; wf_cid c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_repo_get_record(&car, &commit2,
                                     "com.example.posts", "rec1",
                                     &data, &dlen, &c) == WF_ERR_NOT_FOUND);

        wf_car_free(&car);
    }

    /* Create two records, delete one, verify the other survives */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key) == WF_OK);

        wf_car car;
        memset(&car, 0, sizeof(car));

        unsigned char record_a[] = {0xA1, 0x64, 't', 'e', 's', 't',
                                    0x18, 0x7B};
        unsigned char record_b[] = {0xA1, 0x64, 'n', 'a', 'm', 'e',
                                    0x63, 'b', 'o', 'b'};

        wf_cid commit1, rec1;
        memset(&commit1, 0, sizeof(commit1));
        memset(&rec1, 0, sizeof(rec1));
        WF_CHECK(wf_repo_create_record(&car, NULL, "did:plc:test",
                                        "com.example.posts", "rec1",
                                        record_a, sizeof(record_a),
                                        &key, &commit1, &rec1) == WF_OK);

        wf_cid commit2, rec2;
        memset(&commit2, 0, sizeof(commit2));
        memset(&rec2, 0, sizeof(rec2));
        WF_CHECK(wf_repo_create_record(&car, &commit1, "did:plc:test",
                                        "com.example.posts", "rec2",
                                        record_b, sizeof(record_b),
                                        &key, &commit2, &rec2) == WF_OK);

        /* Delete rec1 */
        wf_cid commit3;
        memset(&commit3, 0, sizeof(commit3));
        WF_CHECK(wf_repo_delete_record(&car, &commit2, "did:plc:test",
                                         "com.example.posts", "rec1",
                                         &key, &commit3) == WF_OK);

        /* rec1 should be gone */
        unsigned char *data = NULL; size_t dlen = 0; wf_cid c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_repo_get_record(&car, &commit3,
                                     "com.example.posts", "rec1",
                                     &data, &dlen, &c) == WF_ERR_NOT_FOUND);

        /* rec2 should survive */
        memset(&c, 0, sizeof(c));
        data = NULL; dlen = 0;
        WF_CHECK(wf_repo_get_record(&car, &commit3,
                                     "com.example.posts", "rec2",
                                     &data, &dlen, &c) == WF_OK);
        WF_CHECK(dlen == sizeof(record_b));
        WF_CHECK(memcmp(data, record_b, sizeof(record_b)) == 0);
        free(data);

        wf_car_free(&car);
    }

    /* Delete non-existent record */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key) == WF_OK);

        wf_car car;
        memset(&car, 0, sizeof(car));

        unsigned char record[] = {0xA1, 0x64, 't', 'e', 's', 't',
                                  0x18, 0x7B};
        wf_cid commit1, rec1;
        memset(&commit1, 0, sizeof(commit1));
        memset(&rec1, 0, sizeof(rec1));
        WF_CHECK(wf_repo_create_record(&car, NULL, "did:plc:test",
                                        "com.example.posts", "rec1",
                                        record, sizeof(record),
                                        &key, &commit1, &rec1) == WF_OK);

        /* Try to delete a non-existent rkey */
        wf_cid commit2;
        memset(&commit2, 0, sizeof(commit2));
        WF_CHECK(wf_repo_delete_record(&car, &commit1, "did:plc:test",
                                         "com.example.posts", "nope",
                                         &key, &commit2) == WF_ERR_NOT_FOUND);

        wf_car_free(&car);
    }

    /* Delete invalid args */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        wf_car car;
        memset(&car, 0, sizeof(car));

        WF_CHECK(wf_repo_delete_record(NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_repo_delete_record(&car, NULL, "did", "col", "rkey",
                                        &key, NULL) == WF_ERR_INVALID_ARG);
    }

    /* Verify and import a repository CAR with a resolved P-256 did:key. */
    {
        wf_signing_key key = {0};
        key.type = WF_KEY_TYPE_P256;
        key.bytes[31] = 1;
        const char *did_key =
            "did:key:zDnaepsL7AXenJkVYdkh5KuKsSU7Ykh7kyXaLLU7auN9FWSiZ";
        const char *did = "did:plc:repoverifytest";
        unsigned char record[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x01};
        wf_car car = {0};
        wf_cid commit1 = {0}, record_cid = {0};
        WF_CHECK(wf_repo_create_record(&car, NULL, did,
                                        "com.example.posts", "one",
                                        record, sizeof(record), &key,
                                        &commit1, &record_cid) == WF_OK);
        car.roots = malloc(sizeof(wf_cid));
        WF_CHECK(car.roots != NULL);
        car.roots[0] = commit1;
        car.root_count = 1;

        wf_repo_verify_options options = {did, did_key, NULL};
        wf_commit verified = {0};
        WF_CHECK(wf_repo_verify(&car, &options, &verified) == WF_OK);
        WF_CHECK(strcmp(verified.did, did) == 0);
        WF_CHECK(verified.sig_len == 64);

        unsigned char *bytes = NULL;
        size_t bytes_len = 0;
        WF_CHECK(wf_car_write(&car, &bytes, &bytes_len) == WF_OK);
        wf_car imported = {0};
        wf_commit imported_commit = {0};
        WF_CHECK(wf_repo_import(bytes, bytes_len, &options, &imported,
                                &imported_commit) == WF_OK);
        WF_CHECK(imported.block_count == car.block_count);
        wf_car_free(&imported);

        options.expected_did = "did:plc:not-the-owner";
        WF_CHECK(wf_repo_verify(&car, &options, &verified) == WF_ERR_PARSE);
        options.expected_did = did;
        wf_cid wrong_prev = record_cid;
        options.expected_prev = &wrong_prev;
        WF_CHECK(wf_repo_verify(&car, &options, &verified) == WF_ERR_PARSE);
        options.expected_prev = NULL;

        /* A changed block whose old CID remains in the CAR must be rejected. */
        car.blocks[0].data[car.blocks[0].data_len - 1] ^= 1;
        WF_CHECK(wf_repo_verify(&car, &options, &verified) == WF_ERR_PARSE);
        car.blocks[0].data[car.blocks[0].data_len - 1] ^= 1;

        free(bytes);
        wf_car_free(&car);
    }

    /* Verify, apply, and invert a sparse incremental repository diff. */
    {
        wf_signing_key key = {0};
        key.type = WF_KEY_TYPE_P256;
        key.bytes[31] = 1;
        const char *did = "did:plc:repodifftest";
        const char *did_key =
            "did:key:zDnaepsL7AXenJkVYdkh5KuKsSU7Ykh7kyXaLLU7auN9FWSiZ";
        unsigned char record_one[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x01};
        unsigned char record_two[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x02};
        unsigned char record_three[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x03};
        unsigned char record_four[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x04};
        wf_car working = {0};
        wf_cid commit_one = {0}, commit_base = {0}, commit_update = {0};
        wf_cid old_keep = {0}, old_delete = {0};
        wf_cid new_keep = {0}, new_add = {0};
        WF_CHECK(wf_repo_create_record(&working, NULL, did,
                                        "com.example.posts", "keep",
                                        record_one, sizeof(record_one), &key,
                                        &commit_one, &old_keep) == WF_OK);
        WF_CHECK(wf_repo_create_record(&working, &commit_one, did,
                                        "com.example.posts", "remove",
                                        record_two, sizeof(record_two), &key,
                                        &commit_base, &old_delete) == WF_OK);
        working.roots = malloc(sizeof(wf_cid));
        WF_CHECK(working.roots != NULL);
        working.roots[0] = commit_base;
        working.root_count = 1;

        unsigned char *base_bytes = NULL;
        size_t base_len = 0;
        WF_CHECK(wf_car_write(&working, &base_bytes, &base_len) == WF_OK);
        wf_car base = {0};
        WF_CHECK(wf_car_parse(base_bytes, base_len, &base) == WF_OK);
        free(base_bytes);
        size_t first_update_block = working.block_count;

        wf_cid intermediate = {0};
        WF_CHECK(wf_repo_update_record(&working, &commit_base, did,
                                        "com.example.posts", "keep",
                                        record_three, sizeof(record_three),
                                        &key, &intermediate, &new_keep) == WF_OK);
        wf_cid after_delete = {0};
        WF_CHECK(wf_repo_delete_record(&working, &intermediate, did,
                                        "com.example.posts", "remove", &key,
                                        &after_delete) == WF_OK);
        WF_CHECK(wf_repo_create_record(&working, &after_delete, did,
                                        "com.example.posts", "added",
                                        record_four, sizeof(record_four), &key,
                                        &commit_update, &new_add) == WF_OK);

        wf_car update = {0};
        update.roots = &commit_update;
        update.root_count = 1;
        update.blocks = calloc(working.block_count - first_update_block,
                               sizeof(*update.blocks));
        WF_CHECK(update.blocks != NULL);
        for (size_t i = first_update_block; i < working.block_count; i++) {
            int duplicate = 0;
            for (size_t j = 0; j < update.block_count; j++) {
                if (check_cid(&working.blocks[i].cid, &update.blocks[j].cid)) {
                    duplicate = 1;
                    break;
                }
            }
            if (!duplicate) update.blocks[update.block_count++] = working.blocks[i];
        }
        wf_repo_verify_options options = {did, did_key, NULL};
        wf_repo_diff diff = {0};
        wf_status diff_status = wf_repo_diff_verify(&base, &commit_base,
                                                     &update, &options, &diff);
        WF_CHECK(diff_status == WF_OK);
        WF_CHECK(diff.operation_count == 3);
        WF_CHECK(check_cid(&diff.previous_commit, &commit_base));
        WF_CHECK(check_cid(&diff.commit.cid, &commit_update));
        WF_CHECK(diff.new_blocks.root_count == 1);
        WF_CHECK(diff.new_blocks.block_count > 0);
        WF_CHECK(diff.removed_count > 0);

        const wf_repo_operation *create = NULL, *change = NULL, *remove = NULL;
        for (size_t i = 0; i < diff.operation_count; i++) {
            const wf_repo_operation *operation = &diff.operations[i];
            WF_CHECK(strcmp(operation->collection, "com.example.posts") == 0);
            if (operation->action == WF_REPO_CREATE) create = operation;
            if (operation->action == WF_REPO_UPDATE) change = operation;
            if (operation->action == WF_REPO_DELETE) remove = operation;
        }
        WF_CHECK(create && strcmp(create->rkey, "added") == 0 &&
                 check_cid(&create->cid, &new_add));
        WF_CHECK(change && strcmp(change->rkey, "keep") == 0 &&
                 check_cid(&change->prev, &old_keep) &&
                 check_cid(&change->cid, &new_keep));
        WF_CHECK(remove && strcmp(remove->rkey, "remove") == 0 &&
                 check_cid(&remove->cid, &old_delete));

        wf_repo_operation *inverse = NULL;
        WF_CHECK(wf_repo_operations_invert(diff.operations,
                                            diff.operation_count,
                                            &inverse) == WF_OK);
        WF_CHECK(inverse != NULL);
        for (size_t i = 0; i < diff.operation_count; i++) {
            const wf_repo_operation *original =
                &diff.operations[diff.operation_count - i - 1];
            if (original->action == WF_REPO_CREATE)
                WF_CHECK(inverse[i].action == WF_REPO_DELETE);
            else if (original->action == WF_REPO_DELETE)
                WF_CHECK(inverse[i].action == WF_REPO_CREATE);
            else {
                WF_CHECK(inverse[i].action == WF_REPO_UPDATE);
                WF_CHECK(check_cid(&inverse[i].cid, &original->prev));
                WF_CHECK(check_cid(&inverse[i].prev, &original->cid));
            }
        }
        wf_repo_operations_free(inverse, diff.operation_count);

        /* The encoded prev field is optional transition metadata, not the
         * consumer base: this response spans three commits. */
        wf_repo_diff checked_prev = {0};
        options.expected_prev = &after_delete;
        WF_CHECK(wf_repo_diff_verify(&base, &commit_base, &update, &options,
                                      &checked_prev) == WF_OK);
        wf_repo_diff_free(&checked_prev);
        options.expected_prev = &commit_base;
        WF_CHECK(wf_repo_diff_verify(&base, &commit_base, &update, &options,
                                      &checked_prev) == WF_ERR_PARSE);
        options.expected_prev = NULL;

        /* Like consumer.ts ensureLeaves, changed leaves must be in update. */
        wf_car missing_leaf = update;
        missing_leaf.blocks = calloc(update.block_count,
                                     sizeof(*missing_leaf.blocks));
        WF_CHECK(missing_leaf.blocks != NULL);
        missing_leaf.block_count = 0;
        for (size_t i = 0; i < update.block_count; i++) {
            if (check_cid(&update.blocks[i].cid, &new_add)) continue;
            missing_leaf.blocks[missing_leaf.block_count++] = update.blocks[i];
        }
        wf_repo_diff rejected = {0};
        WF_CHECK(wf_repo_diff_verify(&base, &commit_base, &missing_leaf,
                                      &options, &rejected) == WF_ERR_NOT_FOUND);
        WF_CHECK(rejected.operations == NULL);
        free(missing_leaf.blocks);

        WF_CHECK(wf_repo_diff_apply(&base, &diff) == WF_OK);
        WF_CHECK(check_cid(&base.roots[0], &commit_update));
        unsigned char *found_data = NULL;
        size_t found_len = 0;
        wf_cid found_cid = {0};
        WF_CHECK(wf_repo_get_record(&base, &commit_update,
                                    "com.example.posts", "keep", &found_data,
                                    &found_len, &found_cid) == WF_OK);
        WF_CHECK(found_len == sizeof(record_three));
        WF_CHECK(memcmp(found_data, record_three, found_len) == 0);
        free(found_data);
        WF_CHECK(wf_repo_get_record(&base, &commit_update,
                                    "com.example.posts", "remove", &found_data,
                                    &found_len, &found_cid) == WF_ERR_NOT_FOUND);

        /* Applying twice is rejected without mutating the current root. */
        WF_CHECK(wf_repo_diff_apply(&base, &diff) == WF_ERR_INVALID_ARG);
        WF_CHECK(check_cid(&base.roots[0], &commit_update));

        wf_repo_diff_free(&diff);
        free(update.blocks);
        wf_car_free(&base);
        wf_car_free(&working);
    }

    /* Operation inversion validates ownership-bearing input. */
    {
        wf_repo_operation *output = (wf_repo_operation *)1;
        WF_CHECK(wf_repo_operations_invert(NULL, 0, &output) == WF_OK);
        WF_CHECK(output == NULL);
        WF_CHECK(wf_repo_operations_invert(NULL, 1, &output) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_repo_operations_invert(NULL, 0, NULL) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ── MST same-layer insert must split a preceding subtree (FIX 1) ── */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        /* Find keys with the layers needed to construct a gap subtree:
         *   P (layer 2), R (layer 2), Q (layer 1), with P < R < Q bytes.
         * Add P, then Q (lower layer -> lives in P's subtree), then R
         * (same layer, lands inside P's subtree range). R must split P's
         * subtree around R; without the fix Q becomes unreachable. */
        unsigned char pool1[80][8];
        unsigned char pool2[80][8];
        int n1 = 0, n2 = 0;
        unsigned long long salt = 1;
        while ((n1 < 80 || n2 < 80) && salt < 400000) {
            unsigned char k[8];
            memcpy(k, &salt, 8);
            unsigned L = wf_mst_key_layer(k, 8);
            if (L == 1 && n1 < 80) memcpy(pool1[n1++], k, 8);
            else if (L == 2 && n2 < 80) memcpy(pool2[n2++], k, 8);
            salt++;
        }
        WF_CHECK(n1 > 0 && n2 > 1);

        unsigned char P[8], R[8], Q[8];
        int found = 0;
        for (int i = 0; i < n2 && !found; i++)
            for (int j = 0; j < n2 && !found; j++)
                for (int k = 0; k < n1 && !found; k++)
                    if (memcmp(pool2[i], pool2[j], 8) < 0 &&
                        memcmp(pool2[j], pool1[k], 8) < 0) {
                        memcpy(P, pool2[i], 8);
                        memcpy(R, pool2[j], 8);
                        memcpy(Q, pool1[k], 8);
                        found = 1;
                    }
        WF_CHECK(found);

        wf_cid valP = {{0}, 0}, valR = {{0}, 0}, valQ = {{0}, 0};
        WF_CHECK(wf_cid_of_bytes(P, 8, &valP) == WF_OK);
        WF_CHECK(wf_cid_of_bytes(R, 8, &valR) == WF_OK);
        WF_CHECK(wf_cid_of_bytes(Q, 8, &valQ) == WF_OK);

        wf_cid root = {{0}, 0}, cur = root, next;
        memset(&next, 0, sizeof(next));
        WF_CHECK(wf_mst_add(&car, &cur, P, 8, &valP, &next) == WF_OK); cur = next;
        WF_CHECK(wf_mst_add(&car, &cur, Q, 8, &valQ, &next) == WF_OK); cur = next;
        WF_CHECK(wf_mst_add(&car, &cur, R, 8, &valR, &next) == WF_OK); cur = next;

        wf_cid f = {{0}, 0};
        WF_CHECK(wf_mst_find(&car, &cur, P, 8, &f) == WF_OK && check_cid(&f, &valP));
        WF_CHECK(wf_mst_find(&car, &cur, R, 8, &f) == WF_OK && check_cid(&f, &valR));
        /* The key nested inside P's (now split) subtree must remain reachable. */
        WF_CHECK(wf_mst_find(&car, &cur, Q, 8, &f) == WF_OK && check_cid(&f, &valQ));

        /* Internal consistency: every value CID is reachable via get_all_cids
         * and the full tree lists exactly the inserted leaves in order. */
        wf_mst_leaf *leaves = NULL;
        size_t nl = 0;
        WF_CHECK(wf_mst_list(&car, &cur, &leaves, &nl) == WF_OK);
        WF_CHECK(nl == 3);
        wf_cid *cids = NULL;
        size_t nc = 0;
        WF_CHECK(wf_mst_get_all_cids(&car, &cur, &cids, &nc) == WF_OK);
        WF_CHECK(nc >= 3);
        int sawP = 0, sawR = 0, sawQ = 0;
        for (size_t i = 0; i < nc; i++) {
            if (check_cid(&cids[i], &valP)) sawP = 1;
            if (check_cid(&cids[i], &valR)) sawR = 1;
            if (check_cid(&cids[i], &valQ)) sawQ = 1;
        }
        WF_CHECK(sawP && sawR && sawQ);
        wf_mst_leaf_list_free(leaves, nl);
        wf_mst_cid_list_free(cids, nc);
        wf_car_free(&car);
    }

    /* Multiple same-layer-into-subtree-owning-gap inserts (FIX 1 at scale).
     * Build layer-2 anchors, then in each gap insert a layer-1 key (lower
     * layer -> becomes a subtree) followed by a layer-2 key (same layer ->
     * must split that subtree around it). Only the lower-layer and same-layer
     * paths are exercised, so this isolates the FIX 1 split logic. */
    {
        wf_car car;
        memset(&car, 0, sizeof(car));

        unsigned char p1[200][8];
        unsigned char p2[200][8];
        int n1 = 0, n2 = 0;
        unsigned long long salt = 1;
        while ((n1 < 200 || n2 < 200) && salt < 800000) {
            unsigned char k[8];
            memcpy(k, &salt, 8);
            unsigned L = wf_mst_key_layer(k, 8);
            if (L == 1 && n1 < 200) memcpy(p1[n1++], k, 8);
            else if (L == 2 && n2 < 200) memcpy(p2[n2++], k, 8);
            salt++;
        }
        WF_CHECK(n1 > 0 && n2 > 3);

        /* 4 sorted layer-2 anchors */
        unsigned char anchors[4][8];
        int ai = 0;
        for (int i = 0; i < n2 && ai < 4; i++) {
            int dup = 0;
            for (int j = 0; j < ai; j++)
                if (memcmp(anchors[j], p2[i], 8) == 0) dup = 1;
            if (!dup) memcpy(anchors[ai++], p2[i], 8);
        }
        /* keep anchors sorted */
        for (int a = 0; a < 3; a++)
            for (int b = a + 1; b < 4; b++)
                if (memcmp(anchors[a], anchors[b], 8) > 0) {
                    unsigned char t[8]; memcpy(t, anchors[a], 8);
                    memcpy(anchors[a], anchors[b], 8);
                    memcpy(anchors[b], t, 8);
                }
        WF_CHECK(memcmp(anchors[0], anchors[1], 8) < 0 &&
                 memcmp(anchors[1], anchors[2], 8) < 0 &&
                 memcmp(anchors[2], anchors[3], 8) < 0);

        /* For each of the 3 gaps, pick a layer-1 key (q) and a layer-2 key
         * (r) with anchors[i] < q < r < anchors[i+1]. */
        unsigned char q[3][8], r[3][8];
        int gaps = 0;
        for (int g = 0; g < 3; g++) {
            int got = 0;
            for (int i = 0; i < n1 && !got; i++) {
                if (memcmp(anchors[g], p1[i], 8) >= 0) continue;
                for (int j = 0; j < n2 && !got; j++) {
                    if (memcmp(p1[i], p2[j], 8) < 0 &&
                        memcmp(p2[j], anchors[g + 1], 8) < 0) {
                        memcpy(q[g], p1[i], 8);
                        memcpy(r[g], p2[j], 8);
                        got = 1;
                    }
                }
            }
            if (got) gaps++;
        }
        WF_CHECK(gaps == 3);

        /* total keys: 4 anchors + 3 q + 3 r = 10 */
        unsigned char allk[10][8];
        wf_cid allv[10];
        int n = 0;
        for (int i = 0; i < 4; i++) { memcpy(allk[n], anchors[i], 8); n++; }
        for (int i = 0; i < 3; i++) { memcpy(allk[n], q[i], 8); n++; }
        for (int i = 0; i < 3; i++) { memcpy(allk[n], r[i], 8); n++; }
        for (int i = 0; i < n; i++)
            WF_CHECK(wf_cid_of_bytes(allk[i], 8, &allv[i]) == WF_OK);

        wf_cid root = {{0}, 0}, cur = root, next;
        memset(&next, 0, sizeof(next));
        /* anchors first (same layer), then each gap's (q, r) */
        for (int i = 0; i < 4; i++) {
            WF_CHECK(wf_mst_add(&car, &cur, anchors[i], 8, &allv[i],
                                &next) == WF_OK);
            cur = next;
        }
        for (int g = 0; g < 3; g++) {
            WF_CHECK(wf_mst_add(&car, &cur, q[g], 8,
                                &allv[4 + g], &next) == WF_OK);
            cur = next;
            WF_CHECK(wf_mst_add(&car, &cur, r[g], 8,
                                &allv[7 + g], &next) == WF_OK);
            cur = next;
        }

        for (int i = 0; i < n; i++) {
            wf_cid f = {{0}, 0};
            WF_CHECK(wf_mst_find(&car, &cur, allk[i], 8, &f) == WF_OK &&
                    check_cid(&f, &allv[i]));
        }

        wf_mst_leaf *leaves = NULL;
        size_t nl = 0;
        WF_CHECK(wf_mst_list(&car, &cur, &leaves, &nl) == WF_OK);
        WF_CHECK(nl == (size_t)n);
        for (size_t i = 1; i < nl; i++) {
            size_t min = leaves[i - 1].key_len < leaves[i].key_len
                         ? leaves[i - 1].key_len : leaves[i].key_len;
            int cmp = memcmp(leaves[i - 1].key, leaves[i].key, min);
            if (cmp == 0)
                cmp = (int)leaves[i - 1].key_len - (int)leaves[i].key_len;
            WF_CHECK(cmp < 0);
        }
        wf_mst_leaf_list_free(leaves, nl);

        wf_cid *cids = NULL;
        size_t nc = 0;
        WF_CHECK(wf_mst_get_all_cids(&car, &cur, &cids, &nc) == WF_OK);
        WF_CHECK(nc >= (size_t)n);
        for (int i = 0; i < n; i++) {
            int saw = 0;
            for (size_t j = 0; j < nc; j++)
                if (check_cid(&cids[j], &allv[i])) { saw = 1; break; }
            WF_CHECK(saw);
        }
        wf_mst_cid_list_free(cids, nc);
        wf_car_free(&car);
    }

    WF_TEST_SUMMARY();
}
