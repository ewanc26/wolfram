/**
 * base58.c — base58btc codec shared by crypto.c and crypto_wii.c.
 *
 * See base58.h. Moved here unchanged from the two backends, which carried
 * identical copies.
 */

#include "base58.h"

#include <stdlib.h>
#include <string.h>

static const char wf_b58_alphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

char *wf_b58_encode(const unsigned char *data, size_t len) {
    /* Count leading zero bytes. */
    size_t zeros = 0;
    while (zeros < len && data[zeros] == 0) zeros++;

    /* Big-number base conversion into a temporary buffer (8 bits -> 58). */
    size_t out_size = len * 138 / 100 + 1; /* conservative upper bound */
    unsigned char *buf = calloc(out_size, 1);
    if (!buf) return NULL;

    for (size_t i = zeros; i < len; i++) {
        unsigned carry = data[i];
        for (size_t j = out_size; j-- > 0;) {
            carry += (unsigned)buf[j] * 256u;
            buf[j] = (unsigned char)(carry % 58u);
            carry /= 58u;
        }
    }

    size_t out_len = 0;
    while (out_len < out_size && buf[out_len] == 0) out_len++;

    /* Build the string: leading '1' per zero byte, then base58 digits. */
    size_t total = zeros + (out_size - out_len);
    char *result = malloc(total + 1);
    if (!result) {
        free(buf);
        return NULL;
    }
    size_t p = 0;
    for (size_t i = 0; i < zeros; i++) result[p++] = '1';
    for (size_t i = out_len; i < out_size; i++) {
        result[p++] = wf_b58_alphabet[buf[i]];
    }
    result[p] = '\0';

    free(buf);
    return result;
}

wf_status wf_b58_decode(const char *str, size_t len, unsigned char **out,
                        size_t *out_len) {
    if (!str || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;
    if (len == 0) return WF_ERR_INVALID_ARG;

    size_t zeros = 0;
    while (zeros < len && str[zeros] == '1') zeros++;

    /* Conservative upper bound: log_256(58) < 0.733 per input char. */
    size_t buf_size = len * 733 / 1000 + 1;
    unsigned char *buf = calloc(buf_size, 1);
    if (!buf) return WF_ERR_ALLOC;

    for (size_t i = zeros; i < len; i++) {
        const char *p = str[i] ? strchr(wf_b58_alphabet, str[i]) : NULL;
        if (!p) {
            free(buf);
            return WF_ERR_INVALID_ARG;
        }
        unsigned carry = (unsigned)(p - wf_b58_alphabet);
        for (size_t j = buf_size; j-- > 0;) {
            carry += (unsigned)buf[j] * 58u;
            buf[j] = (unsigned char)(carry & 0xff);
            carry >>= 8;
        }
    }

    size_t start = 0;
    while (start < buf_size && buf[start] == 0) start++;
    size_t payload = buf_size - start;
    unsigned char *result = malloc(zeros + payload ? zeros + payload : 1);
    if (!result) {
        free(buf);
        return WF_ERR_ALLOC;
    }
    size_t k = 0;
    for (size_t i = 0; i < zeros; i++) result[k++] = 0;
    for (size_t i = start; i < buf_size; i++) result[k++] = buf[i];
    free(buf);

    *out = result;
    *out_len = zeros + payload;
    return WF_OK;
}
