/**
 * wiiu_random.c — CSPRNG for the Nintendo Wii U.
 *
 * Provides wii_tls_random(), the mbedTLS-compatible f_rng callback that
 * crypto_wii.c uses for P-256 key generation and ECDSA signing.
 *
 * On the Wii, that symbol comes from wii_tls.c, which also implements the
 * socket/TLS transport on top of libogc. The Wii U does not build that file:
 * it uses the curl-based transport instead (see WOLFRAM_USE_SOCKET_TRANSPORT
 * in CMakeLists.txt), and wii_tls.c is written against libogc's net_* API
 * regardless. So the RNG half is provided separately here.
 *
 * Why not just use mbedtls_entropy_func()
 * ---------------------------------------
 * devkitPro's Wii U mbedTLS portlib does supply mbedtls_hardware_poll, so
 * seeding a DRBG from it would compile and run. Disassembling it shows it is:
 *
 *     srand(OSGetSystemTick()); rand();
 *
 * — a timer-seeded libc PRNG. Its entire state is the console's tick counter
 * at the moment of the call, which is guessable to within a narrow window.
 * Using it for P-256 key generation would make the resulting private keys
 * recoverable by search, and using it for ECDSA nonces would leak the private
 * key outright after a single signature.
 *
 * Wolfram already refuses exactly this on the Wii ("Timing, addresses, the
 * console MAC, and rand() are deliberately not accepted as entropy"), so the
 * Wii U follows the same rule: fail closed until the application provisions a
 * real seed via wf_wiiu_set_entropy_seed().
 */

#include "wolfram/wiiu.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>

#include <string.h>

#define SEED_SIZE WF_WIIU_ENTROPY_SEED_SIZE

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr;
static int g_init;

static unsigned char g_seed[SEED_SIZE];
static int g_seed_ready;
static int g_rotation_pending;

/*
 * The only entropy source registered with mbedTLS. It hands over the
 * provisioned seed exactly once and then wipes it, so a DRBG reseed cannot
 * silently fall back to anything weaker.
 */
static int wiiu_entropy_poll(void *p, unsigned char *out, size_t len,
                             size_t *olen) {
    (void)p;
    if (!g_seed_ready || len < SEED_SIZE) {
        *olen = 0;
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    memcpy(out, g_seed, SEED_SIZE);
    memset(g_seed, 0, sizeof(g_seed));
    g_seed_ready = 0;
    *olen = SEED_SIZE;
    return 0;
}

static wf_status wiiu_drbg_init(void) {
    if (g_init) return WF_OK;
    if (!g_seed_ready) return WF_ERR_CRYPTO;

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr);

    /* MBEDTLS_ENTROPY_SOURCE_STRONG: this is the only source, and a DRBG seed
     * must not succeed without it. */
    if (mbedtls_entropy_add_source(&g_entropy, wiiu_entropy_poll, NULL,
                                   SEED_SIZE,
                                   MBEDTLS_ENTROPY_SOURCE_STRONG) != 0) {
        goto fail;
    }

    if (mbedtls_ctr_drbg_seed(&g_ctr, mbedtls_entropy_func, &g_entropy,
                              (const unsigned char *)"wolfram-wiiu", 12) != 0) {
        goto fail;
    }

    /* Reseeding would re-enter the poll, which has already wiped the seed and
     * would then fail closed mid-operation. The seed is good for this boot;
     * rotation across boots is the application's job. */
    mbedtls_ctr_drbg_set_prediction_resistance(&g_ctr, MBEDTLS_CTR_DRBG_PR_OFF);

    g_init = 1;
    return WF_OK;

fail:
    mbedtls_ctr_drbg_free(&g_ctr);
    mbedtls_entropy_free(&g_entropy);
    memset(g_seed, 0, sizeof(g_seed));
    g_seed_ready = 0;
    return WF_ERR_CRYPTO;
}

wf_status wf_wiiu_set_entropy_seed(const unsigned char *seed, size_t seed_len) {
    if (!seed || seed_len != SEED_SIZE || g_init) return WF_ERR_INVALID_ARG;
    memcpy(g_seed, seed, sizeof(g_seed));
    g_seed_ready = 1;
    return WF_OK;
}

wf_status wf_wiiu_rotate_entropy_seed(unsigned char *out, size_t out_len) {
    if (!out || out_len != SEED_SIZE || g_rotation_pending) {
        return WF_ERR_INVALID_ARG;
    }
    wf_status status = wiiu_drbg_init();
    if (status != WF_OK) return status;
    if (mbedtls_ctr_drbg_random(&g_ctr, out, out_len) != 0)
        return WF_ERR_CRYPTO;

    /* Fail closed until the caller confirms the new seed reached storage. If
     * the console loses power between here and the commit, the next boot must
     * not come up on the old seed and regenerate identical key material. */
    g_rotation_pending = 1;
    return WF_OK;
}

wf_status wf_wiiu_commit_entropy_rotation(void) {
    if (!g_init || !g_rotation_pending) return WF_ERR_INVALID_ARG;
    g_rotation_pending = 0;
    return WF_OK;
}

int wf_wiiu_entropy_ready(void) {
    return !g_rotation_pending && (g_init || g_seed_ready);
}

#include "wolfram/agent.h"

wf_status wf_wiiu_apply_tls_rng(wf_agent *agent) {
    if (!agent) return WF_ERR_INVALID_ARG;
    if (!wf_wiiu_entropy_ready()) return WF_ERR_CRYPTO;
    return wf_agent_set_tls_rng(agent, wii_tls_random, NULL);
}

/*
 * mbedTLS f_rng callback. Signature and semantics match wii_tls_random() in
 * src/transport/wii_tls.h so crypto_wii.c links unchanged against either.
 */
int wii_tls_random(void *p, unsigned char *out, size_t len) {
    (void)p;
    if (g_rotation_pending) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    if (!g_init) {
        if (wiiu_drbg_init() != WF_OK) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    return mbedtls_ctr_drbg_random(&g_ctr, out, len);
}
