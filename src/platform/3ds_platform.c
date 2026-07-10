/**
 * 3ds_platform.c — Nintendo 3DS platform implementation (libctru stubs).
 *
 * Maps platform abstractions to libctru primitives.
 * Currently contains stub implementations; TODO comments indicate what
 * needs to be filled in when the 3DS integration is built out.
 *
 * Build with devkitARM: arm-none-eabi-gcc, linked against libctru.
 */

#include "wolfram/platform.h"

#include <stdlib.h>
#include <string.h>

/* TODO: #include <3ds.h>               — master libctru header */
/* TODO: #include <3ds/services/soc.h>   — for BSD socket API */
/* TODO: #include <3ds/synchronization.h> — for LightLock */
/* TODO: #include <3ds/os.h>            - for osGetTime */

/* ── Init / shutdown ────────────────────────────────────────────────── */

wf_status wf_platform_init(void) {
    /*
     * TODO: Implement when building the 3DS integration:
     *   1. gfxInitDefault() — GPU/Graphics
     *   2. socInit() — BSD socket API over soc:u service
     *   3. httpcInit() — HTTPC service for HTTPS
     *   4. Initialise SSL context (via mbedtls or httpc SSL)
     */
    return WF_OK;
}

void wf_platform_shutdown(void) {
    /*
     * TODO: Implement when building the 3DS integration:
     *   1. httpcExit()
     *   2. socExit()
     *   3. gfxExit()
     */
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

/*
 * TODO: Replace with libctru LightLock:
 *   #include <3ds/synchronization.h>
 *
 *   struct wf_platform_mutex { LightLock lock; };
 *
 *   wf_platform_mutex *wf_platform_mutex_new(void) {
 *       wf_platform_mutex *m = calloc(1, sizeof(*m));
 *       if (!m) return NULL;
 *       LightLock_Init(&m->lock);
 *       return m;
 *   }
 *
 *   void wf_platform_mutex_lock(wf_platform_mutex *m) {
 *       if (m) LightLock_Lock(&m->lock);
 *   }
 *
 *   void wf_platform_mutex_unlock(wf_platform_mutex *m) {
 *       if (m) LightLock_Unlock(&m->lock);
 *   }
 *
 *   void wf_platform_mutex_free(wf_platform_mutex *m) {
 *       if (!m) return;
 *       LightLock_Destroy(&m->lock);
 *       free(m);
 *   }
 */

struct wf_platform_mutex { int dummy; };

wf_platform_mutex *wf_platform_mutex_new(void) {
    return calloc(1, sizeof(wf_platform_mutex));
}

void wf_platform_mutex_lock(wf_platform_mutex *m) {
    (void)m;
}

void wf_platform_mutex_unlock(wf_platform_mutex *m) {
    (void)m;
}

void wf_platform_mutex_free(wf_platform_mutex *m) {
    free(m);
}

/* ── Time ───────────────────────────────────────────────────────────── */

/*
 * TODO: Replace with libctru timer:
 *   #include <3ds/os.h>
 *
 *   uint64_t wf_platform_time_micros(void) {
 *       // osGetTime() returns milliseconds since 2000-01-01
 *       return (uint64_t)osGetTime() * 1000u;
 *   }
 */

uint64_t wf_platform_time_micros(void) {
    /* Stub: return 0 until libctru timer is wired up. */
    return 0;
}
