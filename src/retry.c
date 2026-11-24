/* retry.c -- bounded retry decisions and delays (see retry.h). C89. */

#include "wolfram/retry.h"

#include <limits.h>

int wf_retry_should_retry(wf_failure_kind kind, unsigned int attempts,
                          const wf_retry_policy *policy) {
    if (policy == NULL || policy->max_attempts < 2) return 0;
    if (attempts >= policy->max_attempts) return 0;

    switch (kind) {
        case WF_FAIL_RATE_LIMIT:
        case WF_FAIL_NETWORK:
        case WF_FAIL_TIMEOUT:
        case WF_FAIL_SERVER:
            return 1;
        default:
            return 0;
    }
}

long wf_retry_delay_ms(const wf_retry_policy *policy, unsigned int attempts,
                       long rate_limit_reset_ms, unsigned int jitter_seed) {
    unsigned int i;
    unsigned int percent;
    long delay;
    long spread;
    long offset;

    if (policy == NULL || policy->base_delay_ms <= 0) return 0;
    if (attempts < 1) attempts = 1;

    delay = policy->base_delay_ms;
    for (i = 1; i < attempts; i++) {
        if (delay > LONG_MAX / 2) {
            delay = LONG_MAX;
            break;
        }
        delay *= 2;
    }
    if (policy->max_delay_ms > 0 && delay > policy->max_delay_ms)
        delay = policy->max_delay_ms;

    percent = policy->jitter_percent;
    if (percent > 100) percent = 100;
    if (percent > 0) {
        spread = (delay / 100) * (long)percent +
                 ((delay % 100) * (long)percent) / 100;
        if (spread < 0 || spread == LONG_MAX)
            offset = 0;
        else
            offset = (long)(jitter_seed % (unsigned long)(spread + 1));
        if (delay > LONG_MAX - offset)
            delay = LONG_MAX;
        else
            delay += offset;
        if (policy->max_delay_ms > 0 && delay > policy->max_delay_ms)
            delay = policy->max_delay_ms;
    }

    if (rate_limit_reset_ms > delay) delay = rate_limit_reset_ms;
    return delay;
}
