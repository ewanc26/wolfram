/**
 * test.h — a deliberately tiny test harness.
 *
 * No framework dependency: assert-and-report macros, a shared pass/
 * fail counter, and a summary printed by WF_TEST_MAIN. Good enough
 * for a scaffold; swap in something heavier (Criterion, Unity) if
 * the suite outgrows this.
 */

#ifndef WOLFRAM_TEST_H
#define WOLFRAM_TEST_H

#include <stdio.h>

static int wf_test_pass_count = 0;
static int wf_test_fail_count = 0;

#define WF_CHECK(cond)                                                       \
    do {                                                                     \
        if (cond) {                                                         \
            wf_test_pass_count++;                                           \
        } else {                                                            \
            wf_test_fail_count++;                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define WF_TEST_SUMMARY()                                                    \
    do {                                                                     \
        printf("%d passed, %d failed\n", wf_test_pass_count,                \
               wf_test_fail_count);                                          \
        return wf_test_fail_count == 0 ? 0 : 1;                             \
    } while (0)

#endif /* WOLFRAM_TEST_H */
