/*
 * test_muted_words.c -- wolfram/muted_words.h against
 * test/vectors/muted_words.json.
 */

#include "wolfram/muted_words.h"

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

static char *dup_str(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? strdup(v->valuestring) : NULL;
}

int main(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/muted_words.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    WF_CHECK(root != NULL);
    const cJSON *c;
    cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(root, "cases")) {
        const cJSON *ws = cJSON_GetObjectItemCaseSensitive(c, "words");
        size_t n = (size_t)cJSON_GetArraySize(ws);
        wf_actor_pref_muted_word *words = calloc(n ? n : 1, sizeof *words);
        size_t i = 0;
        const cJSON *w;
        cJSON_ArrayForEach(w, ws) {
            words[i].value = dup_str(w, "value");
            words[i].actor_target = dup_str(w, "actorTarget");
            words[i].expires_at = dup_str(w, "expiresAt");
            const cJSON *ts = cJSON_GetObjectItemCaseSensitive(w, "targets");
            words[i].target_count = (size_t)cJSON_GetArraySize(ts);
            words[i].targets =
                calloc(words[i].target_count + 1, sizeof(char *));
            size_t j = 0;
            const cJSON *t;
            cJSON_ArrayForEach(t, ts) words[i].targets[j++] =
                strdup(t->valuestring);
            i++;
        }
        const cJSON *tx = cJSON_GetObjectItemCaseSensitive(c, "text");
        const cJSON *tg = cJSON_GetObjectItemCaseSensitive(c, "tags");
        const char *tags[8];
        size_t tn = 0;
        if (cJSON_IsArray(tg)) {
            const cJSON *t;
            cJSON_ArrayForEach(t, tg) if (tn < 8) tags[tn++] = t->valuestring;
        }
        bool got = wf_muted_words_match(
            words, n, cJSON_IsString(tx) ? tx->valuestring : NULL,
            cJSON_IsArray(tg) ? tags : NULL, tn,
            cJSON_IsTrue(
                cJSON_GetObjectItemCaseSensitive(c, "author_followed")),
            (int64_t)cJSON_GetObjectItemCaseSensitive(c, "now")->valuedouble);
        bool want = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(c, "match"));
        WF_CHECK(got == want);
        if (got != want)
            fprintf(stderr, "  vector: %s\n",
                    cJSON_GetObjectItemCaseSensitive(c, "name")->valuestring);
        for (i = 0; i < n; i++) {
            free(words[i].value);
            free(words[i].actor_target);
            free(words[i].expires_at);
            for (size_t j = 0; j < words[i].target_count; j++)
                free(words[i].targets[j]);
            free(words[i].targets);
        }
        free(words);
    }
    WF_CHECK(!wf_muted_words_match(NULL, 3, "cat", NULL, 0, false, 1));
    cJSON_Delete(root);
    free(text);
    WF_TEST_SUMMARY();
}
