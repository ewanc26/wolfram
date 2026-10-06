/*
 * base64url_strict.c -- see wf_crypto_base64url_decode_strict in crypto.h.
 * No OpenSSL, so it builds on the console targets too.
 */

#include "wolfram/crypto.h"

#include <stdlib.h>

static int b64u_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

wf_status wf_crypto_base64url_decode_strict(const char *in, size_t len,
                                            unsigned char **out,
                                            size_t *out_len) {
    if (!in || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;
    if (len == 0 || len % 4 == 1) return WF_ERR_PARSE;
    size_t full = len / 4, rem = len % 4;
    size_t n = full * 3 + (rem ? rem - 1 : 0);
    unsigned char *buf = malloc(n + 1);
    if (!buf) return WF_ERR_ALLOC;
    size_t o = 0, i = 0;
    for (size_t g = 0; g < full; g++, i += 4) {
        int a = b64u_value((unsigned char)in[i]);
        int b = b64u_value((unsigned char)in[i + 1]);
        int c = b64u_value((unsigned char)in[i + 2]);
        int d = b64u_value((unsigned char)in[i + 3]);
        if ((a | b | c | d) < 0) {
            free(buf);
            return WF_ERR_PARSE;
        }
        buf[o++] = (unsigned char)((a << 2) | (b >> 4));
        buf[o++] = (unsigned char)((b << 4) | (c >> 2));
        buf[o++] = (unsigned char)((c << 6) | d);
    }
    if (rem) {
        int a = b64u_value((unsigned char)in[i]);
        int b = b64u_value((unsigned char)in[i + 1]);
        int c = rem == 3 ? b64u_value((unsigned char)in[i + 2]) : 0;
        if ((a | b | c) < 0) {
            free(buf);
            return WF_ERR_PARSE;
        }
        buf[o++] = (unsigned char)((a << 2) | (b >> 4));
        if (rem == 2) {
            if (b & 0x0f) { /* non-canonical: unused bits must be zero */
                free(buf);
                return WF_ERR_PARSE;
            }
        } else {
            buf[o++] = (unsigned char)((b << 4) | (c >> 2));
            if (c & 0x03) {
                free(buf);
                return WF_ERR_PARSE;
            }
        }
    }
    buf[o] = '\0';
    *out = buf;
    *out_len = o;
    return WF_OK;
}
