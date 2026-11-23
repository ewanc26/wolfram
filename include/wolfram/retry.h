/*
 * retry.h -- a bounded retry schedule a caller drives itself.
 *
 * The library never sleeps and never owns a clock here: these functions
 * only answer "is this failure worth another attempt?" and "how long should
 * the caller wait?". That keeps the policy deterministic in tests, usable
 * on a console with its own timer, and free of threads.
 *
 * Retryable failures are the ones a later attempt can plausibly fix:
 * WF_FAIL_RATE_LIMIT, WF_FAIL_NETWORK, WF_FAIL_TIMEOUT and WF_FAIL_SERVER.
 * A refused credential, a TLS failure, a malformed response or a missing
 * feature is not retried.
 *
 * Pure C89, no allocation, so the Mac OS 9 target compiles it.
 */

#ifndef WOLFRAM_RETRY_H
#define WOLFRAM_RETRY_H

#include "wolfram/failure.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A caller-set cap and backoff shape. `max_attempts` counts the first
 * attempt, so 1 (or 0) disables retries and 3 allows two retries.
 * `base_delay_ms` is the wait after the first failure; each later failure
 * doubles it up to `max_delay_ms` (a non-positive cap means no cap).
 * `jitter_percent` adds 0..N percent of the capped delay, never subtracts,
 * clamped to 100.
 */
typedef struct wf_retry_policy {
    unsigned int max_attempts;
    long base_delay_ms;
    long max_delay_ms;
    unsigned int jitter_percent;
} wf_retry_policy;

/*
 * True when `kind` is retryable and `attempts` (the number of attempts
 * already made, so 1 after the first failure) is still below the cap.
 * A NULL policy, a cap below 2, or an unknown kind returns 0.
 */
int wf_retry_should_retry(wf_failure_kind kind, unsigned int attempts,
                          const wf_retry_policy *policy);

/*
 * Milliseconds before the next attempt. Exponential from `base_delay_ms`,
 * capped at `max_delay_ms`, plus deterministic jitter derived from
 * `jitter_seed` and capped again so the result never exceeds the cap.
 * `rate_limit_reset_ms` (the server's RateLimit-Reset converted to ms, or 0)
 * is a floor: the next attempt is never earlier than the server asked. It
 * may exceed `max_delay_ms` because it is the server's instruction, not
 * part of the exponential backoff.
 *
 * `attempts` uses the same meaning as wf_retry_should_retry. A NULL policy
 * or a non-positive base delay returns 0. The caller owns sleeping.
 */
long wf_retry_delay_ms(const wf_retry_policy *policy, unsigned int attempts,
                       long rate_limit_reset_ms, unsigned int jitter_seed);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_RETRY_H */
