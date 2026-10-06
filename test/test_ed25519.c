/*
 * test_ed25519.c -- wf_ed25519_verify and wf_update_verify_signature against
 * test/vectors/ed25519.json (signatures made with a reference implementation;
 * the first two are RFC 8032 section 7.1 tests 1 and 2).
 */

#include "wolfram/ed25519.h"
#include "wolfram/update.h"

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

static size_t unhex(const char *h, unsigned char *out) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) {
        char two[3] = {h[i * 2], h[i * 2 + 1], 0};
        out[i] = (unsigned char)strtoul(two, NULL, 16);
    }
    return n;
}

static const char *str(const cJSON *o, const char *k) {
    return cJSON_GetObjectItemCaseSensitive(o, k)->valuestring;
}

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/ed25519.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    WF_CHECK(root != NULL);

    static unsigned char msg[8192];
    unsigned char pk[32], sig[64];
    const cJSON *v;
    int nvalid = 0, ninvalid = 0;

    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "valid")) {
        size_t n = unhex(str(v, "message_hex"), msg);
        WF_CHECK(unhex(str(v, "public"), pk) == 32);
        WF_CHECK(unhex(str(v, "signature"), sig) == 64);
        wf_status st = wf_ed25519_verify(sig, msg, n, pk);
        if (st != WF_OK) fprintf(stderr, "valid vector failed: %s\n", str(v, "note"));
        WF_CHECK(st == WF_OK);
        /* the same through the update wrapper, with and without a newline */
        char hex[160];
        snprintf(hex, sizeof hex, "%s\n", str(v, "signature"));
        WF_CHECK(wf_update_verify_signature(msg, n, hex, 129, pk) == WF_OK);
        WF_CHECK(wf_update_verify_signature(msg, n, hex, 128, pk) == WF_OK);
        nvalid++;
    }
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "invalid")) {
        size_t n = unhex(str(v, "message_hex"), msg);
        WF_CHECK(unhex(str(v, "public"), pk) == 32);
        WF_CHECK(unhex(str(v, "signature"), sig) == 64);
        wf_status st = wf_ed25519_verify(sig, msg, n, pk);
        if (st != WF_ERR_VALIDATION) fprintf(stderr, "invalid vector accepted: %s\n", str(v, "note"));
        WF_CHECK(st == WF_ERR_VALIDATION);
        ninvalid++;
    }
    WF_CHECK(nvalid >= 18);
    WF_CHECK(ninvalid >= 8);

    /* argument and format errors */
    {
        unsigned char zero32[32] = {0}, zero64[64] = {0};
        char hex[129];
        WF_CHECK(wf_ed25519_verify(NULL, "x", 1, zero32) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ed25519_verify(zero64, "x", 1, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ed25519_verify(zero64, NULL, 1, zero32) == WF_ERR_INVALID_ARG);
        memset(hex, '0', 128);
        hex[128] = '\0';
        WF_CHECK(wf_update_verify_signature("x", 1, hex, 127, zero32) == WF_ERR_PARSE);
        WF_CHECK(wf_update_verify_signature("x", 1, hex, 129, zero32) == WF_ERR_PARSE);
        hex[5] = 'g';
        WF_CHECK(wf_update_verify_signature("x", 1, hex, 128, zero32) == WF_ERR_PARSE);
        WF_CHECK(wf_update_verify_signature(NULL, 1, hex, 128, zero32) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_update_verify_signature("x", 1, "", 0, zero32) == WF_ERR_PARSE);
    }

    cJSON_Delete(root);
    free(text);
    WF_TEST_SUMMARY();
}
