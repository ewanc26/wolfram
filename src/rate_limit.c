/* rate_limit.c -- parse RateLimit-* headers (see rate_limit.h). C89. */

#include "wolfram/rate_limit.h"

#include <limits.h>
#include <string.h>

static int rl_is_ows(char c) {
    return c == ' ' || c == '\t';
}

static const char *rl_skip_ows(const char *p) {
    while (p != NULL && rl_is_ows(*p)) p++;
    return p;
}

static int rl_lower(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A' + 'a');
    return (int)c;
}

static int rl_name_is(const char *name, const char *want) {
    if (name == NULL || want == NULL) return 0;
    while (*name != '\0' && *want != '\0') {
        if (rl_lower((unsigned char)*name) != rl_lower((unsigned char)*want))
            return 0;
        name++;
        want++;
    }
    return *name == '\0' && *want == '\0';
}

static int rl_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int rl_is_key_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

/* Parse a non-negative integer and advance *pp past it. Rejects overflow. */
static int rl_parse_uint(const char **pp, long *out) {
    const char *p;
    long value;
    int seen;

    if (pp == NULL || *pp == NULL || out == NULL) return 0;
    p = *pp;
    value = 0;
    seen = 0;
    while (rl_is_digit(*p)) {
        int digit = *p - '0';
        if (value > (LONG_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
        p++;
        seen = 1;
    }
    if (!seen) return 0;
    *pp = p;
    *out = value;
    return 1;
}

/* A whole header value that is exactly one non-negative integer. */
static int rl_parse_plain(const char *value, long *out) {
    const char *p;
    long parsed;

    if (value == NULL || out == NULL) return 0;
    p = rl_skip_ows(value);
    if (!rl_parse_uint(&p, &parsed)) return 0;
    p = rl_skip_ows(p);
    if (*p != '\0') return 0;
    *out = parsed;
    return 1;
}

static int rl_key_is(const char *key, size_t len, const char *want) {
    size_t i;
    size_t want_len;

    if (key == NULL || want == NULL) return 0;
    want_len = strlen(want);
    if (want_len != len) return 0;
    for (i = 0; i < len; i++) {
        if (rl_lower((unsigned char)key[i]) != rl_lower((unsigned char)want[i]))
            return 0;
    }
    return 1;
}

/* Skip a parameter value we do not interpret: quoted string, byte sequence,
 * or a bare token up to the end of this policy item. */
static int rl_skip_value(const char **pp) {
    const char *p;

    if (pp == NULL || *pp == NULL) return 0;
    p = *pp;
    if (*p == '"') {
        p++;
        while (*p != '\0' && *p != '"') {
            if (*p == '\\' && p[1] != '\0') p++;
            p++;
        }
        if (*p != '"') return 0;
        p++;
    } else if (*p == ':') {
        p++;
        while (*p != '\0' && *p != ':') p++;
        if (*p != ':') return 0;
        p++;
    } else {
        while (*p != '\0' && *p != ';' && *p != ',') p++;
    }
    *pp = p;
    return 1;
}

/*
 * Parse the first policy item. Accepts both the current structured form
 * ("default";q=100;w=60) and the legacy bare-quota form (100;w=60).
 * Unknown parameters such as qu= and pk= are skipped, not treated as
 * malformed. If anything that should be an integer is not, the whole header
 * is rejected so a half-understood policy is not reported as a budget.
 */
static int rl_parse_policy(const char *value, long *quota, long *window,
                           int *has_quota, int *has_window) {
    const char *p;
    long parsed_quota;
    long parsed_window;
    int got_quota;
    int got_window;

    if (value == NULL || quota == NULL || window == NULL || has_quota == NULL ||
        has_window == NULL)
        return 0;

    p = rl_skip_ows(value);
    parsed_quota = 0;
    parsed_window = 0;
    got_quota = 0;
    got_window = 0;

    if (*p == '"') {
        p++;
        while (*p != '\0' && *p != '"') {
            if (*p == '\\' && p[1] != '\0') p++;
            p++;
        }
        if (*p != '"') return 0;
        p++;
        p = rl_skip_ows(p);
    } else if (rl_is_digit(*p)) {
        if (!rl_parse_uint(&p, &parsed_quota)) return 0;
        got_quota = 1;
        p = rl_skip_ows(p);
    } else {
        return 0;
    }

    while (*p == ';') {
        const char *key;
        size_t key_len;
        long number;

        p++;
        p = rl_skip_ows(p);
        key = p;
        while (rl_is_key_char(*p)) p++;
        key_len = (size_t)(p - key);
        if (key_len == 0) return 0;
        p = rl_skip_ows(p);
        if (*p != '=') return 0;
        p++;
        p = rl_skip_ows(p);

        if (rl_key_is(key, key_len, "q") || rl_key_is(key, key_len, "w")) {
            if (!rl_parse_uint(&p, &number)) return 0;
            if (rl_key_is(key, key_len, "q")) {
                parsed_quota = number;
                got_quota = 1;
            } else {
                parsed_window = number;
                got_window = 1;
            }
        } else {
            if (!rl_skip_value(&p)) return 0;
        }

        p = rl_skip_ows(p);
        if (*p == ';') continue;
        if (*p == ',') break; /* only the first policy item is reported */
        if (*p != '\0') return 0;
    }

    if (!got_quota && !got_window) return 0;
    *quota = parsed_quota;
    *window = parsed_window;
    *has_quota = got_quota;
    *has_window = got_window;
    return 1;
}

int wf_rate_limit_parse_header(wf_rate_limit *budget, const char *name,
                               const char *value) {
    long number;
    long quota;
    long window;
    int has_quota;
    int has_window;

    if (budget == NULL || name == NULL || value == NULL) return 0;

    if (rl_name_is(name, "RateLimit-Limit")) {
        if (!rl_parse_plain(value, &number)) return 0;
        budget->limit = number;
        budget->present |= WF_RATE_LIMIT_HAS_LIMIT;
        return 1;
    }
    if (rl_name_is(name, "RateLimit-Remaining")) {
        if (!rl_parse_plain(value, &number)) return 0;
        budget->remaining = number;
        budget->present |= WF_RATE_LIMIT_HAS_REMAINING;
        return 1;
    }
    if (rl_name_is(name, "RateLimit-Reset")) {
        if (!rl_parse_plain(value, &number)) return 0;
        budget->reset = number;
        budget->present |= WF_RATE_LIMIT_HAS_RESET;
        return 1;
    }
    if (rl_name_is(name, "RateLimit-Policy")) {
        if (!rl_parse_policy(value, &quota, &window, &has_quota, &has_window))
            return 0;
        budget->policy_quota = quota;
        budget->policy_window = window;
        budget->present |= WF_RATE_LIMIT_HAS_POLICY;
        return 1;
    }
    return 0;
}
