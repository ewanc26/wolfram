#ifndef WOLFRAM_WII_H
#define WOLFRAM_WII_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Supply 64 bytes of cryptographically secure, installation-unique entropy
 * before the first Wii HTTPS or P-256 operation. The bytes are copied and
 * erased after seeding Wolfram's DRBG. Reusing one seed across consoles or
 * installations is not safe.
 */
wf_status wf_wii_set_entropy_seed(const unsigned char *seed, size_t seed_len);

/** Generate the replacement seed for the next boot. Until the application
 *  atomically persists it and calls wf_wii_commit_entropy_rotation(), TLS and
 *  signing fail closed. */
wf_status wf_wii_rotate_entropy_seed(unsigned char *out, size_t out_len);

/** Confirm that the replacement seed was durably persisted. */
wf_status wf_wii_commit_entropy_rotation(void);

#ifdef __cplusplus
}
#endif

#endif
