#include "wolfram/syntax.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int wf_syntax_valid_chars(const char *s, const char *allowed) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (!strchr(allowed, *p)) return 0;
    }
    return 1;
}

static int wf_syntax_ascii_case_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int wf_syntax_ascii_case_equal_n(const char *a, const char *b, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    }
    return 1;
}

static int wf_syntax_subtag_is_alpha(const char *s, size_t len) {
    size_t i;
    if (len < 1) return 0;
    for (i = 0; i < len; i++) {
        if (!isalpha((unsigned char)s[i])) return 0;
    }
    return 1;
}

static int wf_syntax_subtag_is_alnum(const char *s, size_t len) {
    size_t i;
    if (len < 1) return 0;
    for (i = 0; i < len; i++) {
        if (!isalnum((unsigned char)s[i])) return 0;
    }
    return 1;
}

static long long wf_syntax_days_from_civil(int y, unsigned m, unsigned d) {
    int era;
    unsigned yoe;
    unsigned mp;
    unsigned doy;
    unsigned doe;
    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    mp = m > 2 ? m - 3 : m + 9;
    doy = (153 * mp + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long long)era * 146097LL + (long long)doe + 60LL;
}

static int wf_syntax_language_is_grandfathered(const char *language) {
    static const char *const grandfathered[] = {
        "art-lojban",
        "cel-gaulish",
        "en-GB-oed",
        "i-ami",
        "i-bnn",
        "i-default",
        "i-enochian",
        "i-hak",
        "i-klingon",
        "i-lux",
        "i-mingo",
        "i-navajo",
        "i-pwn",
        "i-tao",
        "i-tay",
        "i-tsu",
        "sgn-BE-FR",
        "sgn-BE-NL",
        "sgn-CH-DE",
        "zh-guoyu",
        "zh-hakka",
        "zh-min",
        "zh-min-nan",
        "zh-xiang",
    };
    size_t i;
    for (i = 0; i < sizeof(grandfathered) / sizeof(grandfathered[0]); i++) {
        if (wf_syntax_ascii_case_equal(language, grandfathered[i])) return 1;
    }
    return 0;
}

/* ── DID ─────────────────────────────────────────────────────────────── */

int wf_syntax_did_is_valid(const char *did) {
    size_t len, i, method_start, msid_start;
    if (!did) return 0;
    len = strlen(did);
    if (len == 0 || len > 2048) return 0;
    if (strncmp(did, "did:", 4) != 0) return 0;

    method_start = 4;
    for (i = method_start; i < len; i++) {
        unsigned char c = (unsigned char)did[i];
        if (c == ':') break;
        if (!((c >= 'a' && c <= 'z'))) return 0;
    }
    if (i == method_start || i == len) return 0;

    msid_start = i + 1;
    if (msid_start >= len) return 0;
    for (i = msid_start; i < len; i++) {
        unsigned char c = (unsigned char)did[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-') {
            continue;
        }
        if (c == ':') {
            if (i == len - 1) return 0;
            continue;
        }
        if (c == '%') {
            if (i + 2 >= len) return 0;
            c = (unsigned char)did[i + 1];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                  (c >= 'a' && c <= 'f'))) {
                return 0;
            }
            c = (unsigned char)did[i + 2];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                  (c >= 'a' && c <= 'f'))) {
                return 0;
            }
            i += 2;
            continue;
        }
        return 0;
    }
    return 1;
}

wf_status wf_syntax_did_validate(const char *did) {
    return wf_syntax_did_is_valid(did) ? WF_OK : WF_ERR_PARSE;
}

/* ── Handle ──────────────────────────────────────────────────────────── */

int wf_syntax_handle_is_valid(const char *handle) {
    const char *label;
    const char *p;
    int labels = 0;
    if (!handle || !handle[0] || strlen(handle) > 253) return 0;
    label = handle;
    for (p = handle; ; p++) {
        if (*p == '.' || *p == '\0') {
            size_t length = (size_t)(p - label);
            size_t i;
            if (length < 1 || length > 63 || label[0] == '-' ||
                label[length - 1] == '-') return 0;
            for (i = 0; i < length; i++) {
                if (!isalnum((unsigned char)label[i]) && label[i] != '-') return 0;
            }
            labels++;
            if (*p == '\0') {
                if (labels < 2 || !isalpha((unsigned char)label[0])) return 0;
                return 1;
            }
            label = p + 1;
        }
    }
}

wf_status wf_syntax_handle_validate(const char *handle) {
    return wf_syntax_handle_is_valid(handle) ? WF_OK : WF_ERR_PARSE;
}

/* ── at-identifier ──────────────────────────────────────────────────── */

int wf_syntax_at_identifier_is_valid(const char *id) {
    if (!id) return 0;
    if (strncmp(id, "did:", 4) == 0) return wf_syntax_did_is_valid(id);
    return wf_syntax_handle_is_valid(id);
}

/* ── NSID ─────────────────────────────────────────────────────────────── */

int wf_syntax_nsid_is_valid(const char *nsid) {
    const char *seg_start, *p;
    int seg_count = 0;
    size_t nsid_len;
    if (!nsid) return 0;
    nsid_len = strlen(nsid);
    if (nsid_len == 0 || nsid_len > 317) return 0;
    /* A trailing '.' yields an empty final segment (e.g. "com.example.foo."),
     * which atproto rejects — the segment loop below would otherwise stop at
     * the terminator and miss it, so guard explicitly. */
    if (nsid[nsid_len - 1] == '.') return 0;
    seg_start = nsid;
    while (*seg_start) {
        size_t seg_len;
        int is_last;
        p = seg_start;
        while (*p && *p != '.') p++;
        seg_len = (size_t)(p - seg_start);
        is_last = *p == '\0';
        if (seg_len < 1 || seg_len > 63) return 0;
        if (seg_count == 0) {
            if (!isalpha((unsigned char)seg_start[0])) return 0;
            for (size_t i = 0; i < seg_len; i++) {
                if (!isalnum((unsigned char)seg_start[i]) && seg_start[i] != '-') return 0;
            }
            if (seg_start[seg_len - 1] == '-') return 0;
        } else if (is_last) {
            if (!isalpha((unsigned char)seg_start[0])) return 0;
            for (size_t i = 0; i < seg_len; i++) {
                if (!isalnum((unsigned char)seg_start[i])) return 0;
            }
        } else {
            if (seg_start[0] == '-') return 0;
            for (size_t i = 0; i < seg_len; i++) {
                if (!isalnum((unsigned char)seg_start[i]) && seg_start[i] != '-') return 0;
            }
            if (seg_start[seg_len - 1] == '-') return 0;
        }
        seg_count++;
        if (is_last) break;
        seg_start = p + 1;
    }
    return seg_count >= 3;
}

wf_status wf_syntax_nsid_validate(const char *nsid) {
    return wf_syntax_nsid_is_valid(nsid) ? WF_OK : WF_ERR_PARSE;
}

/* ── Record key ──────────────────────────────────────────────────────── */

int wf_syntax_record_key_is_valid(const char *key) {
    size_t len;
    const unsigned char *p;
    if (!key) return 0;
    len = strlen(key);
    if (len < 1 || len > 512) return 0;
    if (strcmp(key, ".") == 0 || strcmp(key, "..") == 0) return 0;
    for (p = (const unsigned char *)key; *p; p++) {
        if (!isalnum(*p) && *p != '_' && *p != '~' && *p != '.' &&
            *p != ':' && *p != '-') return 0;
    }
    return 1;
}

/* ── TID ──────────────────────────────────────────────────────────────── */

int wf_syntax_tid_is_valid(const char *tid) {
    static const char first_ok[] = "234567abcdefghij";
    static const char rest_ok[]  = "234567abcdefghijklmnopqrstuvwxyz";
    if (!tid || strlen(tid) != 13) return 0;
    if (!strchr(first_ok, tid[0])) return 0;
    for (int i = 1; i < 13; i++) {
        if (!strchr(rest_ok, tid[i])) return 0;
    }
    return 1;
}

/* ── AT URI (manual parser) ──────────────────────────────────────────── */

int wf_syntax_aturi_parse(const char *aturi, wf_syntax_aturi *parsed) {
    const char *p, *start, *end;
    size_t alen;
    char *copy;
    if (!aturi || !parsed) return 0;
    memset(parsed, 0, sizeof(*parsed));
    alen = strlen(aturi);
    if (alen > 8192 || alen < 7 || strncmp(aturi, "at://", 5) != 0) return 0;
    for (p = aturi; *p; p++) {
        if ((unsigned char)*p > 127) return 0;
    }
    copy = strdup(aturi);
    if (!copy) return 0;
    p = aturi + 5;
    end = aturi + alen;
    start = p;
    while (p < end && *p != '/' && *p != '#') p++;
    if (p == start) { free(copy); return 0; }
    copy[p - aturi] = '\0';
    parsed->authority = copy + (start - aturi);
    if (!wf_syntax_at_identifier_is_valid(parsed->authority)) { free(copy); return 0; }
    if (p == end) {
        parsed->alloc = copy;
        return 1;
    }
    if (*p == '#') {
        start = p + 1;
        if (start >= end || *start != '/' || !wf_syntax_valid_chars(start,
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789._~:@!$&')(*+,;=%[\\]/]")) {
            free(copy);
            return 0;
        }
        parsed->fragment = copy + (start - aturi);
        parsed->alloc = copy;
        return 1;
    }
    /* path */
    p++;
    start = p;
    while (p < end && *p != '/' && *p != '#') p++;
    if (p == start) { free(copy); return 0; }
    copy[p - aturi] = '\0';
    parsed->collection = copy + (start - aturi);
    if (!wf_syntax_nsid_is_valid(parsed->collection)) { free(copy); return 0; }
    if (p == end) {
        parsed->alloc = copy;
        return 1;
    }
    if (*p == '#') {
        start = p + 1;
        if (start >= end || *start != '/' || !wf_syntax_valid_chars(start,
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789._~:@!$&')(*+,;=%[\\]/]")) {
            free(copy);
            return 0;
        }
        parsed->fragment = copy + (start - aturi);
        parsed->alloc = copy;
        return 1;
    }
    /* record key */
    p++;
    start = p;
    while (p < end && *p != '/' && *p != '#') p++;
    if (p == start) { free(copy); return 0; }
    copy[p - aturi] = '\0';
    parsed->record_key = copy + (start - aturi);
    if (!wf_syntax_record_key_is_valid(parsed->record_key)) { free(copy); return 0; }
    if (p == end) {
        parsed->alloc = copy;
        return 1;
    }
    if (*p == '#') {
        start = p + 1;
        if (start >= end || *start != '/' || !wf_syntax_valid_chars(start,
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789._~:@!$&')(*+,;=%[\\]/]")) {
            free(copy);
            return 0;
        }
        parsed->fragment = copy + (start - aturi);
        parsed->alloc = copy;
        return 1;
    }
    free(copy);
    return 0;
}

void wf_syntax_aturi_free(wf_syntax_aturi *parsed) {
    if (!parsed) return;
    free(parsed->alloc);
    memset(parsed, 0, sizeof(*parsed));
}

/* ── Datetime (RFC 3339) ─────────────────────────────────────────────── */

int wf_syntax_datetime_is_valid(const char *datetime) {
    const char *p;
    const char *end;
    int year, month, day, hour, minute, second;
    int offset_sign = 0;
    int offset_hour = 0;
    int offset_minute = 0;
    long long seconds;
    long long min_seconds;
    long long max_seconds;
    if (!datetime) return 0;
    end = datetime + strlen(datetime);
    if (end - datetime > 64 || end - datetime < 20) return 0;
    p = datetime;
    if (end - p < 20 || !isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) ||
        !isdigit((unsigned char)p[2]) || !isdigit((unsigned char)p[3]) ||
        p[4] != '-' || !isdigit((unsigned char)p[5]) || !isdigit((unsigned char)p[6]) ||
        p[7] != '-' || !isdigit((unsigned char)p[8]) || !isdigit((unsigned char)p[9]) ||
        p[10] != 'T' || !isdigit((unsigned char)p[11]) || !isdigit((unsigned char)p[12]) ||
        p[13] != ':' || !isdigit((unsigned char)p[14]) || !isdigit((unsigned char)p[15]) ||
        p[16] != ':' || !isdigit((unsigned char)p[17]) || !isdigit((unsigned char)p[18])) {
        return 0;
    }
    year = (p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
    month = (p[5] - '0') * 10 + (p[6] - '0');
    day = (p[8] - '0') * 10 + (p[9] - '0');
    hour = (p[11] - '0') * 10 + (p[12] - '0');
    minute = (p[14] - '0') * 10 + (p[15] - '0');
    second = (p[17] - '0') * 10 + (p[18] - '0');
    p += 19;
    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) p++;
    }
    if (*p == 'Z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        offset_sign = (*p == '+') ? 1 : -1;
        p++;
        if (end - p < 5 || !isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) ||
            p[2] != ':' || !isdigit((unsigned char)p[3]) || !isdigit((unsigned char)p[4])) {
            return 0;
        }
        offset_hour = (p[0] - '0') * 10 + (p[1] - '0');
        offset_minute = (p[3] - '0') * 10 + (p[4] - '0');
        if (offset_hour > 23 || offset_minute > 59) return 0;
        p += 5;
        if (offset_sign < 0 && offset_hour == 0 && offset_minute == 0) return 0;
    } else {
        return 0;
    }
    if (*p != '\0') return 0;
    if (month < 1 || month > 12) return 0;
    if (hour > 23 || minute > 59 || second > 60) return 0;
    {
        static const int month_lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int max_day = month_lengths[month - 1];
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (month == 2 && leap) max_day = 29;
        if (day < 1 || day > max_day) return 0;
    }
    seconds = wf_syntax_days_from_civil(year, (unsigned)month, (unsigned)day) * 86400LL +
        (long long)hour * 3600LL + (long long)minute * 60LL + (long long)second;
    if (offset_sign != 0) {
        seconds -= (long long)offset_sign * ((long long)offset_hour * 3600LL +
            (long long)offset_minute * 60LL);
    }
    min_seconds = wf_syntax_days_from_civil(0, 1, 1) * 86400LL;
    max_seconds = wf_syntax_days_from_civil(10000, 1, 1) * 86400LL - 1;
    if (seconds < min_seconds || seconds > max_seconds) return 0;
    return 1;
}

/* ── Language (BCP 47 — simplified) ──────────────────────────────────── */

int wf_syntax_language_is_valid(const char *language) {
    const char *subtags[32];
    size_t lengths[32];
    int count;
    int pos = 0;
    if (!language || !*language || strlen(language) > 64) return 0;
    if (wf_syntax_language_is_grandfathered(language)) return 1;
    count = 0;
    {
        const char *start = language;
        const char *p = language;
        while (1) {
            if (*p == '-' || *p == '\0') {
                if (p == start || count >= 32) return 0;
                subtags[count] = start;
                lengths[count] = (size_t)(p - start);
                count++;
                if (*p == '\0') break;
                start = p + 1;
            }
            p++;
        }
    }
    if (count < 1) return 0;
    if (lengths[0] == 1 && (subtags[0][0] == 'x' || subtags[0][0] == 'X')) {
        if (count < 2) return 0;
        for (pos = 1; pos < count; pos++) {
            if (lengths[pos] < 1 || lengths[pos] > 8 || !wf_syntax_subtag_is_alnum(subtags[pos], lengths[pos])) {
                return 0;
            }
        }
        return 1;
    }
    if (!wf_syntax_subtag_is_alpha(subtags[0], lengths[0]) || lengths[0] < 2 || lengths[0] > 8) return 0;
    pos = 1;
    if (lengths[0] == 2 || lengths[0] == 3) {
        int extlang_count = 0;
        while (pos < count && extlang_count < 3 && lengths[pos] == 3 && wf_syntax_subtag_is_alpha(subtags[pos], lengths[pos])) {
            pos++;
            extlang_count++;
        }
    }
    if (pos < count && lengths[pos] == 4 && wf_syntax_subtag_is_alpha(subtags[pos], lengths[pos])) {
        pos++;
    }
    if (pos < count && ((lengths[pos] == 2 && wf_syntax_subtag_is_alpha(subtags[pos], lengths[pos])) ||
        (lengths[pos] == 3 && wf_syntax_subtag_is_alnum(subtags[pos], lengths[pos])))) {
        pos++;
    }
    {
        const char *variants[16];
        size_t variant_lengths[16];
        int variant_count = 0;
        while (pos < count) {
            int is_variant = 0;
            if ((lengths[pos] >= 5 && lengths[pos] <= 8 && wf_syntax_subtag_is_alnum(subtags[pos], lengths[pos])) ||
                (lengths[pos] == 4 && isdigit((unsigned char)subtags[pos][0]) && wf_syntax_subtag_is_alnum(subtags[pos], lengths[pos]))) {
                is_variant = 1;
            }
            if (!is_variant) break;
            for (int i = 0; i < variant_count; i++) {
                if (lengths[pos] == variant_lengths[i] &&
                    wf_syntax_ascii_case_equal_n(subtags[pos], variants[i], lengths[pos])) return 0;
            }
            if (variant_count < 16) {
                variants[variant_count] = subtags[pos];
                variant_lengths[variant_count] = lengths[pos];
                variant_count++;
            }
            pos++;
        }
    }
    {
        char seen_singletons[16];
        int seen_singleton_count = 0;
        while (pos < count && lengths[pos] == 1 && tolower((unsigned char)subtags[pos][0]) != 'x') {
            char singleton = (char)tolower((unsigned char)subtags[pos][0]);
            int ext_count = 0;
            int i;
            for (i = 0; i < seen_singleton_count; i++) {
                if (seen_singletons[i] == singleton) return 0;
            }
            if (seen_singleton_count < 16) {
                seen_singletons[seen_singleton_count++] = singleton;
            }
            pos++;
            if (pos >= count) return 0;
            while (pos < count && lengths[pos] >= 2 && lengths[pos] <= 8 && wf_syntax_subtag_is_alnum(subtags[pos], lengths[pos])) {
                pos++;
                ext_count++;
            }
            if (ext_count < 1) return 0;
        }
    }
    if (pos < count && lengths[pos] == 1 && (subtags[pos][0] == 'x' || subtags[pos][0] == 'X')) {
        pos++;
        if (pos >= count) return 0;
        while (pos < count) {
            if (lengths[pos] < 1 || lengths[pos] > 8 || !wf_syntax_subtag_is_alnum(subtags[pos], lengths[pos])) return 0;
            pos++;
        }
        return 1;
    }
    return pos == count;
}
