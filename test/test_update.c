/*
 * test_update.c -- wolfram/update.h against the shared vectors in
 * test/vectors/update/: version comparison, SHA-256 known answers, manifest
 * parsing and download verification.
 */

#include "wolfram/update.h"

#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_VECTOR_DIR
#define WF_TEST_VECTOR_DIR "test/vectors"
#endif

static cJSON *load(const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/update/%s", WF_TEST_VECTOR_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    cJSON *j = NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = '\0';
        j = cJSON_Parse(buf);
    }
    free(buf);
    fclose(f);
    return j;
}

static const char *vstr(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void hex_of(const unsigned char *d, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hx[d[i] >> 4];
        out[i * 2 + 1] = hx[d[i] & 15];
    }
    out[64] = '\0';
}

static void test_versions(void) {
    cJSON *root = load("versions.json");
    WF_CHECK(root != NULL);
    if (!root) return;
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "compare")) {
        int err = 99;
        int c = wf_update_compare_versions(vstr(v, "a"), vstr(v, "b"), &err);
        int want =
            (int)cJSON_GetObjectItemCaseSensitive(v, "sign")->valuedouble;
        int ok = err == 0 && ((c > 0) - (c < 0)) == want;
        WF_CHECK(ok);
        if (!ok)
            fprintf(stderr, "  compare %s %s\n", vstr(v, "a"), vstr(v, "b"));
        /* Antisymmetry. */
        c = wf_update_compare_versions(vstr(v, "b"), vstr(v, "a"), &err);
        WF_CHECK(err == 0 && ((c > 0) - (c < 0)) == -want);
    }
    const cJSON *s;
    cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(root, "invalid")) {
        int err = 0;
        wf_update_compare_versions(s->valuestring, "1.0.0", &err);
        WF_CHECK(err != 0);
        err = 0;
        wf_update_compare_versions("1.0.0", s->valuestring, &err);
        WF_CHECK(err != 0);
        WF_CHECK(!wf_update_version_valid(s->valuestring));
        if (!err) fprintf(stderr, "  not rejected: '%s'\n", s->valuestring);
    }
    cJSON_Delete(root);
}

static void test_sha256(void) {
    cJSON *root = load("sha256.json");
    WF_CHECK(root != NULL);
    if (!root) return;
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "vectors")) {
        unsigned char d[32];
        char hex[65];
        const cJSON *rep = cJSON_GetObjectItemCaseSensitive(v, "repeat");
        wf_sha256 c;
        wf_sha256_init(&c);
        if (rep) {
            unsigned char byte =
                (unsigned char)strtoul(vstr(rep, "byte_hex"), NULL, 16);
            long n = (long)cJSON_GetObjectItemCaseSensitive(rep, "count")
                         ->valuedouble;
            unsigned char chunk[997];
            memset(chunk, byte, sizeof chunk);
            while (n > 0) { /* odd chunk size: exercises buffering */
                size_t k = n < (long)sizeof chunk ? (size_t)n : sizeof chunk;
                wf_sha256_update(&c, chunk, k);
                n -= (long)k;
            }
        } else {
            const char *h = vstr(v, "input_hex");
            size_t n = strlen(h) / 2;
            unsigned char *buf = malloc(n + 1);
            for (size_t i = 0; i < n; i++) {
                char two[3] = {h[i * 2], h[i * 2 + 1], 0};
                buf[i] = (unsigned char)strtoul(two, NULL, 16);
            }
            /* byte at a time, so chunking cannot matter */
            for (size_t i = 0; i < n; i++) wf_sha256_update(&c, buf + i, 1);
            unsigned char whole[32];
            wf_sha256_buffer(buf, n, whole);
            hex_of(whole, hex);
            WF_CHECK(strcmp(hex, vstr(v, "sha256")) == 0);
            free(buf);
        }
        wf_sha256_final(&c, d);
        hex_of(d, hex);
        int ok = strcmp(hex, vstr(v, "sha256")) == 0;
        WF_CHECK(ok);
        if (!ok) fprintf(stderr, "  sha256 vector: %s\n", vstr(v, "name"));
    }
    cJSON_Delete(root);

    unsigned char out[32];
    WF_CHECK(wf_sha256_from_hex("ab", 2, out) == WF_ERR_PARSE);
    WF_CHECK(wf_sha256_from_hex(NULL, 64, out) == WF_ERR_PARSE);
}

static void test_manifests(void) {
    cJSON *root = load("manifest.json");
    WF_CHECK(root != NULL);
    if (!root) return;
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "vectors")) {
        char *text = cJSON_PrintUnformatted(
            cJSON_GetObjectItemCaseSensitive(v, "manifest"));
        const cJSON *pj = cJSON_GetObjectItemCaseSensitive(v, "policy");
        wf_update_policy pol;
        memset(&pol, 0, sizeof pol);
        if (pj) {
            const cJSON *ms = cJSON_GetObjectItemCaseSensitive(pj, "max_size");
            if (ms) pol.max_size = (unsigned long)ms->valuedouble;
            pol.app = vstr(pj, "app");
            pol.url_prefix = vstr(pj, "url_prefix");
        }
        wf_update_manifest m;
        wf_status st =
            wf_update_parse_manifest(text, strlen(text), pj ? &pol : NULL, &m);
        const char *want = vstr(v, "expect");
        wf_status want_st = !strcmp(want, "ok")            ? WF_OK
                            : !strcmp(want, "parse_error") ? WF_ERR_PARSE
                                                           : WF_ERR_VALIDATION;
        int ok = st == want_st;
        WF_CHECK(ok);
        if (!ok)
            fprintf(stderr, "  manifest vector: %s (got %d)\n", vstr(v, "name"),
                    (int)st);
        if (want_st != WF_OK) {
            /* Zeroed on every error: no half-filled manifest to misuse. */
            WF_CHECK(m.version[0] == '\0' && m.asset.url[0] == '\0' &&
                     m.asset.size == 0);
        } else if (cJSON_IsTrue(
                       cJSON_GetObjectItemCaseSensitive(v, "has_signature"))) {
            WF_CHECK(m.has_signature == 1);
        } else {
            WF_CHECK(m.has_signature == 0);
        }
        free(text);
    }
    cJSON_Delete(root);
    wf_update_manifest m;
    WF_CHECK(wf_update_parse_manifest("not json", 8, NULL, &m) == WF_ERR_PARSE);
    WF_CHECK(wf_update_parse_manifest("[]", 2, NULL, &m) == WF_ERR_PARSE);
    WF_CHECK(wf_update_parse_manifest(NULL, 0, NULL, &m) == WF_ERR_INVALID_ARG);
}

static void test_verify(void) {
    const char *payload = "the quick brown fox";
    size_t n = strlen(payload);
    wf_update_asset a;
    memset(&a, 0, sizeof a);
    wf_sha256_buffer(payload, n, a.sha256);
    a.size = (unsigned long)n;
    wf_update_verify v;

    /* Any chunking verifies. */
    wf_update_verify_init(&v, &a);
    for (size_t i = 0; i < n; i += 3) {
        size_t k = n - i < 3 ? n - i : 3;
        WF_CHECK(wf_update_verify_feed(&v, payload + i, k) == WF_OK);
    }
    WF_CHECK(wf_update_verify_final(&v) == WF_OK);

    /* One flipped byte fails. */
    char bad[64];
    memcpy(bad, payload, n);
    bad[4] ^= 1;
    wf_update_verify_init(&v, &a);
    WF_CHECK(wf_update_verify_feed(&v, bad, n) == WF_OK);
    WF_CHECK(wf_update_verify_final(&v) == WF_ERR_VALIDATION);

    /* Truncated fails. */
    wf_update_verify_init(&v, &a);
    WF_CHECK(wf_update_verify_feed(&v, payload, n - 1) == WF_OK);
    WF_CHECK(wf_update_verify_final(&v) == WF_ERR_VALIDATION);

    /* Too long fails at once, and stays failed. */
    wf_update_verify_init(&v, &a);
    WF_CHECK(wf_update_verify_feed(&v, payload, n) == WF_OK);
    WF_CHECK(wf_update_verify_feed(&v, "x", 1) == WF_ERR_VALIDATION);
    WF_CHECK(wf_update_verify_final(&v) == WF_ERR_VALIDATION);
}

int main(void) {
    test_versions();
    test_sha256();
    test_manifests();
    test_verify();
    WF_TEST_SUMMARY();
}
