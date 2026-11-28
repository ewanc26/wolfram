#ifndef WOLFRAM_CRYPTO_BASE58_H
#define WOLFRAM_CRYPTO_BASE58_H

/* base58btc (the multibase 'z' alphabet), shared by both crypto backends
 * (crypto.c on OpenSSL, crypto_wii.c on mbedTLS) and the private-key
 * multibase codec. Pure C: no OpenSSL, no mbedTLS. Not public API. */

#include <stddef.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Encode `len` bytes. Returns a heap NUL-terminated string, or NULL on
 * allocation failure; the caller frees it with free(). */
char *wf_b58_encode(const unsigned char *data, size_t len);

/* Decode `len` characters. On WF_OK *out is heap-allocated (*out_len bytes);
 * the caller frees it with free(). A character outside the alphabet or an
 * empty input is WF_ERR_INVALID_ARG. */
wf_status wf_b58_decode(const char *str, size_t len, unsigned char **out,
                        size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CRYPTO_BASE58_H */
