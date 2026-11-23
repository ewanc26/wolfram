/*
 * rate_limit.h -- the budget a server reports in its RateLimit-* headers.
 *
 * XRPC responses can carry RateLimit-Limit, RateLimit-Remaining,
 * RateLimit-Reset and RateLimit-Policy. This parses them into one small
 * value with no allocation, so a caller can pace a burst instead of
 * discovering the limit through a 429. A field is only set from a header
 * that parsed cleanly: a missing or malformed header leaves it unknown,
 * never zero-by-accident or garbage.
 *
 * The header names are the widely deployed RateLimit-Limit/Remaining/Reset/
 * Policy set. The current IETF draft replaces the first three with a single
 * `RateLimit` field; that field is not parsed here yet.
 *
 * Pure C89, no allocation, so the Mac OS 9 target compiles it.
 */

#ifndef WOLFRAM_RATE_LIMIT_H
#define WOLFRAM_RATE_LIMIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Which fields of wf_rate_limit came from a usable header. */
enum {
    WF_RATE_LIMIT_HAS_LIMIT = 1u << 0,
    WF_RATE_LIMIT_HAS_REMAINING = 1u << 1,
    WF_RATE_LIMIT_HAS_RESET = 1u << 2,
    WF_RATE_LIMIT_HAS_POLICY = 1u << 3
};

/*
 * The latest RateLimit-* budget. A zeroed value means no usable header was
 * seen; inspect `present` before reading any other field.
 *
 * `reset` is in seconds, as the header sends it. `policy_quota` and
 * `policy_window` describe the first policy item in RateLimit-Policy: q=
 * (or the legacy leading quota) and w= (seconds). A policy without a
 * window, or a legacy quota without one, sets only the field it has.
 */
typedef struct wf_rate_limit {
    unsigned present;   /* WF_RATE_LIMIT_HAS_* bitmask */
    long limit;         /* RateLimit-Limit */
    long remaining;     /* RateLimit-Remaining */
    long reset;         /* RateLimit-Reset, seconds */
    long policy_quota;  /* RateLimit-Policy q=, or the legacy leading quota */
    long policy_window; /* RateLimit-Policy w=, seconds */
} wf_rate_limit;

/*
 * Parse one HTTP response header into `budget`. `name` is compared
 * case-insensitively and must not include the trailing colon; `value` is
 * the header value without CR/LF. Returns 1 and updates the matching fields
 * when the header is recognised and valid, 0 otherwise. A malformed header
 * never changes `budget`.
 */
int wf_rate_limit_parse_header(wf_rate_limit *budget, const char *name,
                               const char *value);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_RATE_LIMIT_H */
