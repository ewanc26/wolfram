/*
 * Storage and lookup for the response headers a wf_response carries. Strict
 * C89 and libc only: every transport links it, consoles and Classic Mac OS 9
 * included.
 */

#include "wolfram/xrpc.h"

#include <stdlib.h>
#include <string.h>

/* RFC 9110 token characters, which a field name is made of. */
static int is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           (c != '\0' && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

wf_status wf_response_add_header(wf_response *res, const char *name,
                                 const char *value) {
    size_t name_len;
    size_t value_len;
    size_t used;
    size_t i;
    char *grown;

    if (res == NULL || name == NULL || value == NULL || name[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    name_len = strlen(name);
    for (i = 0; i < name_len; i++) {
        if (!is_name_char(name[i])) {
            return WF_ERR_INVALID_ARG;
        }
    }
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    value_len = strlen(value);
    while (value_len > 0 &&
           (value[value_len - 1] == ' ' || value[value_len - 1] == '\t')) {
        value_len--;
    }
    /* A line break would start a header of the value's choosing. */
    if (memchr(value, '\r', value_len) != NULL ||
        memchr(value, '\n', value_len) != NULL) {
        return WF_ERR_INVALID_ARG;
    }

    /* The block is "name:value\n" lines with lower-cased names. */
    used = res->header_block != NULL ? strlen(res->header_block) : 0;
    if (name_len > WF_RESPONSE_MAX_HEADER_BYTES ||
        value_len > WF_RESPONSE_MAX_HEADER_BYTES ||
        used + name_len + value_len + 2 > WF_RESPONSE_MAX_HEADER_BYTES) {
        return WF_ERR_VALIDATION;
    }
    grown = (char *)realloc(res->header_block, used + name_len + value_len + 3);
    if (grown == NULL) {
        return WF_ERR_ALLOC;
    }
    res->header_block = grown;
    for (i = 0; i < name_len; i++) {
        grown[used + i] = lower(name[i]);
    }
    used += name_len;
    grown[used++] = ':';
    memcpy(grown + used, value, value_len);
    used += value_len;
    grown[used++] = '\n';
    grown[used] = '\0';
    return WF_OK;
}

/* True when the line at `line` is for `name`. */
static int line_is(const char *line, const char *name) {
    while (*name != '\0') {
        if (*line++ != lower(*name++)) {
            return 0;
        }
    }
    return *line == ':';
}

wf_status wf_response_header(const wf_response *res, const char *name,
                             char **out) {
    const char *line;
    char *joined;
    size_t len = 0;
    int found = 0;

    if (out == NULL) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (res == NULL || name == NULL || name[0] == '\0' ||
        strchr(name, ':') != NULL) {
        return WF_ERR_INVALID_ARG;
    }
    if (res->header_block == NULL) {
        return WF_ERR_NOT_FOUND;
    }
    /* Joining with ", " never outgrows the block it comes from. */
    joined = (char *)malloc(strlen(res->header_block) + 1);
    if (joined == NULL) {
        return WF_ERR_ALLOC;
    }
    for (line = res->header_block; *line != '\0';) {
        const char *end = strchr(line, '\n');
        if (end == NULL) {
            break;
        }
        if (line_is(line, name)) {
            const char *value = strchr(line, ':') + 1;
            size_t value_len = (size_t)(end - value);
            if (found) {
                memcpy(joined + len, ", ", 2);
                len += 2;
            }
            memcpy(joined + len, value, value_len);
            len += value_len;
            found = 1;
        }
        line = end + 1;
    }
    if (!found) {
        free(joined);
        return WF_ERR_NOT_FOUND;
    }
    joined[len] = '\0';
    *out = joined;
    return WF_OK;
}
