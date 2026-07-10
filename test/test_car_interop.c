/**
 * test_car_interop.c — validates wolfram's CAR codec and CID handling
 * against the official atproto reference vectors
 * (packages/repo/tests/car-file-fixtures.json, copied verbatim from
 * /Volumes/Storage/Developer/Local/atproto).
 *
 * Each fixture carries a full CAR (`car`, base64url), a root CID, and the
 * list of blocks ({cid, bytes}) where `bytes` is the raw DAG-CBOR content
 * (base64url). We parse the CAR and check the root, every block CID, the
 * re-encoded block bytes, and the recomputed DAG-CBOR CID.
 */

#include "wolfram/repo/car.h"
#include "wolfram/repo/cid.h"
#include "wolfram/crypto.h"

#include "test.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

/* Reference fixture block bytes are encoded with standard base64 (the
 * atproto reference uses 'base64', not base64url). The encoding is unpadded,
 * so pad it before decoding for a byte-exact comparison against the parsed
 * CAR block content. */
static unsigned char *base64_std_decode(const char *in, size_t *out_len) {
    static const int tbl[256] = {
        ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,  ['E'] = 4,  ['F'] = 5,
        ['G'] = 6,  ['H'] = 7,  ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11,
        ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15, ['Q'] = 16, ['R'] = 17,
        ['S'] = 18, ['T'] = 19, ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
        ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29,
        ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35,
        ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
        ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
        ['w'] = 48, ['x'] = 49, ['y'] = 50, ['z'] = 51, ['0'] = 52, ['1'] = 53,
        ['2'] = 54, ['3'] = 55, ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
        ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63,
    };
    size_t in_len = strlen(in);
    size_t pad = (4 - (in_len % 4)) % 4;
    size_t total = in_len + pad;
    char *padded = malloc(total + 1);
    unsigned char *out;
    size_t out_pos = 0, i;
    if (!padded) return NULL;
    memcpy(padded, in, in_len);
    for (i = 0; i < pad; i++) padded[in_len + i] = '=';
    padded[total] = '\0';

    out = malloc(total / 4 * 3 ? total / 4 * 3 : 1);
    if (!out) { free(padded); return NULL; }
    for (i = 0; i + 4 <= total; i += 4) {
        int b0 = tbl[(unsigned char)padded[i]];
        int b1 = tbl[(unsigned char)padded[i + 1]];
        int b2 = tbl[(unsigned char)padded[i + 2]];
        int b3 = tbl[(unsigned char)padded[i + 3]];
        out[out_pos++] = (unsigned char)((b0 << 2) | (b1 >> 4));
        if (padded[i + 2] != '=')
            out[out_pos++] = (unsigned char)((b1 << 4) | (b2 >> 2));
        if (padded[i + 3] != '=')
            out[out_pos++] = (unsigned char)((b2 << 6) | b3);
    }
    free(padded);
    if (out_len) *out_len = out_pos;
    return out;
}

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    long size_long;
    size_t size;
    char *buffer;
    if (len_out) *len_out = 0;
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    size_long = ftell(fp);
    if (size_long < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    size = (size_t)size_long;
    buffer = malloc(size + 1);
    if (!buffer) { fclose(fp); return NULL; }
    if (size > 0 && fread(buffer, 1, size, fp) != size) {
        free(buffer); fclose(fp); return NULL;
    }
    buffer[size] = '\0';
    fclose(fp);
    if (len_out) *len_out = size;
    return buffer;
}

int main(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/car-file-fixtures.json",
             WF_TEST_FIXTURE_DIR);
    char *json = read_entire_file(path, NULL);
    WF_CHECK(json != NULL);
    if (!json) WF_TEST_SUMMARY();

    cJSON *fixtures = cJSON_Parse(json);
    free(json);
    WF_CHECK(fixtures != NULL && cJSON_IsArray(fixtures));
    if (!fixtures || !cJSON_IsArray(fixtures)) WF_TEST_SUMMARY();

    int n = cJSON_GetArraySize(fixtures);
    for (int i = 0; i < n; i++) {
        cJSON *fx = cJSON_GetArrayItem(fixtures, i);
        cJSON *car_b64 = cJSON_GetObjectItemCaseSensitive(fx, "car");
        cJSON *root_cid = cJSON_GetObjectItemCaseSensitive(fx, "root");
        cJSON *blocks = cJSON_GetObjectItemCaseSensitive(fx, "blocks");
        if (!car_b64 || !root_cid || !blocks) { WF_CHECK(0); continue; }

        unsigned char *car_bytes = NULL;
        size_t car_len = 0;
        WF_CHECK(wf_crypto_base64url_decode(car_b64->valuestring, &car_bytes,
                                            &car_len) == WF_OK);
        if (!car_bytes) continue;

        wf_car car;
        WF_CHECK(wf_car_parse(car_bytes, car_len, &car) == WF_OK);
        free(car_bytes);
        if (car.block_count == 0) { wf_car_free(&car); continue; }

        wf_cid expected_root;
        WF_CHECK(wf_cid_from_string(root_cid->valuestring,
                                    &expected_root) == WF_OK);
        WF_CHECK(car.root_count == 1);
        WF_CHECK(cid_equal(&car.roots[0], &expected_root));

        int bn = cJSON_GetArraySize(blocks);
        WF_CHECK((int)car.block_count == bn);

        for (int j = 0; j < bn; j++) {
            cJSON *blk = cJSON_GetArrayItem(blocks, j);
            cJSON *exp_cid = cJSON_GetObjectItemCaseSensitive(blk, "cid");
            cJSON *exp_bytes =
                cJSON_GetObjectItemCaseSensitive(blk, "bytes");
            if (!exp_cid || !exp_bytes) { WF_CHECK(0); continue; }

            wf_cid ec;
            WF_CHECK(wf_cid_from_string(exp_cid->valuestring, &ec) == WF_OK);
            WF_CHECK(cid_equal(&car.blocks[j].cid, &ec));

            unsigned char *exp_data = NULL;
            size_t exp_len = 0;
            exp_data = base64_std_decode(exp_bytes->valuestring, &exp_len);
            WF_CHECK(exp_data != NULL);
            WF_CHECK(car.blocks[j].data_len == exp_len);
            WF_CHECK(exp_data && memcmp(car.blocks[j].data, exp_data,
                                        car.blocks[j].data_len) == 0);
            free(exp_data);

            wf_cid computed;
            WF_CHECK(wf_cid_of_block(car.blocks[j].data,
                                     car.blocks[j].data_len,
                                     &computed) == WF_OK);
            WF_CHECK(cid_equal(&computed, &ec));
        }
        wf_car_free(&car);
    }

    cJSON_Delete(fixtures);
    WF_TEST_SUMMARY();
}
