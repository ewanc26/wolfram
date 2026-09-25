/**
 * 3ds_random.c — Hardware-seeded DRBG for the Nintendo 3DS.
 *
 * Provides wii_tls_random(), the mbedTLS-compatible f_rng callback that
 * crypto_wii.c uses for P-256 key generation and ECDSA signing.
 *
 * On the Wii that symbol comes from wii_tls.c, which also implements the
 * socket/TLS transport on top of libogc. The 3DS does not build that file: it
 * uses the curl-based transport instead (see WOLFRAM_USE_SOCKET_TRANSPORT in
 * CMakeLists.txt), and wii_tls.c is written against libogc's net_* API
 * regardless. So the RNG half is provided separately here, exactly as
 * wiiu_random.c does for the Wii U.
 *
 * Why the 3DS does not need an application-provisioned seed
 * ---------------------------------------------------------
 * The Wii and the Wii U fail closed because devkitPro's mbedTLS builds their
 * only entropy source as srand(OSGetSystemTick()) followed by rand() -- a
 * timer-seeded libc PRNG whose whole state is the console's tick counter at the
 * moment of the call. The 3DS portlib is different: mbedtls_hardware_poll
 * forwards to sslcGenerateRandomData(), the SSLC service's
 * ps:ps GenerateRandomData, which the console backs with hardware. So the
 * standard mbedtls_entropy_func() -> mbedtls_ctr_drbg_seed() chain is seeded
 * from real entropy here and no seed has to be supplied by the application.
 */

#include "wolfram/3ds.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>

#include <3ds.h>

/* No underscore prefix, so the translation unit keeps external linkage and the
 * .c is not force-linked in a static archive. crypto_wii.c references this
 * symbol directly, and libctru provides a separate _unwind variant of
 * pthread_mutex_lock that the prefixed macro would otherwise collide with. */
#include <pthread.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr;
static int g_init;

static int threeds_drbg_init(void) {
    if (g_init) return 0;

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr);

    /* mbedtls_entropy_func() is mbedtls_hardware_poll() on this portlib, which
     * is sslcGenerateRandomData(). MBEDTLS_ENTROPY_SOURCE_STRONG is
     * appropriate because the portlib marks the ALT hardware source strong and
     * there is no weaker fallback in the chain. */
    if (mbedtls_ctr_drbg_seed(&g_ctr, mbedtls_entropy_func, &g_entropy,
                              (const unsigned char *)"wolfram-3ds", 11) != 0) {
        mbedtls_ctr_drbg_free(&g_ctr);
        mbedtls_entropy_free(&g_entropy);
        return -1;
    }

    g_init = 1;
    return 0;
}

/*
 * mbedTLS f_rng callback. Signature and semantics match wii_tls_random() in
 * src/transport/wii_tls.h so crypto_wii.c links unchanged against the Wii, the
 * Wii U, and the 3DS. Callers may reach this from several threads (the agent
 * worker pool signs on a background thread), and mbedtls_ctr_drbg_context is
 * not reentrant, so the whole generate call is serialised.
 */
int wii_tls_random(void *p, unsigned char *out, size_t len) {
    (void)p;
    pthread_mutex_lock(&g_lock);
    if (!g_init && threeds_drbg_init() != 0) {
        pthread_mutex_unlock(&g_lock);
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    int ret = mbedtls_ctr_drbg_random(&g_ctr, out, len);
    pthread_mutex_unlock(&g_lock);
    return ret;
}

int wf_3ds_entropy_ready(void) {
    pthread_mutex_lock(&g_lock);
    int ready = g_init;
    pthread_mutex_unlock(&g_lock);
    return ready;
}

#include "wolfram/agent.h"

wf_status wf_3ds_apply_tls_rng(wf_agent *agent) {
    if (!agent) return WF_ERR_INVALID_ARG;
    wf_status status = wf_agent_set_tls_rng(agent, wii_tls_random, NULL);
    if (status == WF_OK) return WF_OK;
    /* A build whose libcurl is not linked against mbedTLS rejects the hook.
     * That is not a failure: devkitPro's 3DS libcurl already draws TLS entropy
     * from the same hardware source, so the handshake is still sound. */
    if (status == WF_ERR_UNSUPPORTED) return WF_OK;
    return status;
}
