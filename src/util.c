#include "wolfram/util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *wf_dup_span(const char *s, size_t len) {
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

int wf_ascii_iequals(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

char *wf_str_dup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

wf_status wf_str_set(char **dst, const char *src) {
    char *copy = wf_str_dup(src);
    if (src && !copy) return WF_ERR_ALLOC;
    free(*dst);
    *dst = copy;
    return WF_OK;
}
