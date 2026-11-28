/**
 * test_key_multibase.c — the private-key multibase codec.
 *
 * Expected values come from upstream, never from Wolfram's own output:
 *   - indigo atproto/atcrypto/keys_test.go TestParsePrivateMultibase
 *     (privK256MB, privP256MB round-trip strings);
 *   - indigo atproto/atcrypto/testdata/w3c_didkey_K256.json and
 *     w3c_didkey_P256.json (private key -> did:key pairs).
 * The test carries its own base58 codec so the wire bytes it builds are
 * not produced by the code under test.
 */

#include "wolfram/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

static const char B58[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

/* Independent base58btc encode of `len` bytes into `out` (cap bytes). */
static void t_b58_encode(const unsigned char *in, size_t len, char *out,
                         size_t cap) {
    unsigned char digits[128] = {0};
    size_t ndig = 0, zeros = 0, i, j, o = 0;
    while (zeros < len && in[zeros] == 0) zeros++;
    for (i = zeros; i < len; i++) {
        unsigned carry = in[i];
        for (j = 0; j < ndig; j++) {
            carry += (unsigned)digits[j] << 8;
            digits[j] = (unsigned char)(carry % 58);
            carry /= 58;
        }
        while (carry) {
            digits[ndig++] = (unsigned char)(carry % 58);
            carry /= 58;
        }
    }
    for (i = 0; i < zeros && o + 1 < cap; i++) out[o++] = '1';
    for (i = ndig; i-- > 0 && o + 1 < cap;) out[o++] = B58[digits[i]];
    out[o] = '\0';
}

/* Independent base58btc decode; returns decoded length or 0 on error. */
static size_t t_b58_decode(const char *s, unsigned char *out, size_t cap) {
    unsigned char bytes[128] = {0};
    size_t nb = 0, zeros = 0, i, j;
    while (s[zeros] == '1') zeros++;
    for (i = zeros; s[i]; i++) {
        const char *p = strchr(B58, s[i]);
        unsigned carry;
        if (!p) return 0;
        carry = (unsigned)(p - B58);
        for (j = 0; j < nb; j++) {
            carry += (unsigned)bytes[j] * 58;
            bytes[j] = (unsigned char)(carry & 0xff);
            carry >>= 8;
        }
        while (carry) {
            bytes[nb++] = (unsigned char)(carry & 0xff);
            carry >>= 8;
        }
    }
    if (zeros + nb > cap) return 0;
    for (i = 0; i < zeros; i++) out[i] = 0;
    for (i = 0; i < nb; i++) out[zeros + i] = bytes[nb - 1 - i];
    return zeros + nb;
}

/* Build 'z' + b58(prefix || scalar) with the test's own encoder. */
static void t_multibase(unsigned char p0, unsigned char p1,
                        const unsigned char *scalar, size_t scalar_len,
                        char *out, size_t cap) {
    unsigned char raw[64];
    raw[0] = p0;
    raw[1] = p1;
    memcpy(raw + 2, scalar, scalar_len);
    out[0] = 'z';
    t_b58_encode(raw, 2 + scalar_len, out + 1, cap - 1);
}

static void hex32(const char *hex, unsigned char out[32]) {
    for (int i = 0; i < 32; i++) {
        unsigned v = 0;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (unsigned char)v;
    }
}

static void test_indigo_round_trip(const char *mb, wf_key_type want) {
    wf_signing_key key;
    char *again = NULL;
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_OK);
    WF_CHECK(key.type == want);
    WF_CHECK(wf_signing_key_to_multibase(&key, &again) == WF_OK);
    WF_CHECK(again && strcmp(again, mb) == 0);
    wf_signing_key_secret_free(again);
    wf_secure_zero(&key, sizeof(key));
}

/* A private key from an upstream fixture, wrapped by the test's own encoder,
 * must parse and derive the fixture's did:key. */
static void test_fixture_didkey(wf_key_type type, const unsigned char s[32],
                                const char *want_didkey) {
    char mb[96];
    wf_signing_key key;
    char *didkey = NULL;
    char *ours = NULL;
    t_multibase(type == WF_KEY_TYPE_P256 ? 0x86 : 0x81, 0x26, s, 32, mb,
                sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_OK);
    WF_CHECK(key.type == type);
    WF_CHECK(memcmp(key.bytes, s, 32) == 0);
    WF_CHECK(wf_signing_key_public_didkey(&key, &didkey) == WF_OK);
    WF_CHECK(didkey && strcmp(didkey, want_didkey) == 0);
    WF_CHECK(wf_signing_key_to_multibase(&key, &ours) == WF_OK);
    WF_CHECK(ours && strcmp(ours, mb) == 0);
    free(didkey);
    wf_signing_key_secret_free(ours);
    wf_secure_zero(&key, sizeof(key));
}

static void test_rejects(void) {
    static const unsigned char zero[32] = {0};
    /* secp256k1 order n, and n - 1 (the largest valid scalar). */
    unsigned char k_n[32], k_n1[32], p_n[32];
    char mb[96];
    wf_signing_key key;
    const char *good = "z3vLWgA9nXoPzxsJJafDY9BPrZd3EDWjvcCtYfrFxZ7xbMVi";
    char buf[96];

    hex32("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141",
          k_n);
    hex32("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140",
          k_n1);
    hex32("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
          p_n);

    WF_CHECK(wf_signing_key_from_multibase(NULL, &key) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_signing_key_from_multibase("", &key) == WF_ERR_PARSE);
    WF_CHECK(wf_signing_key_from_multibase("z", &key) == WF_ERR_PARSE);

    /* Wrong multibase prefix. */
    strcpy(buf, good);
    buf[0] = 'Z';
    WF_CHECK(wf_signing_key_from_multibase(buf, &key) == WF_ERR_PARSE);
    /* A character outside the alphabet ('0', 'O', 'I', 'l'). */
    strcpy(buf, good);
    buf[10] = '0';
    WF_CHECK(wf_signing_key_from_multibase(buf, &key) == WF_ERR_PARSE);
    strcpy(buf, good);
    buf[20] = 'l';
    WF_CHECK(wf_signing_key_from_multibase(buf, &key) == WF_ERR_PARSE);
    /* Truncated and extended: the decoded length is no longer 34. */
    strcpy(buf, good);
    buf[strlen(buf) - 1] = '\0';
    WF_CHECK(wf_signing_key_from_multibase(buf, &key) == WF_ERR_PARSE);
    strcpy(buf, good);
    strcat(buf, "2");
    WF_CHECK(wf_signing_key_from_multibase(buf, &key) == WF_ERR_PARSE);
    /* Whitespace is not trimmed. */
    snprintf(buf, sizeof(buf), "%s\n", good);
    WF_CHECK(wf_signing_key_from_multibase(buf, &key) == WF_ERR_PARSE);
    /* The failed parse leaves no partial key behind. */
    WF_CHECK(key.type == WF_KEY_TYPE_UNKNOWN);

    /* A public-key multicodec (secp256k1-pub 0xe7 0x01) is not a secret. */
    t_multibase(0xe7, 0x01, k_n1, 32, mb, sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_ERR_PARSE);
    /* A did:key is not a secret either. */
    WF_CHECK(wf_signing_key_from_multibase(
                 "did:key:zQ3shokFTS3brHcDQrn82RUDfCZESWL1ZdCEJwekUDPQiYBme",
                 &key) == WF_ERR_PARSE);
    /* Right prefix, 31-byte scalar. */
    t_multibase(0x81, 0x26, k_n1, 31, mb, sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_ERR_PARSE);

    /* Scalars outside [1, n-1]. */
    t_multibase(0x81, 0x26, zero, 32, mb, sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_ERR_PARSE);
    t_multibase(0x81, 0x26, k_n, 32, mb, sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_ERR_PARSE);
    t_multibase(0x86, 0x26, p_n, 32, mb, sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_ERR_PARSE);
    /* n - 1 is the largest valid secp256k1 scalar. */
    t_multibase(0x81, 0x26, k_n1, 32, mb, sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_OK);
    WF_CHECK(key.type == WF_KEY_TYPE_SECP256K1);
    /* ...but it is above the P-256 order, so not a P-256 scalar. */
    t_multibase(0x86, 0x26, k_n1, 32, mb, sizeof(mb));
    WF_CHECK(wf_signing_key_from_multibase(mb, &key) == WF_ERR_PARSE);

    /* The encoder refuses what the parser refuses. */
    {
        wf_signing_key bad = {0};
        char *out = (char *)1;
        bad.type = WF_KEY_TYPE_P256;
        WF_CHECK(wf_signing_key_to_multibase(&bad, &out) == WF_ERR_INVALID_ARG);
        WF_CHECK(out == NULL);
        memcpy(bad.bytes, p_n, 32);
        WF_CHECK(wf_signing_key_to_multibase(&bad, &out) == WF_ERR_INVALID_ARG);
        bad.type = WF_KEY_TYPE_UNKNOWN;
        memcpy(bad.bytes, k_n1, 32);
        WF_CHECK(wf_signing_key_to_multibase(&bad, &out) == WF_ERR_INVALID_ARG);
    }
}

static void test_generate_round_trip(wf_key_type type) {
    wf_signing_key key, back;
    char *mb = NULL;
    char *d1 = NULL, *d2 = NULL;
    WF_CHECK(wf_signing_key_generate(type, &key) == WF_OK);
    WF_CHECK(wf_signing_key_to_multibase(&key, &mb) == WF_OK);
    WF_CHECK(mb && mb[0] == 'z');
    WF_CHECK(wf_signing_key_from_multibase(mb, &back) == WF_OK);
    WF_CHECK(back.type == key.type);
    WF_CHECK(memcmp(back.bytes, key.bytes, 32) == 0);
    WF_CHECK(wf_signing_key_public_didkey(&key, &d1) == WF_OK);
    WF_CHECK(wf_signing_key_public_didkey(&back, &d2) == WF_OK);
    WF_CHECK(d1 && d2 && strcmp(d1, d2) == 0);
    free(d1);
    free(d2);
    wf_signing_key_secret_free(mb);
    wf_secure_zero(&key, sizeof(key));
    wf_secure_zero(&back, sizeof(back));
    WF_CHECK(key.type == WF_KEY_TYPE_UNKNOWN && key.bytes[0] == 0);
}

int main(void) {
    unsigned char s[32];
    unsigned char raw[64];

    /* indigo keys_test.go TestParsePrivateMultibase. */
    test_indigo_round_trip("z42tvqQS5sVhaV1jLZ5P6ZKEPEbSpYavNVmT88YDYV3MEZ8D",
                           WF_KEY_TYPE_P256);
    test_indigo_round_trip("z3vLWgA9nXoPzxsJJafDY9BPrZd3EDWjvcCtYfrFxZ7xbMVi",
                           WF_KEY_TYPE_SECP256K1);

    /* indigo testdata/w3c_didkey_P256.json: privateKeyBytesBase58. */
    WF_CHECK(t_b58_decode("9p4VRzdmhsnq869vQjVCTrRry7u4TtfRxhvBFJTGU2Cp", raw,
                          sizeof(raw)) == 32);
    test_fixture_didkey(
        WF_KEY_TYPE_P256, raw,
        "did:key:zDnaeTiq1PdzvZXUaMdezchcMJQpBdH2VN4pgrrEhMCCbmwSb");
    test_generate_round_trip(WF_KEY_TYPE_P256);

#ifdef HAVE_LIBSECP256K1
    /* indigo testdata/w3c_didkey_K256.json. */
    hex32("9085d2bef69286a6cbb51623c8fa258629945cd55ca705cc4e66700396894e0c",
          s);
    test_fixture_didkey(
        WF_KEY_TYPE_SECP256K1, s,
        "did:key:zQ3shokFTS3brHcDQrn82RUDfCZESWL1ZdCEJwekUDPQiYBme");
    hex32("f0f4df55a2b3ff13051ea814a8f24ad00f2e469af73c363ac7e9fb999a9072ed",
          s);
    test_fixture_didkey(
        WF_KEY_TYPE_SECP256K1, s,
        "did:key:zQ3shtxV1FrJfhqE1dvxYRcCknWNjHc3c5X1y3ZSoPDi2aur2");
    test_generate_round_trip(WF_KEY_TYPE_SECP256K1);
#else
    (void)s;
#endif

    test_rejects();
    WF_TEST_SUMMARY();
}
