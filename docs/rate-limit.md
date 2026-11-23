# Rate limits and retries

A busy PDS tells a client how much budget it has left in the `RateLimit-Limit`, `RateLimit-Remaining`, `RateLimit-Reset` and `RateLimit-Policy` response headers. Wolfram parses those headers instead of discarding them, so a weak client can pace itself before the next burst rather than discovering the limit through a 429.

The parser is in [`wolfram/rate_limit.h`](../include/wolfram/rate_limit.h); the retry schedule is in [`wolfram/retry.h`](../include/wolfram/retry.h). Both are allocation-free, never sleep, and build as C89 for the Mac OS 9 target.

## Reading the budget

Every `wf_response` carries the budget parsed from that response, and the client keeps the latest one for a quick readback:

```c
wf_response res = {0};
wf_status st = wf_xrpc_query(client, "app.bsky.feed.getTimeline", "limit=50",
                             &res);

wf_rate_limit budget = wf_xrpc_client_get_rate_limit(client);
if (budget.present & WF_RATE_LIMIT_HAS_REMAINING) {
    /* budget.remaining requests are left in this window */
}
if (budget.present & WF_RATE_LIMIT_HAS_RESET) {
    /* budget.reset seconds until the server's window resets */
}
if (budget.present & WF_RATE_LIMIT_HAS_POLICY) {
    /* budget.policy_quota over budget.policy_window seconds */
}

wf_response_free(&res);
```

`present` is a bitmask, because a server may send any subset of the headers. A zeroed budget means no usable header was seen: a missing header, a malformed value, a failure before a response arrived, or a transport that does not parse them. The libcurl transport parses the headers today; the Wii and Mac OS 9 transports report unknown. The values are never guessed — a malformed `RateLimit-Limit: 12x` leaves the field and its bit untouched.

`RateLimit-Policy` accepts both shapes in use: the current `"default";q=100;w=60`, and the legacy bare `200;w=900`. Only the first policy item is reported. Unknown parameters such as `qu=` and `pk=` are skipped.

## Retrying

`wf_retry_should_retry` accepts the failure kinds another attempt can plausibly fix (`rate-limit`, `network`, `timeout`, `server`) and enforces the caller's cap. `wf_retry_delay_ms` computes the wait: exponential from the base, capped, with deterministic jitter and a floor at the server's Reset window. The caller owns the clock and the sleep, so there are no threads or hidden timers:

```c
wf_retry_policy policy = {
    .max_attempts = 4,      /* first try + up to three retries */
    .base_delay_ms = 500,
    .max_delay_ms = 8000,
    .jitter_percent = 20,
};

wf_failure_kind kind =
    wf_failure_classify(st, res.status, xrpc_error); /* see failure.md */
if (wf_retry_should_retry(kind, attempts, &policy)) {
    long reset_ms = (budget.present & WF_RATE_LIMIT_HAS_RESET)
                        ? budget.reset * 1000L
                        : 0;
    long delay = wf_retry_delay_ms(&policy, attempts, reset_ms, seed);
    sleep_ms(delay); /* the caller's own timer */
    attempts++;
}
```

`jitter_seed` is any caller-supplied value, typically a per-process counter or a random number. The same inputs always give the same delay, which is what makes the schedule testable. Jitter only adds time; it never retries earlier than the exponential schedule. A 429 is already classified as `WF_FAIL_RATE_LIMIT` by `wf_failure_classify`, so a caller does not need a second rate-limit check.
