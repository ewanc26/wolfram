/**
 * wiiu_platform.c — Nintendo Wii U platform implementation (devkitPPC/wut).
 *
 * Maps the platform abstraction onto wut primitives:
 *   - init/shutdown → nsysnet's socket library
 *   - mutex         → coreinit OSMutex
 *   - time          → OSGetTime, converted to microseconds since the UNIX epoch
 *
 * Build with devkitPPC against wut (-lwut).
 *
 * Network connection ownership
 * ----------------------------
 * Unlike the Wii, where net_init() both brings up the link and initialises
 * sockets, the Wii U splits these — and wut already owns the connection half.
 *
 * wut's socket devoptab initialiser (__init_wut_socket, pulled into the link
 * as soon as anything references sockets) runs socket_lib_init(), AddDevice()
 * and then ACInitialize() followed by ACConnectAsync(). Its counterpart
 * __fini_wut_socket calls ACClose() and ACFinalize() at exit.
 *
 * Wolfram must therefore NOT touch nn::ac: calling ACInitialize/ACFinalize
 * here would double-finalize against wut's own teardown. socket_lib_init() is
 * still called below because wut only reaches it when the socket devoptab is
 * linked in, which is not guaranteed for an embedder that pulls in Wolfram
 * without otherwise using sockets. Calling it a second time is safe — it is
 * the same idempotent RPL export wut invokes unconditionally at startup.
 *
 * Deciding whether the link is actually up (ACIsApplicationConnected) stays
 * with the application, which is where the "no network" UI lives anyway.
 */

#include "wolfram/platform.h"

#include <coreinit/mutex.h>
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
/* socket_lib_init/finish live in the raw nsysnet header; nsysnet/socket.h is
 * a deprecated compatibility shim that no longer declares them. */
#include <nsysnet/_socket.h>

#include <stdlib.h>

/* ── Init / shutdown ────────────────────────────────────────────────── */

static int wf_socket_initialized = 0;

wf_status wf_platform_init(void) {
    if (wf_socket_initialized) return WF_OK;
    /* socket_lib_init() returns void; there is no failure to report. */
    socket_lib_init();
    wf_socket_initialized = 1;
    return WF_OK;
}

void wf_platform_shutdown(void) {
    if (!wf_socket_initialized) return;
    socket_lib_finish();
    wf_socket_initialized = 0;
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

/*
 * OSMutex rather than OSSemaphore: it is the real mutual-exclusion primitive
 * on this platform, it is recursive for the owning thread, and it needs no
 * explicit destroy call.
 */
struct wf_platform_mutex { OSMutex handle; };

wf_platform_mutex *wf_platform_mutex_new(void) {
    wf_platform_mutex *mutex = calloc(1, sizeof(*mutex));
    if (!mutex) return NULL;
    OSInitMutex(&mutex->handle);
    return mutex;
}

void wf_platform_mutex_lock(wf_platform_mutex *m) {
    if (m) OSLockMutex(&m->handle);
}

void wf_platform_mutex_unlock(wf_platform_mutex *m) {
    if (m) OSUnlockMutex(&m->handle);
}

void wf_platform_mutex_free(wf_platform_mutex *m) {
    /* OSMutex has no destructor; it is a plain structure the OS links into a
     * thread's owned-mutex list only while the mutex is held. */
    free(m);
}

/* ── Time ───────────────────────────────────────────────────────────── */

/* Seconds between the UNIX epoch and the Wii U's 2000-01-01 OSTime epoch. */
#define WF_WIIU_EPOCH_OFFSET_SECONDS 946684800ull

/*
 * OSGetTime() counts timer ticks since 2000-01-01, so by now it is on the
 * order of 5e16. wut's OSTicksToMicroseconds() multiplies by 1000000 before
 * dividing, which overflows uint64 well before that (5e16 * 1e6 = 5e22 against
 * a ceiling of 1.8e19) and would return a wrapped, non-monotonic value. The
 * conversion is therefore done divide-first, keeping the remainder so
 * sub-second resolution survives.
 *
 * OSGetSystemTime() (ticks since boot) would sidestep the overflow but is the
 * wrong clock: TIDs are record keys that encode a real timestamp and have to
 * sort correctly against records written by other clients, so a boot-relative
 * clock would place every record written here at the start of 1970.
 */
uint64_t wf_platform_time_micros(void) {
    const uint64_t ticks_per_second = (uint64_t)OSTimerClockSpeed;
    if (ticks_per_second == 0) return 0;

    const uint64_t ticks = (uint64_t)OSGetTime();
    const uint64_t seconds = ticks / ticks_per_second;
    const uint64_t remainder = ticks % ticks_per_second;

    return (seconds + WF_WIIU_EPOCH_OFFSET_SECONDS) * 1000000ull
           + (remainder * 1000000ull) / ticks_per_second;
}
