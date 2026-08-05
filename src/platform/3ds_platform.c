/**
 * 3ds_platform.c — Nintendo 3DS platform implementation (libctru).
 *
 * Maps platform abstractions to libctru primitives.
 * Build with devkitARM: arm-none-eabi-gcc, linked against libctru.
 */

#include "wolfram/platform.h"

#include <stdlib.h>
#include <string.h>

#include <3ds.h>
#include <3ds/services/soc.h>
#include <3ds/synchronization.h>
#include <3ds/os.h>

/* ── Init / shutdown ────────────────────────────────────────────────── */

wf_status wf_platform_init(void) {
    if (socInit() < 0) return WF_ERR_NETWORK;
    if (httpcInit(0) != 0) {
        socExit();
        return WF_ERR_NETWORK;
    }
    return WF_OK;
}

void wf_platform_shutdown(void) {
    httpcExit();
    socExit();
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

struct wf_platform_mutex {
    LightLock lock;
};

wf_platform_mutex *wf_platform_mutex_new(void) {
    wf_platform_mutex *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    LightLock_Init(&m->lock);
    return m;
}

void wf_platform_mutex_lock(wf_platform_mutex *m) {
    if (m) LightLock_Lock(&m->lock);
}

void wf_platform_mutex_unlock(wf_platform_mutex *m) {
    if (m) LightLock_Unlock(&m->lock);
}

void wf_platform_mutex_free(wf_platform_mutex *m) {
    if (!m) return;
    LightLock_Destroy(&m->lock);
    free(m);
}

/* ── Time ───────────────────────────────────────────────────────────── */

uint64_t wf_platform_time_micros(void) {
    return (uint64_t)osGetTime() * 1000u;
}