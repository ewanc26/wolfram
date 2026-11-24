/**
 * windows_platform.c — Windows platform implementation (MinGW / Win32).
 *
 * Maps platform abstractions to Win32 APIs. Used when building for Windows
 * natively (MSVC/MinGW) or cross-compiling from Linux/macOS via MinGW-w64.
 */

#ifdef _WIN32

#include "wolfram/platform.h"

/* <winsock2.h> must come before <windows.h>, which otherwise pulls in the
 * conflicting Winsock 1 declarations. */
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>

/* ── Init / shutdown ────────────────────────────────────────────────── */

wf_status wf_platform_init(void) {
    WSADATA wsa;
    return (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? WF_OK : WF_ERR_NETWORK;
}

void wf_platform_shutdown(void) {
    WSACleanup();
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

struct wf_platform_mutex {
    CRITICAL_SECTION cs;
};

wf_platform_mutex *wf_platform_mutex_new(void) {
    wf_platform_mutex *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    InitializeCriticalSection(&m->cs);
    return m;
}

void wf_platform_mutex_lock(wf_platform_mutex *m) {
    if (m) EnterCriticalSection(&m->cs);
}

void wf_platform_mutex_unlock(wf_platform_mutex *m) {
    if (m) LeaveCriticalSection(&m->cs);
}

void wf_platform_mutex_free(wf_platform_mutex *m) {
    if (!m) return;
    DeleteCriticalSection(&m->cs);
    free(m);
}

/* ── Time ───────────────────────────────────────────────────────────── */

/* FILETIME counts 100 ns ticks since 1601-01-01; UNIX time starts this many
 * ticks later. */
#define WF_FILETIME_TICKS_PER_MICRO 10u
#define WF_FILETIME_UNIX_EPOCH_TICKS 116444736000000000ull

uint64_t wf_platform_time_micros(void) {
    /* UNIX time, not the performance counter. The counter starts at boot, and
     * this value becomes the timestamp half of a TID (wii_tid.c): a TID minted
     * from time-since-boot sorts decades before every record already in a
     * repository. Callers that need the clock never to step backwards enforce
     * that themselves, as wf_tid_now() does. */
    FILETIME ft;
    ULARGE_INTEGER ticks;

    GetSystemTimePreciseAsFileTime(&ft);
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;
    if (ticks.QuadPart < WF_FILETIME_UNIX_EPOCH_TICKS) return 0;
    return (ticks.QuadPart - WF_FILETIME_UNIX_EPOCH_TICKS) /
           WF_FILETIME_TICKS_PER_MICRO;
}

#endif /* _WIN32 */
