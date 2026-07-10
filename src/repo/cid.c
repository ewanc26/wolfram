#include "wolfram/repo/cid.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <openssl/sha.h>

/* RFC 4648 base32 (lowercase, no padding) encoder.
 * Output buffer must be at least ((len * 8 + 4) / 5) + 1 bytes.
 */
static void wf_base32_encode(const unsigned char *in, size_t in_len,
                             char *out) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    size_t i = 0, o = 0;
    while (i < in_len) {
        uint64_t buf = 0;
        int bits = 0;
        for (int n = 0; n < 5 && i < in_len; n++, i++) {
            buf = (buf << 8) | in[i];
            bits += 8;
        }
        int need = (bits + 4) / 5;
        for (int c = 0; c < need; c++) {
            int shift = bits - 5;
            if (shift >= 0) {
                out[o++] = alphabet[(buf >> shift) & 0x1f];
            } else {
                out[o++] = alphabet[(buf << (-shift)) & 0x1f];
            }
            bits -= 5;
        }
    }
    out[o] = '\0';
}

/*
 * Build a raw CIDv1 byte array from a SHA-256 digest.
 * cid_buf must be at least 36 bytes.
 * Returns the number of bytes written (always 36).
 * `codec` is the multicodec: 0x71 (dag-cbor) for repository blocks,
 * 0x55 (raw) for blobs.
 */
static size_t wf_cid_from_hash_codec(const unsigned char hash[32],
                                     unsigned char codec,
                                     unsigned char *cid_buf) {
    cid_buf[0] = 0x01;              /* CIDv1 */
    cid_buf[1] = codec;             /* multicodec */
    cid_buf[2] = 0x12;              /* sha2-256 multihash code */
    cid_buf[3] = 0x20;             /* 32-byte digest length */
    memcpy(cid_buf + 4, hash, 32);  /* the digest itself */
    return 36;
}

static size_t wf_cid_from_hash(const unsigned char hash[32],
                                unsigned char *cid_buf) {
    return wf_cid_from_hash_codec(hash, 0x71, cid_buf); /* dag-cbor */
}

char *wf_cid_to_string(const wf_cid *cid) {
    if (!cid || cid->len == 0) return NULL;

    /* base32 output: ceil(len*8/5) chars + 'b' prefix + NUL */
    size_t b32_len = (cid->len * 8 + 4) / 5;
    char *str = malloc(b32_len + 2);
    if (!str) return NULL;

    str[0] = 'b';
    wf_base32_encode(cid->bytes, cid->len, str + 1);
    return str;
}

wf_status wf_cid_of_block(const unsigned char *cbor, size_t cbor_len,
                           wf_cid *out) {
    if (!cbor || cbor_len == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(cbor, cbor_len, hash);

    out->len = wf_cid_from_hash(hash, out->bytes);
    return WF_OK;
}

wf_status wf_cid_of_bytes(const unsigned char *data, size_t data_len,
                           wf_cid *out) {
    if (!data || data_len == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, data_len, hash);

    out->len = wf_cid_from_hash_codec(hash, 0x55, out->bytes); /* raw */
    return WF_OK;
}

int cid_equal(const wf_cid *a, const wf_cid *b) {
    return a && b && a->len == b->len && a->len > 0 &&
           memcmp(a->bytes, b->bytes, a->len) == 0;
}

/* RFC 4648 base32 (lowercase, no padding) decoder.
 * Writes at most 64 bytes; returns 1 on success, 0 on invalid input. */
static int wf_base32_decode(const char *in, size_t in_len,
                            unsigned char *out, size_t *out_len) {
    size_t i = 0, o = 0, bits = 0;
    uint32_t buf = 0;
    while (i < in_len) {
        int v;
        char c = in[i++];
        if (c >= 'a' && c <= 'z') v = c - 'a';
        else if (c >= '2' && c <= '7') v = c - '2' + 26;
        else return 0;
        buf = (buf << 5) | (uint32_t)v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (o >= 64) return 0;
            out[o++] = (unsigned char)((buf >> bits) & 0xff);
        }
    }
    *out_len = o;
    return 1;
}

wf_status wf_cid_from_string(const char *str, wf_cid *out) {
    if (!str || !out) return WF_ERR_INVALID_ARG;
    if (str[0] != 'b') return WF_ERR_INVALID_ARG;

    unsigned char bytes[64];
    size_t len = 0;
    if (!wf_base32_decode(str + 1, strlen(str + 1), bytes, &len))
        return WF_ERR_INVALID_ARG;
    if (len != 36) return WF_ERR_INVALID_ARG;
    if (bytes[0] != 0x01 || (bytes[1] != 0x71 && bytes[1] != 0x55) ||
        bytes[2] != 0x12 || bytes[3] != 0x20)
        return WF_ERR_INVALID_ARG;

    memcpy(out->bytes, bytes, 36);
    out->len = 36;
    return WF_OK;
}
