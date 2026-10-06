/*
 * test_failure.c -- wf_failure_classify and wf_failure_tag against
 * test/vectors/failure.json, plus totality: every wf_status and a spread of
 * HTTP statuses and error names gives a valid kind.
 */

#include "wolfram/failure.h"

#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_VECTOR_DIR
#define WF_TEST_VECTOR_DIR "test/vectors"
#endif

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n)
        b[n] = '\0';
    else {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/failure.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    WF_CHECK(root != NULL);

    const cJSON *statuses = cJSON_GetObjectItemCaseSensitive(root, "statuses");
    const cJSON *c;
    int n = 0;
    cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(root, "cases")) {
        const char *sname =
            cJSON_GetObjectItemCaseSensitive(c, "status")->valuestring;
        const cJSON *sv = cJSON_GetObjectItemCaseSensitive(statuses, sname);
        WF_CHECK(sv != NULL);
        long http = (long)cJSON_GetObjectItemCaseSensitive(c, "http")->valuedouble;
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(c, "xrpc_error");
        const char *err = cJSON_IsString(e) ? e->valuestring : NULL;
        const char *want =
            cJSON_GetObjectItemCaseSensitive(c, "kind")->valuestring;
        wf_failure_kind got =
            wf_failure_classify((wf_status)(int)sv->valuedouble, http, err);
        if (strcmp(wf_failure_tag(got), want) != 0)
            fprintf(stderr, "case %d (%s, %ld, %s): got %s, want %s\n", n, sname,
                    http, err ? err : "null", wf_failure_tag(got), want);
        WF_CHECK(strcmp(wf_failure_tag(got), want) == 0);
        n++;
    }
    WF_CHECK(n > 50);

    /* Total: no status, HTTP status or name produces an out-of-range kind. */
    {
        static const char *names[] = {NULL, "", "RateLimitExceeded", "x"};
        int s, h, i;
        for (s = 0; s < 40; s++)
            for (h = -1; h < 700; h += 1)
                for (i = 0; i < 4; i++) {
                    wf_failure_kind k =
                        wf_failure_classify((wf_status)s, h, names[i]);
                    WF_CHECK(k >= WF_FAIL_NONE && k <= WF_FAIL_OTHER);
                }
    }

    /* Tags are distinct and stable. */
    {
        wf_failure_kind a, b;
        for (a = WF_FAIL_NONE; a <= WF_FAIL_OTHER; a++)
            for (b = WF_FAIL_NONE; b < a; b++)
                WF_CHECK(strcmp(wf_failure_tag(a), wf_failure_tag(b)) != 0);
        WF_CHECK(strcmp(wf_failure_tag((wf_failure_kind)99), "other") == 0);
    }

    cJSON_Delete(root);
    free(text);
    WF_TEST_SUMMARY();
}
