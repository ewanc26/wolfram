/*
 * test_lexicon_resolve.c -- wf_lexicon_registry_document and wf_lexicon_resolve
 * against test/vectors/lexicon_resolve.json and the vendored corpus.
 */

#include "wolfram/validate.h"

#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef WF_ATPROTO_LEXICONS
#define WF_ATPROTO_LEXICONS NULL
#endif
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
    char *vec_text = NULL;
    cJSON *vec = NULL;
    const char *mini;
    const char *mini2;
    const char *unknown;
    const char *bad_dir;
    cJSON *nsids;

    snprintf(path, sizeof path, "%s/lexicon_resolve.json", WF_TEST_VECTOR_DIR);
    vec_text = slurp(path);
    WF_CHECK(vec_text != NULL);
    vec = cJSON_Parse(vec_text);
    WF_CHECK(vec != NULL);
    mini = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(vec, "mini_lexicon"));
    mini2 = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(vec, "mini_lexicon_replacement"));
    unknown = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(vec, "unknown_nsid"));
    bad_dir = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(vec, "bad_corpus_dir"));
    nsids = cJSON_GetObjectItemCaseSensitive(vec, "corpus_nsids");
    WF_CHECK(mini && mini2 && unknown && bad_dir && cJSON_IsArray(nsids));

    /* In-memory registry: lookup returns the document as loaded, an unknown
     * NSID is WF_ERR_NOT_FOUND, NULL inputs are WF_ERR_INVALID_ARG, and a
     * reload replaces the raw document. */
    {
        wf_lexicon_registry *r = wf_lexicon_registry_new();
        const char *json = NULL;
        size_t len = 0;

        WF_CHECK(r != NULL);
        WF_CHECK(wf_lexicon_registry_load(r, mini, strlen(mini)) == WF_OK);
        WF_CHECK(wf_lexicon_registry_document(r, "com.example.mini", &json,
                                              &len) == WF_OK);
        WF_CHECK(json != NULL && len == strlen(mini) &&
                 memcmp(json, mini, len) == 0);
        WF_CHECK(wf_lexicon_registry_document(r, unknown, &json, &len) ==
                 WF_ERR_NOT_FOUND);
        WF_CHECK(wf_lexicon_registry_document(r, NULL, &json, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_lexicon_registry_load(r, mini2, strlen(mini2)) == WF_OK);
        WF_CHECK(wf_lexicon_registry_document(r, "com.example.mini", &json,
                                              &len) == WF_OK);
        WF_CHECK(len == strlen(mini2) && memcmp(json, mini2, len) == 0);
        wf_lexicon_registry_free(r);
    }

    /* wf_lexicon_resolve against the vendored corpus. */
    {
        const char *env = getenv("WF_ATPROTO_LEXICONS");
        const char *dir = (env && *env) ? env : WF_ATPROTO_LEXICONS;

        if (dir && *dir && access(dir, R_OK | X_OK) == 0) {
            cJSON *it;
            cJSON_ArrayForEach(it, nsids) {
                char *out = NULL;
                size_t out_len = 0;
                char needle[256];

                WF_CHECK(cJSON_IsString(it));
                WF_CHECK(wf_lexicon_resolve(it->valuestring, dir, &out,
                                            &out_len) == WF_OK);
                WF_CHECK(out != NULL && out_len > 0);
                snprintf(needle, sizeof needle, "\"id\": \"%s\"",
                         it->valuestring);
                if (!strstr(out, needle)) {
                    snprintf(needle, sizeof needle, "\"id\":\"%s\"",
                             it->valuestring);
                }
                WF_CHECK(strstr(out, needle) != NULL);
                free(out);
            }
            {
                char *out = NULL;
                size_t out_len = 0;
                WF_CHECK(wf_lexicon_resolve(unknown, dir, &out, &out_len) ==
                         WF_ERR_NOT_FOUND);
                WF_CHECK(out == NULL && out_len == 0);
                WF_CHECK(wf_lexicon_resolve("app.bsky.feed.post", bad_dir, &out,
                                            &out_len) == WF_ERR_INVALID_ARG);
                WF_CHECK(wf_lexicon_resolve(NULL, dir, &out, &out_len) ==
                         WF_ERR_INVALID_ARG);
            }
        } else {
            printf("SKIP: vendored corpus dir unavailable (set "
                   "WF_ATPROTO_LEXICONS)\n");
        }
    }

    cJSON_Delete(vec);
    free(vec_text);
    WF_TEST_SUMMARY();
}
