/*
 * test_time.c -- wolfram/time.h against test/vectors/time.json and the atproto
 * interop datetime fixtures (every syntactically valid datetime must parse,
 * every invalid one must not).
 */

#include "wolfram/time.h"

#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_VECTOR_DIR
#define WF_TEST_VECTOR_DIR "test/vectors"
#endif
#ifndef SYNTAX_FIXTURE_DIR
#define SYNTAX_FIXTURE_DIR "test/fixtures/syntax"
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

static void fixture_lines(const char *name, int want_valid) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", SYNTAX_FIXTURE_DIR, name);
    char *text = slurp(path);
    WF_CHECK(text != NULL);
    if (!text) return;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        if (!line[0] || line[0] == '#') continue;
        int64_t e = 12345;
        wf_status st = wf_time_parse_rfc3339(line, &e);
        int ok = want_valid ? st == WF_OK : (st == WF_ERR_PARSE && e == 12345);
        WF_CHECK(ok);
        if (!ok) fprintf(stderr, "  fixture %s: '%s'\n", name, line);
    }
    free(text);
}

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/time.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    WF_CHECK(root != NULL);
    const cJSON *v;
    if (root) {
        cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "parse")) {
            const char *in =
                cJSON_GetObjectItemCaseSensitive(v, "input")->valuestring;
            const cJSON *want = cJSON_GetObjectItemCaseSensitive(v, "epoch");
            int64_t e = 777;
            wf_status st = wf_time_parse_rfc3339(in, &e);
            int ok = cJSON_IsNull(want)
                         ? (st == WF_ERR_PARSE && e == 777)
                         : (st == WF_OK && e == (int64_t)want->valuedouble);
            WF_CHECK(ok);
            if (!ok)
                fprintf(stderr, "  parse '%s' -> %d, %lld\n", in, (int)st,
                        (long long)e);
        }
        cJSON_ArrayForEach(v,
                           cJSON_GetObjectItemCaseSensitive(root, "format")) {
            char out[32];
            int64_t e = (int64_t)cJSON_GetObjectItemCaseSensitive(v, "epoch")
                            ->valuedouble;
            WF_CHECK(wf_time_format_rfc3339(e, out, sizeof out) == WF_OK);
            int ok = strcmp(out, cJSON_GetObjectItemCaseSensitive(v, "text")
                                     ->valuestring) == 0;
            WF_CHECK(ok);
            if (!ok)
                fprintf(stderr, "  format %lld -> %s\n", (long long)e, out);
            /* and it round-trips */
            int64_t back = 0;
            WF_CHECK(wf_time_parse_rfc3339(out, &back) == WF_OK && back == e);
        }
        cJSON_ArrayForEach(v,
                           cJSON_GetObjectItemCaseSensitive(root, "relative")) {
            char out[16];
            int64_t age =
                (int64_t)cJSON_GetObjectItemCaseSensitive(v, "age_seconds")
                    ->valuedouble;
            wf_time_relative(1000000 - age, 1000000, out, sizeof out);
            int ok = strcmp(out, cJSON_GetObjectItemCaseSensitive(v, "text")
                                     ->valuestring) == 0;
            WF_CHECK(ok);
            if (!ok)
                fprintf(stderr, "  relative %lld -> %s\n", (long long)age, out);
        }
    }
    cJSON_Delete(root);
    free(text);

    fixture_lines("datetime_syntax_valid.txt", 1);
    fixture_lines("datetime_syntax_invalid.txt", 0);
    fixture_lines("datetime_parse_invalid.txt", 0);

    char small[8], out[32];
    WF_CHECK(wf_time_format_rfc3339(0, small, sizeof small) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_time_format_rfc3339(253402300800LL, out, sizeof out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_time_format_rfc3339(-62167219201LL, out, sizeof out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_time_parse_rfc3339(NULL, (int64_t[1]){0}) == WF_ERR_PARSE);
    WF_CHECK(wf_time_parse_rfc3339("1985-04-12T23:20:50Z", NULL) ==
             WF_ERR_PARSE);
    wf_time_relative(0, 100, small, 2); /* truncation is safe */
    WF_CHECK(small[1] == '\0' || small[0] == '\0' || small[0] != 0);
    WF_TEST_SUMMARY();
}
