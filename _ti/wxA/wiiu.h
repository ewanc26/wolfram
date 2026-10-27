#ifndef WOLFRAM_WIIU_H
#define WOLFRAM_WIIU_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wii U entropy provisioning.
 *
 * This mirrors the Wii API in include/wolfram/wii.h, and for the same reason:
 * the Wii U has no application-facing cryptographically secure RNG that
 * Wolfram can rely on.
 *
 * devkitPro's mbedTLS portlib for Wii U does define mbedtls_hardware_poll, so
 * mbedtls_entropy_func() will happily return bytes — but that implementation
 * is srand(OSGetSystemTick()) followed by rand(). It is a timer-seeded libc
 * PRNG, not an entropy source. Since wii_tls_random() feeds P-256 key
 * generation and ECDSA signing, accepting it would make private keys derivable
 * from the console's uptime at the moment of generation.
 *
 * Wolfram therefore fails closed on Wii U exactly as it does on Wii: signing
 * and key generation return an error until a real seed has been provisioned.
 */

/** Size, in bytes, of the entropy seed these functions expect. */
#define WF_WIIU_ENTROPY_SEED_SIZE 64

/**
 * Supply 64 bytes of cryptographically secure, installation-unique entropy
 * before the first Wii U P-256 operation. The bytes are copied and erased
 * after seeding Wolfram's DRBG. Reusing one seed across consoles or
 * installations is not safe.
 */
wf_status wf_wiiu_set_entropy_seed(const unsigned char *seed, size_t seed_len);

/**
 * Generate the replacement seed for the next boot. A seed must not be reused
 * across boots: the DRBG is deterministic, so re-seeding with the same bytes
 * regenerates the same key material. Until the application atomically persists
 * the returned seed and calls wf_wiiu_commit_entropy_rotation(), signing fails
 * closed.
 */
wf_status wf_wiiu_rotate_entropy_seed(unsigned char *out, size_t out_len);

/** Confirm that the replacement seed was durably persisted. */
wf_status wf_wiiu_commit_entropy_rotation(void);

/** True once a seed has been provisioned and not invalidated by a pending
 *  rotation, i.e. signing and key generation will succeed. */
int wf_wiiu_entropy_ready(void);

struct wf_agent; /* forward decl — avoid pulling in agent.h for all consumers */

/**
 * Wire the application-seeded DRBG into an agent's curl TLS transport.
 * Must be called after wf_wiiu_set_entropy_seed() and before any network I/O.
 * Returns WF_ERR_CRYPTO if the DRBG is not yet seeded.
 */
wf_status wf_wiiu_apply_tls_rng(struct wf_agent *agent);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_WIIU_H */
