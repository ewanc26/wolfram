/*
 * test_qr.c -- wolfram/qr.h against test/vectors/qr.json. The matrices there
 * were decoded back to their text by an independent decoder (zxing-cpp) when
 * generated, so matching them means the encoder is correct, not just stable.
 * Also checks the structure ISO 18004 requires of every code.
 */

#include "wolfram/qr.h"

#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_VECTOR_DIR
#define WF_TEST_VECTOR_DIR "test/vectors"
#endif

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n)
        b[n] = '\0';
    else {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

#define AT(m, n, x, y) ((m)[(y) * (n) + (x)])

/* The finder pattern with its separator at (ox, oy) (the 7x7 ring plus 3x3
 * core), and the timing patterns, alternate dark/light from a dark corner. */
static int structure_ok(const uint8_t *m, int n) {
    const int origin[3][2] = {{0, 0}, {n - 7, 0}, {0, n - 7}};
    for (int f = 0; f < 3; f++)
        for (int dy = 0; dy < 7; dy++)
            for (int dx = 0; dx < 7; dx++) {
                int ring = dx == 0 || dx == 6 || dy == 0 || dy == 6;
                int core = dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
                if (AT(m, n, origin[f][0] + dx, origin[f][1] + dy) !=
                    (ring || core))
                    return 0;
            }
    for (int i = 8; i < n - 8; i++)
        if (AT(m, n, i, 6) != (i % 2 == 0) || AT(m, n, 6, i) != (i % 2 == 0))
            return 0;
    return AT(m, n, 8, n - 8) == 1; /* the always-dark module */
}

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/qr.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    WF_CHECK(root != NULL);
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "vectors")) {
        const char *t =
            cJSON_GetObjectItemCaseSensitive(v, "text")->valuestring;
        const char *e = cJSON_GetObjectItemCaseSensitive(v, "ecc")->valuestring;
        wf_qr_ecc ecc = e[0] == 'L'   ? WF_QR_ECC_L
                        : e[0] == 'M' ? WF_QR_ECC_M
                        : e[0] == 'Q' ? WF_QR_ECC_Q
                                      : WF_QR_ECC_H;
        int want_n =
            (int)cJSON_GetObjectItemCaseSensitive(v, "size")->valuedouble;
        uint8_t *m = NULL;
        int n = 0;
        WF_CHECK(wf_qr_encode(t, ecc, &m, &n) == WF_OK);
        WF_CHECK(n == want_n && m != NULL);
        if (!m || n != want_n) continue;
        const cJSON *rows = cJSON_GetObjectItemCaseSensitive(v, "rows");
        int same = 1;
        for (int y = 0; y < n; y++) {
            const char *row = cJSON_GetArrayItem(rows, y)->valuestring;
            for (int x = 0; x < n; x++)
                if (AT(m, n, x, y) != row[x] - '0') same = 0;
        }
        WF_CHECK(same);
        if (!same)
            fprintf(stderr, "  matrix differs for '%.30s' level %s\n", t, e);
        WF_CHECK(structure_ok(m, n));
        free(m);
    }

    /* The smallest version that fits is chosen, per level (byte mode capacity:
     * v1 L 17 bytes, v1 M 14, v1 Q 11, v1 H 7). */
    struct {
        wf_qr_ecc ecc;
        size_t len;
        int version;
    } caps[] = {
        {WF_QR_ECC_L, 17, 1}, {WF_QR_ECC_L, 18, 2}, {WF_QR_ECC_M, 14, 1},
        {WF_QR_ECC_M, 15, 2}, {WF_QR_ECC_Q, 11, 1}, {WF_QR_ECC_Q, 12, 2},
        {WF_QR_ECC_H, 7, 1},  {WF_QR_ECC_H, 8, 2},  {WF_QR_ECC_L, 271, 10},
    };
    char big[400];
    memset(big, 'x', sizeof big);
    for (size_t i = 0; i < sizeof caps / sizeof caps[0]; i++) {
        uint8_t *m = NULL;
        int n = 0;
        WF_CHECK(wf_qr_encode_bytes(big, caps[i].len, caps[i].ecc, &m, &n) ==
                 WF_OK);
        WF_CHECK(n == 4 * caps[i].version + 17);
        if (m) WF_CHECK(structure_ok(m, n));
        free(m);
    }
    uint8_t *m = (uint8_t *)1;
    int n = 99;
    WF_CHECK(wf_qr_encode_bytes(big, 272, WF_QR_ECC_L, &m, &n) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(m == NULL && n == 0);
    WF_CHECK(wf_qr_encode(NULL, WF_QR_ECC_L, &m, &n) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_qr_encode("x", (wf_qr_ecc)7, &m, &n) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_qr_encode("x", WF_QR_ECC_L, NULL, &n) == WF_ERR_INVALID_ARG);
    /* Same input, same matrix. */
    uint8_t *a = NULL, *b = NULL;
    int na, nb;
    WF_CHECK(wf_qr_encode("https://example.com/", WF_QR_ECC_M, &a, &na) ==
             WF_OK);
    WF_CHECK(wf_qr_encode("https://example.com/", WF_QR_ECC_M, &b, &nb) ==
             WF_OK);
    WF_CHECK(na == nb && a && b && memcmp(a, b, (size_t)na * (size_t)na) == 0);
    free(a);
    free(b);
    cJSON_Delete(root);
    free(text);
    WF_TEST_SUMMARY();
}
