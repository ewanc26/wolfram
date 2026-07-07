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
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}
