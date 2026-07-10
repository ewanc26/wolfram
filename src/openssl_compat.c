/**
 * openssl_compat.c — Pure-C implementations of the minimal OpenSSL API
 *                     needed by the wolfram SDK on Wii.
 *
 * SHA-256 is a straightforward FIPS 180-4 implementation.
 * Base64 encode/decode is standard RFC 4648.
 * RAND_bytes is a stub that returns failure.
 */

#include "openssl_compat.h"

#include <string.h>

/* ── SHA-256 (FIPS 180-4) ───────────────────────────────────────────── */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(uint32_t state[8], const unsigned char block[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, w[64];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void SHA256(const unsigned char *data, size_t len, unsigned char *out) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    unsigned char block[64];
    size_t i, remaining;

    /* Process complete 64-byte blocks. */
    for (i = 0; i + 64 <= len; i += 64) {
        sha256_transform(state, data + i);
    }

    /* Pad: append 1-bit, zeros, then 64-bit big-endian length. */
    remaining = len - i;
    memset(block, 0, 64);
    if (remaining > 0) {
        memcpy(block, data + i, remaining);
    }
    block[remaining] = 0x80;

    if (remaining >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }

    {
        uint64_t bitlen = (uint64_t)len * 8;
        block[56] = (unsigned char)(bitlen >> 56);
        block[57] = (unsigned char)(bitlen >> 48);
        block[58] = (unsigned char)(bitlen >> 40);
        block[59] = (unsigned char)(bitlen >> 32);
        block[60] = (unsigned char)(bitlen >> 24);
        block[61] = (unsigned char)(bitlen >> 16);
        block[62] = (unsigned char)(bitlen >> 8);
        block[63] = (unsigned char)(bitlen);
    }
    sha256_transform(state, block);

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (unsigned char)(state[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(state[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(state[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(state[i]);
    }
}

/* ── Base64 encode/decode (RFC 4648) ────────────────────────────────── */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int EVP_EncodeBlock(unsigned char *out, const unsigned char *in, int len) {
    int i, j = 0;
    for (i = 0; i + 2 < len; i += 3) {
        out[j++] = b64_table[(in[i] >> 2) & 0x3F];
        out[j++] = b64_table[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
        out[j++] = b64_table[((in[i + 1] & 0x0F) << 2) | (in[i + 2] >> 6)];
        out[j++] = b64_table[in[i + 2] & 0x3F];
    }
    if (i < len) {
        out[j++] = b64_table[(in[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = b64_table[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
            out[j++] = b64_table[(in[i + 1] & 0x0F) << 2];
        } else {
            out[j++] = b64_table[(in[i] & 0x03) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
    return j;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int EVP_DecodeBlock(unsigned char *out, const unsigned char *in, int len) {
    int i, j = 0, val[4];
    int pad = 0;

    /* Strip trailing padding. */
    while (len > 0 && in[len - 1] == '=') { len--; pad++; }

    for (i = 0; i < len; i += 4) {
        val[0] = b64_decode_char((char)in[i]);
        val[1] = (i + 1 < len) ? b64_decode_char((char)in[i + 1]) : 0;
        val[2] = (i + 2 < len) ? b64_decode_char((char)in[i + 2]) : 0;
        val[3] = (i + 3 < len) ? b64_decode_char((char)in[i + 3]) : 0;

        if (val[0] < 0 || val[1] < 0 || val[2] < 0 || val[3] < 0)
            return -1;

        out[j++] = (unsigned char)((val[0] << 2) | (val[1] >> 4));
        if (i + 2 < len) out[j++] = (unsigned char)(((val[1] & 0x0F) << 4) | (val[2] >> 2));
        if (i + 3 < len) out[j++] = (unsigned char)(((val[2] & 0x03) << 6) | val[3]);
    }

    return j - pad;
}

/* ── Random bytes (stub) ────────────────────────────────────────────── */

/*
 * TODO: Implement via mbedTLS mbedtls_ctr_drbg_random() seeded from
 * a hardware RNG or a seed stored on SD card.
 */
int RAND_bytes(unsigned char *buf, int len) {
    (void)buf; (void)len;
    return 0;  /* failure */
}
