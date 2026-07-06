#include "wolfram/syntax.h"
#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

static int wf_syntax_valid_chars(const char *s, const char *allowed) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (!strchr(allowed, *p)) return 0;
    }
    return 1;
}

/* ── DID ─────────────────────────────────────────────────────────────── */

int wf_syntax_did_is_valid(const char *did) {
    regex_t regex;
    int ret;
    if (!did || strlen(did) > 2048) return 0;
    ret = regcomp(&regex,
        "^did:[a-z]+:[a-zA-Z0-9._:%-]*[a-zA-Z0-9._-]$",
        REG_EXTENDED | REG_NOSUB);
    if (ret != 0) return 0;
    ret = regexec(&regex, did, 0, NULL, 0);
    regfree(&regex);
    return ret == 0;
}

wf_status wf_syntax_did_validate(const char *did) {
    return wf_syntax_did_is_valid(did) ? WF_OK : WF_ERR_PARSE;
}

/* ── Handle ──────────────────────────────────────────────────────────── */

int wf_syntax_handle_is_valid(const char *handle) {
    regex_t regex;
    int ret;
    if (!handle || strlen(handle) > 253) return 0;
    ret = regcomp(&regex,
        "^([a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\\.)+"
        "[a-zA-Z]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$",
        REG_EXTENDED | REG_NOSUB);
    if (ret != 0) return 0;
    ret = regexec(&regex, handle, 0, NULL, 0);
    regfree(&regex);
    return ret == 0;
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
    int seg_count = 0, seg_len;
    if (!nsid || strlen(nsid) > 317) return 0;
    seg_start = nsid;
    while (*seg_start) {
        p = seg_start;
        while (*p && *p != '.') p++;
        seg_len = (int)(p - seg_start);
        if (seg_len < 1 || seg_len > 63) return 0;
        if (!isalpha((unsigned char)*seg_start)) return 0;
        if (seg_start[0] == '-' || seg_start[seg_len - 1] == '-') return 0;
        for (int i = 0; i < seg_len; i++) {
            if (!isalnum((unsigned char)seg_start[i]) && seg_start[i] != '-') return 0;
        }
        seg_count++;
        if (!*p) break;
        seg_start = p + 1;
    }
    if (seg_count < 3) return 0;
    {
        const char *last = nsid;
        const char *dot;
        while ((dot = strchr(last, '.')) != NULL) last = dot + 1;
        for (const char *cp = last; *cp; cp++) {
            if (*cp == '-') return 0;
        }
    }
    return 1;
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
    const char *p, *start;
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
    start = p;
    while (*p && *p != '/' && *p != '#') p++;
    if (p == start) { free(copy); return 0; }
    copy[start - aturi + (p - start)] = '\0';
    parsed->authority = copy + (start - aturi);
    if (!wf_syntax_at_identifier_is_valid(parsed->authority)) { free(copy); return 0; }
    if (*p == '/') {
        p++;
        start = p;
        while (*p && *p != '/' && *p != '#') p++;
        if (p > start) {
            copy[start - aturi + (p - start)] = '\0';
            parsed->collection = copy + (start - aturi);
            if (!wf_syntax_nsid_is_valid(parsed->collection)) { free(copy); return 0; }
        }
        if (*p == '/') {
            p++;
            start = p;
            while (*p && *p != '#') p++;
            if (p > start) {
                copy[start - aturi + (p - start)] = '\0';
                parsed->record_key = copy + (start - aturi);
                if (!wf_syntax_record_key_is_valid(parsed->record_key)) { free(copy); return 0; }
            }
        }
        if (*p == '/') { free(copy); return 0; }
    }
    if (*p == '#') {
        p++;
        start = p;
        copy[start - aturi] = '\0';
        if (*start == '/') {
            if (!wf_syntax_valid_chars(start + 1,
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "0123456789._~:@!$&')(*+,;=%[/-")) {
                free(copy); return 0;
            }
        }
        parsed->fragment = copy + (start - aturi);
    }
    parsed->alloc = copy;
    return 1;
}

void wf_syntax_aturi_free(wf_syntax_aturi *parsed) {
    if (!parsed) return;
    free(parsed->alloc);
    memset(parsed, 0, sizeof(*parsed));
}

/* ── Datetime (RFC 3339) ─────────────────────────────────────────────── */

int wf_syntax_datetime_is_valid(const char *datetime) {
    regex_t regex;
    int ret;
    int year;
    if (!datetime || strlen(datetime) > 64) return 0;
    ret = regcomp(&regex,
        "^([0-9]{4})-"
        "(0[1-9]|1[012])-"
        "([0-2][0-9]|3[01])T"
        "([0-1][0-9]|2[0-3]):"
        "([0-5][0-9]):"
        "([0-5][0-9]|60)"
        "(\\.[0-9]+)?"
        "(Z|[+-]([0-1][0-9]|2[0-3]):[0-5][0-9])$",
        REG_EXTENDED);
    if (ret != 0) return 0;
    ret = regexec(&regex, datetime, 0, NULL, 0);
    regfree(&regex);
    if (ret != 0) return 0;
    if (strstr(datetime, "-00:00") != NULL) return 0;
    year = atoi(datetime);
    if (year < 0 || year > 9999) return 0;
    return 1;
}

/* ── Language (BCP 47 — simplified) ──────────────────────────────────── */

int wf_syntax_language_is_valid(const char *language) {
    regex_t regex;
    int ret;
    if (!language || !*language) return 0;
    ret = regcomp(&regex,
        "^(("
        "[a-z]{2,3}(-[a-z]{3}(-[a-z]{3}){0,2})?"
        "|[a-z]{4}|[a-z]{5,8})"
        "(-[a-z]{4})?"
        "(-[a-z]{2}|[0-9]{3})?"
        "(-[a-z0-9]{5,8}|-[0-9][a-z0-9]{3})*"
        "(-[0-9a-wy-z][-a-z0-9]{2,8})*"
        "(-x(-[a-z0-9]{1,8})+)?"
        "|x(-[a-z0-9]{1,8})+)$",
        REG_EXTENDED | REG_ICASE);
    if (ret != 0) return 0;
    ret = regexec(&regex, language, 0, NULL, 0);
    regfree(&regex);
    return ret == 0;
}
