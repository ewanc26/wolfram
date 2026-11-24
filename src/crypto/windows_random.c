/**
 * windows_random.c — System RNG for the Windows build.
 *
 * Provides wii_tls_random(), the mbedTLS-compatible f_rng callback that
 * crypto_wii.c uses for key generation and ECDSA signing, and that
 * openssl_compat.c's RAND_bytes() forwards to. The Wii gets that symbol from
 * wii_tls.c, the Wii U from wiiu_random.c and the 3DS from 3ds_random.c.
 *
 * Those platforms each keep an mbedTLS CTR_DRBG and have to find entropy to
 * seed it with. Windows needs neither: BCryptGenRandom() with
 * BCRYPT_USE_SYSTEM_PREFERRED_RNG is the operating system's own CSPRNG, already
 * seeded, safe to call from any thread and with no state to keep here. So there
 * is no seed to provision, no lock, and nothing to initialise.
 */

#include "transport/wii_tls.h"

#include <mbedtls/entropy.h>

/* <windows.h> must come before <bcrypt.h>, which uses its types. */
#include <windows.h>
#include <bcrypt.h>

#include <limits.h>

int wii_tls_random(void *p, unsigned char *out, size_t len) {
    (void)p;
    if (!out && len > 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    /* BCryptGenRandom takes a 32-bit length; size_t is 64-bit on Win64. */
    while (len > 0) {
        ULONG chunk = len > ULONG_MAX ? ULONG_MAX : (ULONG)len;
        if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, out, chunk,
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        out += chunk;
        len -= chunk;
    }
    return 0;
}
