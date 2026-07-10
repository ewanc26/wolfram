/**
 * wiiu_platform.c — Nintendo Wii U platform implementation (wut stubs).
 *
 * Maps platform abstractions to wut (Wii U Toolchain) primitives.
 * Currently contains stub implementations; TODO comments indicate what
 * needs to be filled in when the Wii U integration is built out.
 *
 * Build with devkitPPC: powerpc-eabi-gcc, linked against wut.
 */

#include "wolfram/platform.h"

#include <stdlib.h>
#include <string.h>

/* TODO: #include <coreinit/thread.h>   — for OSJoinThread, OSSemaphore */
/* TODO: #include <coreinit/time.h>      — for OSGetTime */
/* TODO: #include <nsysnet/socket.h>     — for socket (BSD-like) */
/* TODO: #include <nn/ssl.h>             — for SSL context */

/* ── Init / shutdown ────────────────────────────────────────────────── */

wf_status wf_platform_init(void) {
    /*
     * TODO: Implement when building the Wii U integration:
     *   1. FSInit() — filesystem
     *   2. socketInit() — networking
     *   3. Initialise SSL/TLS context
     *   4. Initialise AX audio (if needed)
     */
    return WF_OK;
}

void wf_platform_shutdown(void) {
    /*
     * TODO: Implement when building the Wii U integration:
     *   1. socketShutdown()
     *   2. FSShutdown()
     */
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

/*
 * TODO: Replace with wut OSSemaphore:
 *   #include <coreinit/semaphore.h>
 *
 *   struct wf_platform_mutex { OSSemaphore sem; };
 *
 *   wf_platform_mutex *wf_platform_mutex_new(void) {
 *       wf_platform_mutex *m = calloc(1, sizeof(*m));
 *       if (!m) return NULL;
 *       OSSemaphoreInit(&m->sem, 1);
 *       return m;
 *   }
 *
 *   void wf_platform_mutex_lock(wf_platform_mutex *m) {
 *       if (m) OSSemaphoreAcquire(&m->sem, 1);
 *   }
 *
 *   void wf_platform_mutex_unlock(wf_platform_mutex *m) {
 *       if (m) OSSemaphoreRelease(&m->sem);
 *   }
 *
 *   void wf_platform_mutex_free(wf_platform_mutex *m) {
 *       if (!m) return;
 *       OSSemaphoreDestroy(&m->sem);
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
 * TODO: Replace with wut timer:
 *   #include <coreinit/time.h>
 *
 *   uint64_t wf_platform_time_micros(void) {
 *       // OSGetTime() returns ticks since boot; OSTicksToMicroseconds() converts
 *       return OSTicksToMicroseconds(OSGetTime());
 *   }
 */

uint64_t wf_platform_time_micros(void) {
    /* Stub: return 0 until wut timer is wired up. */
    return 0;
}
