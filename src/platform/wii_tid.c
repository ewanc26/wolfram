/*
 * wii_tid.c — TID generation clock (Wii implementation).
 *
 * Same logic as tid.c but uses the platform abstraction for time and mutex
 * instead of POSIX gettimeofday() and pthreads.
 */

#include "wolfram/tid.h"
#include "wolfram/platform.h"

#include <stdlib.h>
#include <string.h>

/* S32 alphabet (index 0..31). */
static const char WF_TID_S32[] = "234567abcdefghijklmnopqrstuvwxyz";

static int wf_tid_s32_index(char c) {
    for (int i = 0; i < 32; i++) {
        if (WF_TID_S32[i] == c) return i;
    }
    return -1;
}

wf_status wf_tid_encode(uint64_t tid_value, char out[15]) {
    if (!out) return WF_ERR_INVALID_ARG;
    uint64_t v = tid_value;
    for (int i = WF_TID_LEN - 1; i >= 0; i--) {
        out[i] = WF_TID_S32[v & 31];
        v >>= 5;
    }
    out[WF_TID_LEN] = '\0';
    return WF_OK;
}

wf_status wf_tid_decode(const char *tid, uint64_t *out_value) {
    if (!tid || strlen(tid) != WF_TID_LEN) return WF_ERR_INVALID_ARG;
    uint64_t v = 0;
    for (int i = 0; i < WF_TID_LEN; i++) {
        int idx = wf_tid_s32_index(tid[i]);
        if (idx < 0) return WF_ERR_INVALID_ARG;
        v = (v << 5) | (uint64_t)(unsigned)idx;
    }
    if (out_value) *out_value = v;
    return WF_OK;
}

wf_status wf_tid_from_time(uint64_t timestamp_micros, uint32_t clockid,
                           char out[15]) {
    if (!out) return WF_ERR_INVALID_ARG;
    uint64_t value = (timestamp_micros << 10) | ((uint64_t)(clockid & 0x3FF));
    return wf_tid_encode(value, out);
}

/* ── Monotonic process-wide clock ─────────────────────────────────────── */

static wf_platform_mutex *wf_tid_lock = NULL;
static uint64_t wf_tid_last_ts = 0;
static uint32_t wf_tid_clock = 0;
static int wf_tid_initialized = 0;

/*
 * TODO: When wii_platform.c implements wf_platform_time_micros() with
 * libogc's gettime()/ticks_to_microsecs(), this will produce real
 * microsecond timestamps. Currently returns 0 (stub), so TIDs will
 * sort by clockid only.
 */
static uint64_t wf_tid_now_micros(void) {
    return wf_platform_time_micros();
}

wf_status wf_tid_now(char out[15]) {
    if (!out) return WF_ERR_INVALID_ARG;

    /* Lazy-init the mutex on first call. */
    if (!wf_tid_lock) {
        wf_tid_lock = wf_platform_mutex_new();
        if (!wf_tid_lock) return WF_ERR_ALLOC;
    }

    wf_platform_mutex_lock(wf_tid_lock);
    if (!wf_tid_initialized) {
        wf_tid_clock = (uint32_t)rand() & 0x3FF;
        wf_tid_initialized = 1;
    }

    uint64_t now = wf_tid_now_micros();
    if (now <= wf_tid_last_ts) now = wf_tid_last_ts + 1;
    wf_tid_last_ts = now;

    uint64_t value = (now << 10) | ((uint64_t)(wf_tid_clock & 0x3FF));
    wf_platform_mutex_unlock(wf_tid_lock);

    return wf_tid_encode(value, out);
}

uint64_t wf_tid_timestamp_micros(const char *tid) {
    uint64_t value = 0;
    if (wf_tid_decode(tid, &value) != WF_OK) return 0;
    return value >> 10;
}

uint32_t wf_tid_clockid(const char *tid) {
    uint64_t value = 0;
    if (wf_tid_decode(tid, &value) != WF_OK) return 0;
    return (uint32_t)(value & 0x3FF);
}
