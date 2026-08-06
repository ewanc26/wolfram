/*
 * feedgen.c — implementation of the getFeedSkeleton builder.
 *
 * See include/wolfram/feedgen.h for the public API and ownership rules.
 * URI validation reuses the existing AT-URI syntax validator
 * (wf_syntax_aturi_parse, declared in wolfram/syntax.h); there is no
 * dedicated wf_syntax_at_uri_is_valid, so a non-zero parse result is treated
 * as "valid". cJSON is used to render the response (same include style as
 * feedgen_typed.c / notation.c: `#include <cJSON.h>`).
 */

#include "wolfram/feedgen.h"

#include "wolfram/syntax.h"

#include <cJSON.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

wf_status wf_feedgen_validate_candidates(const char *const *uris,
                                         size_t uri_count,
                                         size_t *out_invalid_index) {
    if (out_invalid_index) {
        *out_invalid_index = 0;
    }
    if (!uris && uri_count > 0) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < uri_count; i++) {
        wf_syntax_aturi parsed;
        /* wf_syntax_aturi_parse returns non-zero only for a well-formed
         * at:// URI; it allocates internally, so always free. */
        int ok = wf_syntax_aturi_parse(uris[i], &parsed);
        wf_syntax_aturi_free(&parsed);
        if (!ok) {
            if (out_invalid_index) {
                *out_invalid_index = i;
            }
            return WF_ERR_INVALID_ARG;
        }
    }
    return WF_OK;
}

/* Allocate a NUL-terminated decimal string for `value`. Returns NULL on
 * allocation failure. */
static char *wf_feedgen_offset_to_cursor(size_t value) {
    size_t t = value;
    int digits = 1;
    while (t >= 10) {
        t /= 10;
        digits++;
    }
    char *s = (char *)malloc((size_t)digits + 1);
    if (!s) {
        return NULL;
    }
    snprintf(s, (size_t)digits + 1, "%zu", value);
    return s;
}

wf_status wf_feedgen_build_skeleton(const char *const *uris, size_t uri_count,
                                    size_t limit, const char *cursor,
                                    char **out_json, char **out_next_cursor) {
    if (!out_json || !out_next_cursor) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;
    *out_next_cursor = NULL;
    if (!uris && uri_count > 0) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit == 0) {
        return WF_ERR_INVALID_ARG;
    }

    /* Validate every candidate up front; reject the whole batch on any
     * invalid URI. */
    wf_status vs = wf_feedgen_validate_candidates(uris, uri_count, NULL);
    if (vs != WF_OK) {
        return vs;
    }

    /* Decode the cursor as a decimal offset into the de-duplicated list. */
    size_t offset = 0;
    if (cursor && *cursor) {
        errno = 0;
        char *endp = NULL;
        long v = strtol(cursor, &endp, 10);
        if (endp == cursor || *endp != '\0' || v < 0 || errno == ERANGE) {
            return WF_ERR_INVALID_ARG;
        }
        offset = (size_t)v;
    }

    /* De-duplicate preserving input order. Worst case every URI is unique,
     * so a uri_count-sized pointer table is sufficient. */
    const char **unique = NULL;
    if (uri_count > 0) {
        unique = (const char **)malloc(uri_count * sizeof(*unique));
        if (!unique) {
            return WF_ERR_ALLOC;
        }
    }
    size_t uniq = 0;
    for (size_t i = 0; i < uri_count; i++) {
        int dup = 0;
        for (size_t j = 0; j < uniq; j++) {
            if (strcmp(unique[j], uris[i]) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup) {
            unique[uniq++] = uris[i];
        }
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(unique);
        return WF_ERR_ALLOC;
    }
    cJSON *feed = cJSON_AddArrayToObject(root, "feed");
    if (!feed) {
        cJSON_Delete(root);
        free(unique);
        return WF_ERR_ALLOC;
    }

    /* If the cursor points past the end, clamp so we emit nothing. */
    if (offset > uniq) {
        offset = uniq;
    }

    size_t emitted = 0;
    size_t pos = offset;
    while (pos < uniq && emitted < limit) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            free(unique);
            return WF_ERR_ALLOC;
        }
        cJSON_AddItemToArray(feed, item);
        if (!cJSON_AddStringToObject(item, "post", unique[pos])) {
            cJSON_Delete(root);
            free(unique);
            return WF_ERR_ALLOC;
        }
        pos++;
        emitted++;
    }

    /* Cursor for the next page: the new offset (pos) when more remain.
     * It is emitted both as the `cursor` key in the JSON response and as a
     * standalone owned string for the caller's convenience. */
    if (pos < uniq) {
        char *next = wf_feedgen_offset_to_cursor(pos);
        if (!next) {
            cJSON_Delete(root);
            free(unique);
            return WF_ERR_ALLOC;
        }
        if (!cJSON_AddStringToObject(root, "cursor", next)) {
            free(next);
            cJSON_Delete(root);
            free(unique);
            return WF_ERR_ALLOC;
        }
        *out_next_cursor = next;
    }

    char *js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(unique);
    if (!js) {
        free(*out_next_cursor);
        *out_next_cursor = NULL;
        return WF_ERR_ALLOC;
    }
    *out_json = js;
    return WF_OK;
}

void wf_feedgen_skeleton_free(char *json, char *cursor) {
    free(json);
    free(cursor);
}
