/*
 * test_base64url_strict.c -- wf_crypto_base64url_decode_strict against
 * test/vectors/base64url_strict.json, and its difference from the lenient
 * decoder (which accepts what the strict one must refuse).
 */

#include "wolfram/crypto.h"

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

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/base64url_strict.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    WF_CHECK(root != NULL);
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "ok")) {
        const char *t =
            cJSON_GetObjectItemCaseSensitive(v, "text")->valuestring;
        unsigned char want[512];
        size_t wn =
            unhex(cJSON_GetObjectItemCaseSensitive(v, "bytes_hex")->valuestring,
                  want);
        unsigned char *got = NULL;
        size_t gn = 0;
        WF_CHECK(wf_crypto_base64url_decode_strict(t, strlen(t), &got, &gn) ==
                 WF_OK);
        WF_CHECK(gn == wn && got && memcmp(got, want, wn) == 0);
        free(got);
        /* the encoder's output is always accepted */
        char *enc = NULL;
        WF_CHECK(wf_crypto_base64url_encode(want, wn, &enc) == WF_OK);
        WF_CHECK(enc && strcmp(enc, t) == 0);
        free(enc);
    }
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "reject")) {
        unsigned char buf[16], *got = (unsigned char *)1;
        size_t gn = 99, len;
        const char *t = NULL;
        const cJSON *tx = cJSON_GetObjectItemCaseSensitive(v, "text");
        if (tx) {
            t = tx->valuestring;
            len = strlen(t);
        } else {
            len = unhex(
                cJSON_GetObjectItemCaseSensitive(v, "text_hex")->valuestring,
                buf);
            t = (const char *)buf;
        }
        int ok = wf_crypto_base64url_decode_strict(t, len, &got, &gn) ==
                     WF_ERR_PARSE &&
                 got == NULL && gn == 0;
        WF_CHECK(ok);
        if (!ok)
            fprintf(stderr, "  not rejected: %s\n",
                    cJSON_GetObjectItemCaseSensitive(v, "why")->valuestring);
    }
    /* The lenient decoder takes the standard alphabet and non-canonical bits;
     * that is exactly what the strict one exists to refuse. */
    unsigned char *o = NULL;
    size_t on = 0;
    WF_CHECK(wf_crypto_base64url_decode("Zm9v+A", &o, &on) == WF_OK);
    free(o);
    o = NULL;
    WF_CHECK(wf_crypto_base64url_decode_strict("Zm9v+A", 6, &o, &on) ==
             WF_ERR_PARSE);
    WF_CHECK(wf_crypto_base64url_decode_strict(NULL, 0, &o, &on) ==
             WF_ERR_INVALID_ARG);
    cJSON_Delete(root);
    free(text);
    WF_TEST_SUMMARY();
}
