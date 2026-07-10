/**
 * posix_platform.c — POSIX platform implementation (Linux, macOS).
 *
 * Maps platform abstractions to standard POSIX APIs. This is the default
 * when building for desktop targets.
 */

#include "wolfram/platform.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>

/* ── Init / shutdown ────────────────────────────────────────────────── */

wf_status wf_platform_init(void) {
    return WF_OK;
}

void wf_platform_shutdown(void) {
    /* No-op on POSIX. */
}

/* ── Mutex ──────────────────────────────────────────────────────────── */

struct wf_platform_mutex {
    pthread_mutex_t mtx;
};

wf_platform_mutex *wf_platform_mutex_new(void) {
    wf_platform_mutex *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    pthread_mutex_init(&m->mtx, NULL);
    return m;
}

void wf_platform_mutex_lock(wf_platform_mutex *m) {
    if (m) pthread_mutex_lock(&m->mtx);
}

void wf_platform_mutex_unlock(wf_platform_mutex *m) {
    if (m) pthread_mutex_unlock(&m->mtx);
}

void wf_platform_mutex_free(wf_platform_mutex *m) {
    if (!m) return;
    pthread_mutex_destroy(&m->mtx);
    free(m);
}

/* ── Time ───────────────────────────────────────────────────────────── */

uint64_t wf_platform_time_micros(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000u + (uint64_t)tv.tv_usec;
}
