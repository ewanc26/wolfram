/**
 * windows_platform.c — Windows platform implementation (MinGW / Win32).
 *
 * Maps platform abstractions to Win32 APIs. Used when building for Windows
 * natively (MSVC/MinGW) or cross-compiling from Linux/macOS via MinGW-w64.
 */

#ifdef _WIN32

#include "wolfram/platform.h"

#include <windows.h>
#include <winsock2.h>
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

uint64_t wf_platform_time_micros(void) {
    /* QueryPerformanceCounter provides sub-microsecond resolution. */
    static LARGE_INTEGER freq = {0};
    static int have_freq = 0;
    LARGE_INTEGER now;

    if (!have_freq) {
        QueryPerformanceFrequency(&freq);
        have_freq = 1;
    }
    QueryPerformanceCounter(&now);
    return (uint64_t)now.QuadPart * 1000000u / (uint64_t)freq.QuadPart;
}

#endif /* _WIN32 */
