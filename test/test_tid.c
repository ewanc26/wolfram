#include "wolfram/tid.h"
#include "wolfram/syntax.h"
#include "test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ── Hand-computed known values (s32 algorithm, 13 chars) ─────────────── */

static void test_known_values(void) {
    char buf[15];

    /* value 0 -> thirteen '2's (S32[0]). */
    WF_CHECK(wf_tid_from_time(0, 0, buf) == WF_OK);
    WF_CHECK(strcmp(buf, "2222222222222") == 0);

    /* ts=1, clk=0 -> value = (1<<10) = 1024 -> base32 [1,0,0] -> ..."322". */
    WF_CHECK(wf_tid_from_time(1, 0, buf) == WF_OK);
    WF_CHECK(strcmp(buf, "2222222222322") == 0);

    /* ts=0, clk=31 -> value = 31 -> lowest char 'z', rest '2'. */
    WF_CHECK(wf_tid_from_time(0, 31, buf) == WF_OK);
    WF_CHECK(strcmp(buf, "222222222222z") == 0);

    /* ts=0, clk=1023 -> value = 1023 = 31*32 + 31 -> lowest two chars 'zz'. */
    WF_CHECK(wf_tid_from_time(0, 1023, buf) == WF_OK);
    WF_CHECK(strcmp(buf, "22222222222zz") == 0);
}

/* ── encode / decode round-trip ───────────────────────────────────────── */

static void test_roundtrip(void) {
    uint64_t values[] = {
        0,
        1,
        31,
        1023,
        1024,
        (uint64_t)1 << 10,
        (uint64_t)1 << 53,            /* max 53-bit timestamp, clk 0 */
        (((uint64_t)1 << 53) - 1) << 10 | 1023, /* max 53-bit ts, clk max */
        UINT64_MAX >> 1,              /* large but bit 63 clear */
    };
    char buf[15];
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        WF_CHECK(wf_tid_encode(values[i], buf) == WF_OK);
        WF_CHECK(strlen(buf) == WF_TID_LEN);
        uint64_t back = 0;
        WF_CHECK(wf_tid_decode(buf, &back) == WF_OK);
        WF_CHECK(back == values[i]);
    }

    /* Malformed input rejected. */
    uint64_t dummy = 0;
    WF_CHECK(wf_tid_decode(NULL, &dummy) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_tid_decode("too_short", &dummy) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_tid_decode("222222222222!", &dummy) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_tid_decode("2222222222222", NULL) == WF_OK);
}

/* ── from_time: first-char class, length, validity ────────────────────── */

static void test_from_time_shape(void) {
    char buf[15];
    for (uint64_t ts = 0; ts < 5; ts++) {
        for (uint32_t clk = 0; clk <= 1023; clk++) {
            WF_CHECK(wf_tid_from_time(ts, clk, buf) == WF_OK);
            WF_CHECK(wf_syntax_tid_is_valid(buf) == 1);
            WF_CHECK(buf[0] != '\0' && strchr("234567abcdefghij", buf[0]) != NULL);
        }
    }
}

/* ── accessors recover what from_time put in ─────────────────────────── */

static void test_accessors(void) {
    char buf[15];
    uint64_t ts = 123456789012345ULL;
    uint32_t clk = 777;
    WF_CHECK(wf_tid_from_time(ts, clk, buf) == WF_OK);
    WF_CHECK(wf_tid_timestamp_micros(buf) == ts);
    WF_CHECK(wf_tid_clockid(buf) == clk);

    /* Invalid input -> 0. */
    WF_CHECK(wf_tid_timestamp_micros(NULL) == 0);
    WF_CHECK(wf_tid_clockid("bad") == 0);
}

/* ── monotonicity & uniqueness (single threaded) ─────────────────────── */

static void test_monotonic(void) {
    char prev[15] = {0};
    char cur[15];
    for (int i = 0; i < 5000; i++) {
        WF_CHECK(wf_tid_now(cur) == WF_OK);
        WF_CHECK(wf_syntax_tid_is_valid(cur) == 1);
        if (i > 0) {
            WF_CHECK(strcmp(cur, prev) > 0); /* strictly greater => unique */
        }
        memcpy(prev, cur, sizeof(prev));
    }
}

/* ── multithreaded uniqueness (exercises the mutex path) ──────────────── */

#define MT_THREADS 4
#define MT_PER_THREAD 2000
#define MT_TOTAL (MT_THREADS * MT_PER_THREAD)

static char g_ids[MT_TOTAL][15];
static size_t g_count = 0;
static pthread_mutex_t g_count_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *mt_worker(void *arg) {
    (void)arg;
    char last[15] = {0};
    for (int i = 0; i < MT_PER_THREAD; i++) {
        char cur[15];
        if (wf_tid_now(cur) != WF_OK) continue;
        if (i > 0) {
            /* Within a single thread the clock must still strictly advance. */
            if (strcmp(cur, last) <= 0) {
                fprintf(stderr, "FAIL: intra-thread non-monotonic %s !> %s\n",
                        cur, last);
                return (void *)(intptr_t)1;
            }
        }
        memcpy(last, cur, sizeof(last));

        pthread_mutex_lock(&g_count_mutex);
        size_t idx = g_count++;
        pthread_mutex_unlock(&g_count_mutex);
        memcpy(g_ids[idx], cur, 15);
    }
    return NULL;
}

static void test_multithreaded_unique(void) {
    pthread_t threads[MT_THREADS];
    for (int t = 0; t < MT_THREADS; t++) {
        pthread_create(&threads[t], NULL, mt_worker, NULL);
    }
    int failed = 0;
    for (int t = 0; t < MT_THREADS; t++) {
        void *rc = NULL;
        pthread_join(threads[t], &rc);
        if (rc) failed = 1;
    }
    WF_CHECK(!failed);
    WF_CHECK(g_count == MT_TOTAL);

    /* Global uniqueness: O(n^2) over MT_TOTAL strings is fine here. */
    for (size_t i = 0; i < g_count; i++) {
        for (size_t j = i + 1; j < g_count; j++) {
            if (strcmp(g_ids[i], g_ids[j]) == 0) {
                fprintf(stderr, "FAIL: duplicate TID %s across threads\n",
                        g_ids[i]);
                WF_CHECK(0);
                return;
            }
        }
    }
    WF_CHECK(1);
}

int main(void) {
    test_known_values();
    test_roundtrip();
    test_from_time_shape();
    test_accessors();
    test_monotonic();
    test_multithreaded_unique();
    WF_TEST_SUMMARY();
}
