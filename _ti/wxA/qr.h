/*
 * qr.h -- a small QR Code encoder that returns the module matrix, so a client
 * draws it however its platform draws things (rectangles, a texture, text).
 *
 * Byte mode only, versions 1 to 10 (up to 271 bytes at level L), all four error
 * correction levels, mask chosen by the standard's penalty rules. That is
 * enough for the URLs a client wants to show as a code. It follows ISO/IEC
 * 18004; the tests decode generated codes with an independent decoder.
 *
 * Pure C with no allocation except the returned matrix, so it builds on every
 * target, console targets included.
 */

#ifndef WOLFRAM_QR_H
#define WOLFRAM_QR_H

#include "wolfram/xrpc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_qr_ecc {
    WF_QR_ECC_L = 0, /* about 7% of codewords recoverable */
    WF_QR_ECC_M = 1, /* about 15% */
    WF_QR_ECC_Q = 2, /* about 25% */
    WF_QR_ECC_H = 3  /* about 30% */
} wf_qr_ecc;

#define WF_QR_MAX_VERSION 10
#define WF_QR_MAX_SIZE (4 * WF_QR_MAX_VERSION + 17)

/*
 * Encode `len` bytes at `data` (no terminator needed, any bytes allowed) at
 * error correction level `ecc`, choosing the smallest version that fits.
 * On WF_OK, *modules is a malloc'd array of (*size) * (*size) bytes in
 * row-major order, 1 for a dark module and 0 for a light one, without the quiet
 * zone: draw at least four light modules around it. Free it with free().
 * WF_ERR_INVALID_ARG for a bad argument or data too long for version 10 at that
 * level. On error *modules is NULL.
 */
wf_status wf_qr_encode_bytes(const void *data, size_t len, wf_qr_ecc ecc,
                             uint8_t **modules, int *size);

/* The same for a NUL-terminated string. */
wf_status wf_qr_encode(const char *text, wf_qr_ecc ecc, uint8_t **modules,
                       int *size);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_QR_H */
