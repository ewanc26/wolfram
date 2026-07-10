/**
 * wii_platform.c — Nintendo Wii platform implementation (libogc stubs).
 *
 * Maps platform abstractions to libogc primitives. Currently contains
 * stub implementations; TODO comments indicate what needs to be filled
 * in when the Wii integration is built out.
 *
 * Build with devkitPPC: powerpc-eabi-gcc, linked against libogc.
 */

#include "wolfram/platform.h"

#include <stdlib.h>
#include <string.h>

/* TODO: #include <ogc/lwp.h>   — for LWP mutex */
/* TODO: #include <ogc/lwp_mutex.h> */
/* TODO: #include <ogc/video.h>  — for VIDEO_Init */
/* TODO: #include <network.h>    — for net_init */

/* ── Init / shutdown ────────────────────────────────────────────────── */

wf_status wf_platform_init(void) {
    /*
     * TODO: Implement when building the Wii integration:
     *   1. VIDEO_Init()
     *   2. net_init() — lwIP TCP/IP stack
     *   3. Initialise SSL/TLS context (mbedTLS)
     *   4. Mount SD card (fatMountSimple)
     */
    return WF_ERR_NOT_IMPLEMENTED;
}

void wf_platform_shutdown(void) {
    /*
     * TODO: Implement when building the Wii integration:
     *   1. net_shutdown()
     *   2. fatUnmount("sd:")
     */
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

/*
 * TODO: Replace with lwp_mutex from libogc:
 *   #include <ogc/lwp_mutex.h>
 *
 *   struct wf_platform_mutex { mutex_t mtx; };
 *
 *   wf_platform_mutex *wf_platform_mutex_new(void) {
 *       wf_platform_mutex *m = calloc(1, sizeof(*m));
 *       if (!m) return NULL;
 *       LWP_MutexInit(&m->mtx, false);
 *       return m;
 *   }
 *
 *   void wf_platform_mutex_lock(wf_platform_mutex *m) {
 *       if (m) LWP_MutexLock(m->mtx);
 *   }
 *
 *   void wf_platform_mutex_unlock(wf_platform_mutex *m) {
 *       if (m) LWP_MutexUnlock(m->mtx);
 *   }
 *
 *   void wf_platform_mutex_free(wf_platform_mutex *m) {
 *       if (!m) return;
 *       LWP_MutexDestroy(m->mtx);
 *       free(m);
 *   }
 */

struct wf_platform_mutex { int dummy; };

wf_platform_mutex *wf_platform_mutex_new(void) {
    /* Stub: no real backend yet — signal failure honestly. */
    return NULL;
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
 * TODO: Replace with libogc timer:
 *   #include <ogc/lwp_watchdog.h>
 *
 *   uint64_t wf_platform_time_micros(void) {
 *       // gettime() returns TB ticks; ticks_to_microsecs() converts
 *       return ticks_to_microsecs(gettime());
 *   }
 *
 * The Wii's Timebase (TB) counter runs at 162.5 MHz on NTSC (144 MHz PAL)
 * and provides monotonically increasing ticks. ticks_to_microsecs()
 * from libogc handles the conversion.
 */

uint64_t wf_platform_time_micros(void) {
    /* Stub: return 0. TID generation will not produce unique values
     * until this is implemented. */
    return 0;
}
