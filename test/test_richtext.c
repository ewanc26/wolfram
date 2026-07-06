#include "wolfram/richtext.h"
#include "test.h"

#include <string.h>

/* ── grapheme length ── */
static void test_grapheme_len_ascii(void) {
    WF_CHECK(wf_richtext_grapheme_len("hello", 5) == 5);
    WF_CHECK(wf_richtext_grapheme_len("", 0) == 0);
}

static void test_grapheme_len_multi_byte(void) {
    /* 2-byte: U+00E9 (é) */
    WF_CHECK(wf_richtext_grapheme_len("\xc3\xa9", 2) == 1);
    /* 3-byte: U+4E00 (一) */
    WF_CHECK(wf_richtext_grapheme_len("\xe4\xb8\x80", 3) == 1);
    /* mixed */
    WF_CHECK(wf_richtext_grapheme_len("h\xc3\xa9\xe4\xb8\x80", 6) == 3);
}

/* ── init / free ── */
static void test_init_null(void) {
    WF_CHECK(wf_richtext_init(NULL, "x") == WF_ERR_INVALID_ARG);
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, NULL) == WF_ERR_INVALID_ARG);
}

static void test_init_free_basic(void) {
    wf_richtext rt;
    memset(&rt, 0xff, sizeof(rt));
    WF_CHECK(wf_richtext_init(&rt, "hello") == WF_OK);
    WF_CHECK(rt.text != NULL);
    WF_CHECK(strcmp(rt.text, "hello") == 0);
    WF_CHECK(rt.text_len == 5);
    WF_CHECK(rt.facet_count == 0);
    WF_CHECK(rt.facets == NULL);
    wf_richtext_free(&rt);
}

static void test_free_null(void) {
    wf_richtext_free(NULL);
}

/* ── domain / TLD ── */
static void test_domain_valid(void) {
    WF_CHECK(wf_richtext_is_valid_domain("bsky.social"));
    WF_CHECK(wf_richtext_is_valid_domain("example.com"));
    WF_CHECK(wf_richtext_is_valid_domain("a.b.c.com"));
}

static void test_domain_invalid(void) {
    WF_CHECK(!wf_richtext_is_valid_domain(NULL));
    WF_CHECK(!wf_richtext_is_valid_domain(""));
    WF_CHECK(!wf_richtext_is_valid_domain("no-tld"));
    WF_CHECK(!wf_richtext_is_valid_domain(".com"));
}

static void test_tld_valid(void) {
    WF_CHECK(wf_richtext_is_valid_tld("com"));
    WF_CHECK(wf_richtext_is_valid_tld("org"));
    WF_CHECK(wf_richtext_is_valid_tld("io"));
    WF_CHECK(wf_richtext_is_valid_tld("dev"));
}

static void test_tld_invalid(void) {
    WF_CHECK(!wf_richtext_is_valid_tld(NULL));
    WF_CHECK(!wf_richtext_is_valid_tld(""));
    WF_CHECK(!wf_richtext_is_valid_tld("invalidtld"));
}

/* ── mention detection ── */
static void test_mention_basic(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "hello @bsky.social world") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].byte_start == 6);
    WF_CHECK(rt.facets[0].byte_end == 18);
    WF_CHECK(rt.facets[0].feature_count == 1);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_MENTION);
    wf_richtext_free(&rt);
}

static void test_mention_no_false_positive(void) {
    /* email-like without valid TLD */
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "notreally@notatld") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 0);
    wf_richtext_free(&rt);
}

/* ── link detection ── */
static void test_link_scheme(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "visit https://bsky.social now") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].byte_start == 6);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_LINK);
    wf_richtext_free(&rt);
}

static void test_link_bare_domain(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "check bsky.social for info") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_LINK);
    wf_richtext_free(&rt);
}

/* ── tag detection ── */
static void test_tag_hash(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "this is #ATProtocol") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_TAG);
    WF_CHECK(strcmp(rt.facets[0].features[0].tag, "ATProtocol") == 0);
    wf_richtext_free(&rt);
}

static void test_tag_fullwidth(void) {
    wf_richtext rt;
    memset(&rt, 0, sizeof(rt));
    /* U+FF03 is EF BC 83 in UTF-8 */
    const char *text = "test \xef\xbc\x83" "bluesky end";
    WF_CHECK(wf_richtext_init(&rt, text) == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_TAG);
    wf_richtext_free(&rt);
}

/* ── cashtag detection ── */
static void test_cashtag(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "looking at $AAPL today") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].byte_start == 11);
    WF_CHECK(rt.facets[0].byte_end == 16);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_TAG);
    wf_richtext_free(&rt);
}

/* ── segment iteration ── */
static void test_segment_no_facets(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "hello world") == WF_OK);
    WF_CHECK(wf_richtext_segment_count(&rt) == 1);
    wf_richtext_segment seg = wf_richtext_get_segment(&rt, 0);
    WF_CHECK(seg.text != NULL);
    WF_CHECK(seg.text_len == 11);
    WF_CHECK(seg.facet == NULL);
    /* out of range */
    seg = wf_richtext_get_segment(&rt, 1);
    WF_CHECK(seg.text == NULL);
    wf_richtext_free(&rt);
}

static void test_segment_with_facets(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "hi @bsky.social ok") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    size_t n = wf_richtext_segment_count(&rt);
    WF_CHECK(n == 3);

    wf_richtext_segment s = wf_richtext_get_segment(&rt, 0);
    WF_CHECK(s.text != NULL);
    WF_CHECK(s.facet == NULL);

    s = wf_richtext_get_segment(&rt, 1);
    WF_CHECK(s.text != NULL);
    WF_CHECK(s.facet != NULL);
    WF_CHECK(s.facet->features[0].type == WF_RICHTEXT_FEATURE_MENTION);

    s = wf_richtext_get_segment(&rt, 2);
    WF_CHECK(s.text != NULL);
    WF_CHECK(s.facet == NULL);
    wf_richtext_free(&rt);
}

/* ── insert ── */
static void test_insert_middle(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "ab") == WF_OK);
    WF_CHECK(wf_richtext_insert(&rt, 1, "XX") == WF_OK);
    WF_CHECK(strcmp(rt.text, "aXXb") == 0);
    WF_CHECK(rt.text_len == 4);
    wf_richtext_free(&rt);
}

static void test_insert_adjusts_facets(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "use @bsky.social now") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    uint32_t orig_start = rt.facets[0].byte_start;
    uint32_t orig_end = rt.facets[0].byte_end;

    /* insert before mention */
    WF_CHECK(wf_richtext_insert(&rt, 0, ">>") == WF_OK);
    WF_CHECK(rt.facets[0].byte_start == orig_start + 2);
    WF_CHECK(rt.facets[0].byte_end == orig_end + 2);
    wf_richtext_free(&rt);
}

/* ── delete ── */
static void test_delete_middle(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "hello world") == WF_OK);
    WF_CHECK(wf_richtext_delete(&rt, 2, 5) == WF_OK);
    WF_CHECK(strcmp(rt.text, "he world") == 0);
    wf_richtext_free(&rt);
}

static void test_delete_removes_facet(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "x @bsky.social y") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    /* delete the entire mention */
    uint32_t s = rt.facets[0].byte_start;
    uint32_t e = rt.facets[0].byte_end;
    WF_CHECK(wf_richtext_delete(&rt, s, e) == WF_OK);
    WF_CHECK(rt.facet_count == 0);
    wf_richtext_free(&rt);
}

static void test_delete_null_checks(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "test") == WF_OK);
    WF_CHECK(wf_richtext_delete(NULL, 0, 1) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_richtext_delete(&rt, 1, 0) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_richtext_delete(&rt, 0, 5) == WF_ERR_INVALID_ARG);
    wf_richtext_free(&rt);
}

int main(void) {
    test_grapheme_len_ascii();
    test_grapheme_len_multi_byte();
    test_init_null();
    test_init_free_basic();
    test_free_null();
    test_domain_valid();
    test_domain_invalid();
    test_tld_valid();
    test_tld_invalid();
    test_mention_basic();
    test_mention_no_false_positive();
    test_link_scheme();
    test_link_bare_domain();
    test_tag_hash();
    test_tag_fullwidth();
    test_cashtag();
    test_segment_no_facets();
    test_segment_with_facets();
    test_insert_middle();
    test_insert_adjusts_facets();
    test_delete_middle();
    test_delete_removes_facet();
    test_delete_null_checks();
    WF_TEST_SUMMARY();
}
