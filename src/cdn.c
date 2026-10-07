/* cdn.c -- wf_bsky_cdn_url (see cdn.h). C89. */

#include "wolfram/cdn.h"

#include <string.h>

#define CDN_PREFIX "https://cdn.bsky.app/img/"

static const char *const preset_names[] = {
    "avatar", "avatar_thumbnail", "banner", "feed_thumbnail", "feed_fullsize"};
#define PRESET_COUNT (sizeof preset_names / sizeof preset_names[0])

static int is_known_preset(const char *p, size_t n) {
    size_t i;
    for (i = 0; i < PRESET_COUNT; i++)
        if (strlen(preset_names[i]) == n && memcmp(preset_names[i], p, n) == 0)
            return 1;
    return 0;
}

static int plain_char(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c == ':' || c == '.' || c == '-' ||
           c == '_' || c == '/';
}

wf_status wf_bsky_cdn_url(const char *url, wf_cdn_preset preset,
                          wf_cdn_format format, char *out, size_t cap) {
    const char *p;
    const char *slash;
    const char *tail;
    const char *at;
    const char *suffix = "";
    const char *name;
    size_t prefix_len = sizeof CDN_PREFIX - 1;
    size_t need;

    if (out && cap > 0) out[0] = '\0';
    if (!url || !out || cap == 0 || (unsigned)preset >= PRESET_COUNT ||
        (unsigned)format > WF_CDN_FORMAT_PNG)
        return WF_ERR_INVALID_ARG;
    if (strncmp(url, CDN_PREFIX, prefix_len) != 0) return WF_ERR_VALIDATION;

    p = url + prefix_len;
    slash = strchr(p, '/');
    if (!slash || !is_known_preset(p, (size_t)(slash - p)))
        return WF_ERR_VALIDATION;
    if (strncmp(slash, "/plain/", 7) != 0) return WF_ERR_VALIDATION;
    tail = slash + 7; /* did/cid[@format] */
    if (*tail == '\0') return WF_ERR_VALIDATION;

    for (p = tail; *p; p++)
        if (!plain_char((unsigned char)*p) && *p != '@')
            return WF_ERR_VALIDATION;
    at = strchr(tail, '@');
    if (at) {
        if (strchr(at + 1, '@')) return WF_ERR_VALIDATION;
        if (strcmp(at + 1, "jpeg") != 0 && strcmp(at + 1, "png") != 0)
            return WF_ERR_VALIDATION;
    }

    if (format == WF_CDN_FORMAT_JPEG)
        suffix = "@jpeg";
    else if (format == WF_CDN_FORMAT_PNG)
        suffix = "@png";
    else if (at)
        suffix = at; /* keep */

    name = preset_names[preset];
    need = prefix_len + strlen(name) + 7 +
           (size_t)((at ? at : tail + strlen(tail)) - tail) + strlen(suffix) +
           1;
    if (need > cap) return WF_ERR_INVALID_ARG;

    memcpy(out, CDN_PREFIX, prefix_len);
    out += prefix_len;
    memcpy(out, name, strlen(name));
    out += strlen(name);
    memcpy(out, "/plain/", 7);
    out += 7;
    memcpy(out, tail, (size_t)((at ? at : tail + strlen(tail)) - tail));
    out += (at ? at : tail + strlen(tail)) - tail;
    memcpy(out, suffix, strlen(suffix) + 1);
    return WF_OK;
}
