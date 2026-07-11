#include "wolfram/validate.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* Lexicon with three exact-grapheme-count string definitions so a passing
 * validation proves the grapheme count is exactly N (minGraphemes == maxGraphemes). */
static const char *LEX =
    "{\"lexicon\":1,\"id\":\"com.example.grapheme\",\"defs\":{"
    "\"g1\":{\"type\":\"string\",\"minGraphemes\":1,\"maxGraphemes\":1},"
    "\"g2\":{\"type\":\"string\",\"minGraphemes\":2,\"maxGraphemes\":2},"
    "\"g5\":{\"type\":\"string\",\"minGraphemes\":5,\"maxGraphemes\":5}"
    "}}";

/* Wrap a raw (UTF-8) C string into a JSON string and validate it against the
 * named grapheme-count definition. */
static int grapheme_ok(wf_lexicon_registry *r, const char *def, const char *s) {
    size_t cap = strlen(s) + 3;
    char *buf = (char *)malloc(cap);
    wf_validate_result res;
    int ok;
    if (!buf) return 0;
    snprintf(buf, cap, "\"%s\"", s);
    res = wf_validate_value(r, "com.example.grapheme", def, buf, strlen(buf));
    ok = res.success;
    wf_validate_result_free(&res);
    free(buf);
    return ok;
}

static void test_grapheme_clusters(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);
    if (!r) return;
    WF_CHECK(wf_lexicon_registry_load(r, LEX, strlen(LEX)) == WF_OK);

    /* (a) base + combining mark == 1 grapheme (would be 2 code points). */
    WF_CHECK(grapheme_ok(r, "g1", "a\u0301") == 1);          /* á as a + combining */
    WF_CHECK(grapheme_ok(r, "g1", "a\u0301b") == 0);         /* 2 graphemes */

    /* (b) ZWJ family sequence == 1 grapheme. */
    WF_CHECK(grapheme_ok(r, "g1", "\U0001F468\U0000200D\U0001F469\U0000200D\U0001F467") == 1);
    WF_CHECK(grapheme_ok(r, "g2", "\U0001F468\U0000200D\U0001F469\U0000200D\U0001F467") == 0);

    /* (c) emoji base + emoji modifier (skin tone) == 1 grapheme. */
    WF_CHECK(grapheme_ok(r, "g1", "\U0001F44B\U0001F3FD") == 1);   /* waving hand + skin tone */
    WF_CHECK(grapheme_ok(r, "g2", "\U0001F44B\U0001F3FD") == 0);

    /* (d) regional-indicator pair == 1 flag grapheme. */
    WF_CHECK(grapheme_ok(r, "g1", "\U0001F1FA\U0001F1F8") == 1);   /* US flag */
    WF_CHECK(grapheme_ok(r, "g1", "\U0001F1FA\U0001F1F8\U0001F1FA\U0001F1F8") == 0); /* 2 flags */
    WF_CHECK(grapheme_ok(r, "g2", "\U0001F1FA\U0001F1F8\U0001F1FA\U0001F1F8") == 1);

    /* Plain ASCII: grapheme count == code point count == byte length. */
    WF_CHECK(grapheme_ok(r, "g5", "hello") == 1);
    WF_CHECK(grapheme_ok(r, "g5", "hello!") == 0);

    wf_lexicon_registry_free(r);
}

int main(void) {
    test_grapheme_clusters();
    WF_TEST_SUMMARY();
}
