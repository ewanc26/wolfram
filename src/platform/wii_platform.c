/**
 * wii_platform.c — Nintendo Wii platform implementation (libogc stubs).
 *
 * Maps platform abstractions to libogc primitives. Video and filesystem
 * setup remain application concerns; Wolfram owns only networking, locking,
 * and its monotonic clock.
 *
 * Build with devkitPPC: powerpc-eabi-gcc, linked against libogc.
 */

#include "wolfram/platform.h"

#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <stdlib.h>

/* ── Init / shutdown ────────────────────────────────────────────────── */

static int wf_network_initialized = 0;

wf_status wf_platform_init(void) {
    if (wf_network_initialized) return WF_OK;
    if (net_init() < 0) return WF_ERR_NETWORK;
    wf_network_initialized = 1;
    return WF_OK;
}

void wf_platform_shutdown(void) {
    if (!wf_network_initialized) return;
    net_deinit();
    wf_network_initialized = 0;
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

struct wf_platform_mutex { mutex_t handle; };

wf_platform_mutex *wf_platform_mutex_new(void) {
    wf_platform_mutex *mutex = calloc(1, sizeof(*mutex));
    if (!mutex) return NULL;
    if (LWP_MutexInit(&mutex->handle, false) < 0) {
        free(mutex);
        return NULL;
    }
    return mutex;
}

void wf_platform_mutex_lock(wf_platform_mutex *m) {
    if (m) LWP_MutexLock(m->handle);
}

void wf_platform_mutex_unlock(wf_platform_mutex *m) {
    if (m) LWP_MutexUnlock(m->handle);
}

void wf_platform_mutex_free(wf_platform_mutex *m) {
    if (!m) return;
    LWP_MutexDestroy(m->handle);
    free(m);
}

/* ── Time ───────────────────────────────────────────────────────────── */

uint64_t wf_platform_time_micros(void) {
    return ticks_to_microsecs(gettime());
}
