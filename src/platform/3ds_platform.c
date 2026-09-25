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

/* libctru's socInit() takes a caller-owned context buffer. 0x1000 is the size
 * libctru's own examples pass. */
static u32 soc_ctx[0x1000 / sizeof(u32)];

wf_status wf_platform_init(void) {
    /* Only the socket layer is needed here. The 3DS transport is the shared
     * libcurl one (see WOLFRAM_USE_SOCKET_TRANSPORT in CMakeLists.txt), and
     * devkitPro's 3DS libcurl talks to libctru's sockets directly, so the HTTPC
     * service is deliberately not started. */
    if (socInit(soc_ctx, sizeof(soc_ctx)) < 0) return WF_ERR_NETWORK;
    return WF_OK;
}

void wf_platform_shutdown(void) {
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
    /* libctru 2.x exposes no destroy/teardown for LightLock; it is a bare
     * futex word with no allocated resources, so freeing the wrapper is the
     * whole teardown. */
    if (!m) return;
    free(m);
}

/* ── Time ───────────────────────────────────────────────────────────── */

uint64_t wf_platform_time_micros(void) {
    return (uint64_t)osGetTime() * 1000u;
}