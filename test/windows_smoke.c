/**
 * windows_smoke.c — Link and smoke check for the Windows build.
 *
 * The Windows build (WOLFRAM_BUILD_WINDOWS) produces a static archive, and an
 * archive hides undefined symbols until a program links against it. This
 * program is that program: it calls into each part of the library that is
 * wired differently on Windows than anywhere else, so a missing backend fails
 * the build at link time instead of in a consumer.
 *
 *   - windows_platform.c   init, and the clock behind wf_tid_now()
 *   - windows_random.c     the RNG behind key generation and signing
 *   - crypto_wii.c         P-256 and secp256k1 on mbedTLS
 *   - openssl_compat.c     one-shot and streaming SHA-256 behind CIDs
 *   - windows_tls.c        the socket transport's connection backend
 *   - json.c               the schema validator without POSIX regex
 *
 * It is offline by construction: the Windows build has no network transport
 * yet, and the checks below assert exactly that. CI builds it and does not run
 * it; run it on Windows, or under Wine with `wine wolfram_windows_smoke.exe`.
 */

#include "wolfram/crypto.h"
#include "wolfram/json.h"
#include "wolfram/platform.h"
#include "wolfram/repo/cid.h"
#include "wolfram/tid.h"
#include "wolfram/websocket.h"
#include "wolfram/xrpc.h"
#include "test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WF_SMOKE_SIG_LEN 64
/* A signer that forgets to normalise produces a high-S signature half the
 * time, so this many round trips miss it once in 2^32 runs. */
#define WF_SMOKE_SIGN_ROUNDS 32
#define WF_SMOKE_MICROS_PER_SEC 1000000ull
/* How far wf_tid_now() may sit from time(): generous enough for a slow
 * emulator, far tighter than the decades a boot-relative clock is off by. */
#define WF_SMOKE_CLOCK_TOLERANCE_SEC 3600ull

/* ── Hashing: openssl_compat.c ────────────────────────────────────────── */

static void test_sha256(void) {
    /* FIPS 180-4 "abc" vector. */
    static const unsigned char want[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    unsigned char got[32] = {0};

    WF_CHECK(wf_crypto_sha256((const unsigned char *)"abc", 3, got) == WF_OK);
    WF_CHECK(memcmp(got, want, sizeof(want)) == 0);
}

static void test_cid(void) {
    static const unsigned char data[] = "hello";
    const size_t len = sizeof(data) - 1;
    wf_cid one_shot = {0};
    wf_cid streamed = {0};

    WF_CHECK(wf_cid_of_bytes(data, len, &one_shot) == WF_OK);

    char *str = wf_cid_to_string(&one_shot);
    WF_CHECK(str != NULL);
    if (str) {
        WF_CHECK(strcmp(str, "bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44ye"
                             "gnrjhc4yeq") == 0);
        free(str);
    }

    /* The incremental hasher is a second SHA-256 (mbedTLS's, through the
     * EVP_MD_CTX shim); it must agree with the one-shot one, fed in pieces. */
    wf_cid_hasher *hasher = wf_cid_hasher_new();
    WF_CHECK(hasher != NULL);
    if (hasher) {
        WF_CHECK(wf_cid_hasher_update(hasher, data, 2) == WF_OK);
        WF_CHECK(wf_cid_hasher_update(hasher, data + 2, len - 2) == WF_OK);
        WF_CHECK(wf_cid_hasher_finish_raw(hasher, &streamed) == WF_OK);
        wf_cid_hasher_free(hasher);
        WF_CHECK(streamed.len == one_shot.len);
        WF_CHECK(memcmp(streamed.bytes, one_shot.bytes, one_shot.len) == 0);
    }
}

/* ── Keys and signatures: crypto_wii.c + windows_random.c ─────────────── */

static void test_sign_verify(wf_key_type type) {
    static const unsigned char msg[] = "wolfram windows smoke";
    wf_signing_key key = {0};
    wf_signing_key other = {0};
    unsigned char sig[WF_SMOKE_SIG_LEN] = {0};
    char *didkey = NULL;

    WF_CHECK(wf_signing_key_generate(type, &key) == WF_OK);
    WF_CHECK(wf_signing_key_generate(type, &other) == WF_OK);
    /* An RNG that returns the same bytes twice is not one. */
    WF_CHECK(memcmp(key.bytes, other.bytes, sizeof(key.bytes)) != 0);

    WF_CHECK(wf_signing_key_public_didkey(&key, &didkey) == WF_OK);
    WF_CHECK(didkey != NULL);
    if (!didkey) return;

    /* Signing is randomised, so each round is a different signature. */
    for (int round = 0; round < WF_SMOKE_SIGN_ROUNDS; round++) {
        WF_CHECK(wf_sign(&key, msg, sizeof(msg), sig, sizeof(sig)) == WF_OK);
        WF_CHECK(wf_verify(didkey, msg, sizeof(msg), sig, sizeof(sig)) ==
                 WF_OK);
    }

    sig[0] ^= 0x01;
    WF_CHECK(wf_verify(didkey, msg, sizeof(msg), sig, sizeof(sig)) != WF_OK);
    free(didkey);
}

/* ── Clock: windows_platform.c ────────────────────────────────────────── */

static void test_tid_clock(void) {
    char first[15] = {0};
    char second[15] = {0};

    WF_CHECK(wf_tid_now(first) == WF_OK);
    WF_CHECK(wf_tid_now(second) == WF_OK);
    /* TIDs sort by creation time. */
    WF_CHECK(strcmp(first, second) < 0);

    /* The timestamp half is UNIX time, so it tracks time(). */
    uint64_t tid_sec = wf_tid_timestamp_micros(first) / WF_SMOKE_MICROS_PER_SEC;
    uint64_t now_sec = (uint64_t)time(NULL);
    uint64_t apart = tid_sec > now_sec ? tid_sec - now_sec : now_sec - tid_sec;
    WF_CHECK(apart < WF_SMOKE_CLOCK_TOLERANCE_SEC);
}

/* ── Transport: xrpc_wii.c / websocket_wii.c over windows_tls.c ───────── */

static wf_status canned_handler(void *userdata, const char *method,
                                const char *url, const char *content_type,
                                const char *body, size_t body_len,
                                const wf_http_header *headers,
                                size_t header_count, wf_response *out) {
    static const char reply[] = "{\"ok\":true}";
    int *calls = (int *)userdata;
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;

    (*calls)++;
    out->body = malloc(sizeof(reply));
    if (!out->body) return WF_ERR_ALLOC;
    memcpy(out->body, reply, sizeof(reply));
    out->body_len = sizeof(reply) - 1;
    out->status = 200;
    return WF_OK;
}

static void test_transport(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("https://example.invalid");
    WF_CHECK(client != NULL);
    if (!client) return;

    /* No connection backend yet: the request fails before anything is sent. */
    wf_response res = {0};
    WF_CHECK(wf_xrpc_query(client, "com.atproto.server.describeServer", NULL,
                           &res) == WF_ERR_NETWORK);
    wf_response_free(&res);

    /* Everything above the connection works, which is what lets a consumer
     * test against the handler seam on Windows today. */
    int calls = 0;
    wf_response canned = {0};
    wf_xrpc_set_handler(client, canned_handler, &calls);
    WF_CHECK(wf_xrpc_query(client, "com.atproto.server.describeServer", NULL,
                           &canned) == WF_OK);
    WF_CHECK(calls == 1);
    WF_CHECK(canned.status == 200);
    WF_CHECK(canned.body != NULL && strcmp(canned.body, "{\"ok\":true}") == 0);
    wf_response_free(&canned);
    wf_xrpc_client_free(client);

    wf_websocket *socket = NULL;
    WF_CHECK(wf_websocket_connect("wss://example.invalid/xrpc/x", &socket) ==
             WF_ERR_NETWORK);
    WF_CHECK(socket == NULL);
}

/* ── JSON schema validation: json.c without POSIX regex ───────────────── */

static void test_json_validate(void) {
    static const char doc[] = "\"abc\"";
    static const char plain[] = "{\"type\":\"string\",\"maxLength\":3}";
    static const char pattern[] = "{\"type\":\"string\",\"pattern\":\"^a\"}";
    char *error = NULL;

    WF_CHECK(wf_json_validate(plain, sizeof(plain) - 1, doc, sizeof(doc) - 1,
                              &error) == WF_OK);
    free(error);
    error = NULL;

    /* A pattern cannot be evaluated here, so it must not validate. */
    WF_CHECK(wf_json_validate(pattern, sizeof(pattern) - 1, doc,
                              sizeof(doc) - 1, &error) != WF_OK);
    WF_CHECK(error != NULL && strstr(error, "not supported") != NULL);
    free(error);
}

int main(void) {
    WF_CHECK(wf_platform_init() == WF_OK);

    test_sha256();
    test_cid();
    test_sign_verify(WF_KEY_TYPE_P256);
    test_sign_verify(WF_KEY_TYPE_SECP256K1);
    test_tid_clock();
    test_transport();
    test_json_validate();

    wf_platform_shutdown();
    WF_TEST_SUMMARY();
}
