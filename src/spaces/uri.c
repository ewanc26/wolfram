#include "wolfram/spaces/uri.h"

#include <stdlib.h>
#include <string.h>

#include "wolfram/syntax.h"

#define SPACE_PREFIX "at:space/"
#define SPACE_PREFIX_LEN (sizeof(SPACE_PREFIX) - 1)

int wf_space_uri_has_prefix(const char *uri) {
    return uri && strncmp(uri, SPACE_PREFIX, SPACE_PREFIX_LEN) == 0;
}

int wf_space_skey_is_valid(const char *skey) {
    size_t i;

    if (!skey || skey[0] == '\0' || strcmp(skey, ".") == 0 ||
        strcmp(skey, "..") == 0) {
        return 0;
    }
    for (i = 0; skey[i] != '\0'; i++) {
        char c = skey[i];
        if (i >= WF_SPACE_SKEY_MAX) {
            return 0;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' ||
              c == '~' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

void wf_space_uri_free(wf_space_uri *uri) {
    if (!uri) {
        return;
    }
    free(uri->authority);
    free(uri->type);
    free(uri->skey);
    free(uri->author);
    free(uri->collection);
    free(uri->rkey);
    memset(uri, 0, sizeof(*uri));
}

static char *copy_segment(const char *start, size_t len) {
    char *s = malloc(len + 1);
    if (s) {
        memcpy(s, start, len);
        s[len] = '\0';
    }
    return s;
}

wf_status wf_space_uri_parse(const char *uri, wf_space_uri *out) {
    char *parts[6] = {0};
    size_t count = 0;
    size_t i;
    const char *p;
    wf_status status = WF_ERR_PARSE;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!uri) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_space_uri_has_prefix(uri)) {
        return WF_ERR_PARSE;
    }

    p = uri + SPACE_PREFIX_LEN;
    for (;;) {
        size_t len = strcspn(p, "/");
        if (len == 0 || count == 6) {
            goto fail;
        }
        parts[count] = copy_segment(p, len);
        if (!parts[count]) {
            status = WF_ERR_ALLOC;
            goto fail;
        }
        count++;
        if (p[len] == '\0') {
            break;
        }
        p += len + 1;
    }

    if (count != 3 && count != 6) {
        goto fail;
    }
    if (!wf_syntax_did_is_valid(parts[0]) ||
        !wf_syntax_nsid_is_valid(parts[1]) ||
        !wf_space_skey_is_valid(parts[2])) {
        goto fail;
    }
    if (count == 6 && (!wf_syntax_did_is_valid(parts[3]) ||
                       !wf_syntax_nsid_is_valid(parts[4]) ||
                       !wf_syntax_record_key_is_valid(parts[5]))) {
        goto fail;
    }

    out->authority = parts[0];
    out->type = parts[1];
    out->skey = parts[2];
    out->author = parts[3];
    out->collection = parts[4];
    out->rkey = parts[5];
    return WF_OK;

fail:
    for (i = 0; i < 6; i++) {
        free(parts[i]);
    }
    return status;
}

/* Join `count` strings with '/' after `prefix`. */
static char *join_path(const char *prefix, const char *const *parts,
                       size_t count) {
    size_t len = strlen(prefix);
    size_t i;
    size_t at;
    char *s;

    for (i = 0; i < count; i++) {
        len += strlen(parts[i]) + (i ? 1 : 0);
    }
    s = malloc(len + 1);
    if (!s) {
        return NULL;
    }
    at = strlen(prefix);
    memcpy(s, prefix, at);
    for (i = 0; i < count; i++) {
        size_t n = strlen(parts[i]);
        if (i) {
            s[at++] = '/';
        }
        memcpy(s + at, parts[i], n);
        at += n;
    }
    s[at] = '\0';
    return s;
}

wf_status wf_space_ref_format(const char *authority, const char *type,
                              const char *skey, char **out) {
    const char *parts[3];

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!authority || !type || !wf_syntax_did_is_valid(authority) ||
        !wf_syntax_nsid_is_valid(type) || !wf_space_skey_is_valid(skey)) {
        return WF_ERR_INVALID_ARG;
    }
    parts[0] = authority;
    parts[1] = type;
    parts[2] = skey;
    *out = join_path(SPACE_PREFIX, parts, 3);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

wf_status wf_space_record_uri_format(const char *space_ref, const char *author,
                                     const char *collection, const char *rkey,
                                     char **out) {
    wf_space_uri space;
    const char *parts[4];
    wf_status status;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!space_ref || !author || !collection || !rkey ||
        !wf_syntax_did_is_valid(author) ||
        !wf_syntax_nsid_is_valid(collection) ||
        !wf_syntax_record_key_is_valid(rkey)) {
        return WF_ERR_INVALID_ARG;
    }
    status = wf_space_uri_parse(space_ref, &space);
    if (status != WF_OK) {
        return status == WF_ERR_ALLOC ? status : WF_ERR_INVALID_ARG;
    }
    if (space.author) {
        wf_space_uri_free(&space);
        return WF_ERR_INVALID_ARG;
    }
    wf_space_uri_free(&space);
    parts[0] = space_ref;
    parts[1] = author;
    parts[2] = collection;
    parts[3] = rkey;
    *out = join_path("", parts, 4);
    return *out ? WF_OK : WF_ERR_ALLOC;
}
