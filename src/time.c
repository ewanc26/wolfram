/*
 * time.c -- see wolfram/time.h.
 */

#include "wolfram/time.h"

#include "wolfram/syntax.h"

#include <stdio.h>
#include <string.h>

/* Days from 1970-01-01 to y-m-d (proleptic Gregorian), Howard Hinnant's
 * days_from_civil. Valid for the whole range this file accepts. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yy + (*m <= 2);
}

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int two(const char *s) {
    return (s[0] - '0') * 10 + (s[1] - '0');
}

wf_status wf_time_parse_rfc3339(const char *dt, int64_t *epoch) {
    static const int mdays[12] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
    if (!dt || !epoch || !wf_syntax_datetime_is_valid(dt)) return WF_ERR_PARSE;
    /* The validator has fixed the shape:
     * YYYY-MM-DDTHH:MM:SS[.f+](Z|+hh:mm|-hh:mm). */
    const int year = two(dt) * 100 + two(dt + 2);
    const int month = two(dt + 5), day = two(dt + 8);
    const int hour = two(dt + 11), minute = two(dt + 14), second = two(dt + 17);
    if (month < 1 || month > 12 || day < 1 ||
        day > mdays[month - 1] + (month == 2 && is_leap(year)) || hour > 23 ||
        minute > 59 || second > 60)
        return WF_ERR_PARSE;
    const char *p = dt + 19;
    if (*p == '.')
        for (++p; *p >= '0' && *p <= '9'; ++p) {
        }
    int64_t offset = 0;
    if (*p == '+' || *p == '-') {
        if (strlen(p) != 6) return WF_ERR_PARSE;
        const int oh = two(p + 1), om = two(p + 4);
        if (oh > 23 || om > 59) return WF_ERR_PARSE;
        offset = (int64_t)(oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
    } else if (*p != 'Z' || p[1] != '\0') {
        return WF_ERR_PARSE;
    }
    *epoch = days_from_civil(year, (unsigned)month, (unsigned)day) * 86400 +
             hour * 3600 + minute * 60 + second - offset;
    return WF_OK;
}

wf_status wf_time_format_rfc3339(int64_t epoch, char *out, size_t out_size) {
    /* 0000-01-01T00:00:00Z .. 9999-12-31T23:59:59Z */
    const int64_t lo = -62167219200LL, hi = 253402300799LL;
    if (!out || out_size < 21 || epoch < lo || epoch > hi)
        return WF_ERR_INVALID_ARG;
    int64_t days = epoch >= 0 ? epoch / 86400 : -((-epoch + 86399) / 86400);
    int64_t rem = epoch - days * 86400;
    int64_t y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    snprintf(out, out_size, "%04d-%02u-%02uT%02d:%02d:%02dZ", (int)y, m, d,
             (int)(rem / 3600), (int)(rem % 3600 / 60), (int)(rem % 60));
    return WF_OK;
}

void wf_time_relative(int64_t epoch, int64_t now, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    int64_t age = now - epoch;
    if (age < 0) age = 0;
    if (age < 60)
        snprintf(out, out_size, "%ds", (int)age);
    else if (age < 3600)
        snprintf(out, out_size, "%dm", (int)(age / 60));
    else if (age < 86400)
        snprintf(out, out_size, "%dh", (int)(age / 3600));
    else if (age < 7 * 86400)
        snprintf(out, out_size, "%dd", (int)(age / 86400));
    else if (age < 365 * 86400)
        snprintf(out, out_size, "%dw", (int)(age / (7 * 86400)));
    else
        snprintf(out, out_size, "%dy", (int)(age / (365 * 86400)));
}
