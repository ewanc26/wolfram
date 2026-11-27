/* Shared loader for the vectors in test/vectors/spaces/. */

#ifndef WOLFRAM_TEST_SPACES_VECTORS_H
#define WOLFRAM_TEST_SPACES_VECTORS_H

#include <stdio.h>
#include <stdlib.h>

#include <cJSON.h>

#ifndef WF_TEST_VECTOR_DIR
#define WF_TEST_VECTOR_DIR "test/vectors"
#endif

/* Parse test/vectors/spaces/<name>; NULL if it cannot be read. */
static inline cJSON *vec_load(const char *name) {
    char path[1024];
    FILE *f;
    long n;
    char *buf;
    cJSON *root = NULL;

    snprintf(path, sizeof(path), "%s/spaces/%s", WF_TEST_VECTOR_DIR, name);
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = '\0';
        root = cJSON_Parse(buf);
    }
    free(buf);
    fclose(f);
    return root;
}

/* The string member `key`, or NULL when absent. */
static inline const char *vec_str(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

/* True when `data` (len bytes) equals the hex string `hex`. */
static inline int vec_hex_eq(const unsigned char *data, size_t len,
                             const char *hex) {
    size_t i;
    char two[3];

    if (!hex) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        snprintf(two, sizeof(two), "%02x", data[i]);
        if (hex[2 * i] != two[0] || hex[2 * i + 1] != two[1]) {
            return 0;
        }
    }
    return hex[2 * len] == '\0';
}

#endif /* WOLFRAM_TEST_SPACES_VECTORS_H */
