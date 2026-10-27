#ifndef WOLFRAM_3DS_H
#define WOLFRAM_3DS_H

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Nintendo 3DS entropy.
 *
 * The Wii and the Wii U have no cryptographically secure RNG that Wolfram can
 * reach, so those platforms fail closed until the application provisions a
 * seed (see wolfram/wii.h and wolfram/wiiu.h). The 3DS is different: its
 * SSLC service exposes sslcGenerateRandomData(), which the 3DS backs with
 * hardware. devkitPro's 3DS mbedTLS portlib registers that same call as its
 * MBEDTLS_ENTROPY_HARDWARE_ALT source, so mbedtls_entropy_func() is a genuine
 * entropy source here rather than the timer-seeded libc PRNG the Wii U ships.
 *
 * Consequently there is no seed to provision and no rotation to persist. The
 * DRBG behind wii_tls_random() is seeded lazily from hardware entropy on first
 * use and stays resident for the life of the process.
 */

/** True once the DRBG has been seeded from hardware entropy, i.e. once
 *  signing and key generation will succeed. */
int wf_3ds_entropy_ready(void);

struct wf_agent;

/**
 * Wire the hardware-seeded DRBG into an agent's curl TLS transport. Optional:
 * devkitPro's 3DS libcurl already draws TLS entropy from the same hardware
 * source, so this only makes the DRBG explicit and shared with the rest of the
 * SDK. Returns WF_ERR_CRYPTO if the DRBG cannot be seeded.
 */
wf_status wf_3ds_apply_tls_rng(struct wf_agent *agent);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_3DS_H */
