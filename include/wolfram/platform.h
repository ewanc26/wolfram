/**
 * platform.h — Platform abstraction layer for wolfram.
 *
 * Provides thin wrappers around platform-specific primitives (time, mutex,
 * init/shutdown) so the rest of the SDK can be compiled for both desktop
 * (Linux/macOS) and embedded targets (Nintendo Wii via devkitPPC/libogc).
 *
 * On desktop, these map to POSIX APIs. On Wii, they map to libogc primitives.
 * The public API is identical across platforms.
 */

#ifndef WOLFRAM_PLATFORM_H
#define WOLFRAM_PLATFORM_H

#include "wolfram/xrpc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Platform detection ──────────────────────────────────────────────── */

#if defined(__WII__)
  #define WOLFRAM_PLATFORM_WII 1
#elif defined(__APPLE__)
  #define WOLFRAM_PLATFORM_MACOS 1
  #define WOLFRAM_PLATFORM_POSIX 1
#elif defined(__linux__)
  #define WOLFRAM_PLATFORM_LINUX 1
  #define WOLFRAM_PLATFORM_POSIX 1
#else
  #define WOLFRAM_PLATFORM_POSIX 1
#endif

/* ── Init / shutdown ────────────────────────────────────────────────── */

/**
 * One-time platform initialisation.
 *
 * Desktop: no-op (returns WF_OK).
 * Wii: will initialise net_init(), VIDEO_Init(), etc.
 */
wf_status wf_platform_init(void);

/**
 * Platform cleanup. Call once at exit.
 */
void wf_platform_shutdown(void);

/* ── Mutex ──────────────────────────────────────────────────────────── */

/** Opaque platform mutex. */
typedef struct wf_platform_mutex wf_platform_mutex;

/** Allocate a new mutex. Free with wf_platform_mutex_free. */
wf_platform_mutex *wf_platform_mutex_new(void);

/** Lock the mutex (blocking). */
void wf_platform_mutex_lock(wf_platform_mutex *m);

/** Unlock the mutex. */
void wf_platform_mutex_unlock(wf_platform_mutex *m);

/** Free a mutex. */
void wf_platform_mutex_free(wf_platform_mutex *m);

/* ── Time ───────────────────────────────────────────────────────────── */

/**
 * Return the current wall-clock time in microseconds since an
 * arbitrary epoch (need not be UNIX time).
 *
 * The value must be monotonically non-decreasing within a process.
 * Used by TID generation and any timestamping that needs sub-second
 * resolution.
 */
uint64_t wf_platform_time_micros(void);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_PLATFORM_H */
