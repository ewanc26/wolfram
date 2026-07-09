#include "wolfram/richtext.h"
#include "test.h"

#include <string.h>

/* ── grapheme length counts UTF-8 codepoints ── */
static void test_grapheme_len_codepoints(void) {
    WF_CHECK(wf_richtext_grapheme_len("a", 1) == 1);
    WF_CHECK(wf_richtext_grapheme_len("h\xc3\xa9llo", 6) == 5); /* é counts as 1 */
    /* emoji U+1F642 (F0 9F 99 82) is one codepoint */
    WF_CHECK(wf_richtext_grapheme_len("\xf0\x9f\x99\x82", 4) == 1);
    /* 'a' + emoji + 'b' = 3 codepoints */
    WF_CHECK(wf_richtext_grapheme_len("a\xf0\x9f\x99\x82" "b", 6) == 3);
    /* e + combining acute (U+0301, CC 81) = 2 codepoints */
    WF_CHECK(wf_richtext_grapheme_len("e\xcc\x81", 3) == 2);
    WF_CHECK(wf_richtext_grapheme_len("", 0) == 0);
}

/* ── mention preceded by a multi-byte char keeps correct byte offsets ── */
static void test_mention_multibyte_prefix(void) {
    /* é (C3 A9) + space + @bsky.social */
    const char *text = "\xc3\xa9 @bsky.social";
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, text) == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].byte_start == 3); /* after é(2)+space(1) */
    WF_CHECK(rt.facets[0].byte_end == 15);  /* @bsky.social is 12 bytes */
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_MENTION);
    wf_richtext_free(&rt);
}

/* ── multiple mentions ── */
static void test_mention_multiple(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "@a.bsky.social and @b.bsky.social") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 2);
    WF_CHECK(rt.facets[0].byte_start == 0);
    WF_CHECK(rt.facets[1].byte_start == 19); /* "@a.bsky.social and " is 19 bytes */
    wf_richtext_free(&rt);
}

/* ── mention with subdomain handle ── */
static void test_mention_subdomain(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "@user.example.com") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_MENTION);
    wf_richtext_free(&rt);
}

/* ── mention false positive: handle without valid TLD ── */
static void test_mention_no_tld(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "@foo.localhost") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 0);
    wf_richtext_free(&rt);
}

/* ── link: scheme URL with trailing punctuation stripped ── */
static void test_link_trailing_punct(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "see https://bsky.social.") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_LINK);
    WF_CHECK(rt.facets[0].byte_start == 4);
    size_t ulen = strlen(rt.facets[0].features[0].uri);
    WF_CHECK(rt.facets[0].byte_end == 4 + ulen);
    WF_CHECK(rt.facets[0].byte_end == 23); /* "https://bsky.social" is 19 bytes */
    WF_CHECK(rt.facets[0].features[0].uri[ulen - 1] != '.');
    wf_richtext_free(&rt);
}

/* ── link: bare domain with path + trailing comma stripped ── */
static void test_link_bare_trailing_comma(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "go to bsky.social/x,") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_LINK);
    WF_CHECK(rt.facets[0].features[0].uri[0] == 'h'); /* https:// prefix */
    WF_CHECK(strstr(rt.facets[0].features[0].uri, "bsky.social/x") != NULL);
    size_t ulen = strlen(rt.facets[0].features[0].uri);
    WF_CHECK(rt.facets[0].features[0].uri[ulen - 1] != ',');
    wf_richtext_free(&rt);
}

/* ── tag: fullwidth hash byte offset and capture ── */
static void test_tag_fullwidth_offset(void) {
    wf_richtext rt;
    const char *text = "test \xef\xbc\x83" "bluesky end"; /* U+FF03 then "bluesky" */
    WF_CHECK(wf_richtext_init(&rt, text) == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].byte_start == 5); /* index of U+FF03 */
    WF_CHECK(rt.facets[0].features[0].type == WF_RICHTEXT_FEATURE_TAG);
    WF_CHECK(strcmp(rt.facets[0].features[0].tag, "bluesky") == 0);
    wf_richtext_free(&rt);
}

/* ── tag: ascii hash, multi-word stops at space ── */
static void test_tag_ascii_multibyte(void) {
    wf_richtext rt;
    /* "#café" -> tag "café" (é is 2 bytes) */
    WF_CHECK(wf_richtext_init(&rt, "#caf\xc3\xa9") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(strcmp(rt.facets[0].features[0].tag, "caf\xc3\xa9") == 0);
    wf_richtext_free(&rt);
}

/* ── cashtag: length bounds ── */
static void test_cashtag_bounds(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "$AAPL") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(strcmp(rt.facets[0].features[0].tag, "$AAPL") == 0);
    wf_richtext_free(&rt);

    WF_CHECK(wf_richtext_init(&rt, "$ABCDEFG") == WF_OK); /* 7 letters > 5 */
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 0);
    wf_richtext_free(&rt);

    WF_CHECK(wf_richtext_init(&rt, "$1ABC") == WF_OK); /* must start with letter */
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 0);
    wf_richtext_free(&rt);
}

/* ── insert before a facet with multi-byte text adjusts offsets ── */
static void test_insert_multibyte_adjusts(void) {
    wf_richtext rt;
    const char *text = "\xc3\xa9 @bsky.social"; /* é + space + mention */
    WF_CHECK(wf_richtext_init(&rt, text) == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    uint32_t s = rt.facets[0].byte_start;
    uint32_t e = rt.facets[0].byte_end;
    WF_CHECK(wf_richtext_insert(&rt, 0, ">>") == WF_OK);
    WF_CHECK(rt.facets[0].byte_start == s + 2);
    WF_CHECK(rt.facets[0].byte_end == e + 2);
    wf_richtext_free(&rt);
}

/* ── delete a non-overlapping range keeps the facet ── */
static void test_delete_outside_facet(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "xx @bsky.social yy") == WF_OK);
    WF_CHECK(wf_richtext_detect_facets(&rt) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    /* delete the leading "xx " (3 bytes) */
    WF_CHECK(wf_richtext_delete(&rt, 0, 3) == WF_OK);
    WF_CHECK(rt.facet_count == 1);
    WF_CHECK(rt.facets[0].byte_start == 0);
    wf_richtext_free(&rt);
}

/* ── sanitize collapse of a long newline run ── */
static void test_sanitize_long_run(void) {
    wf_richtext rt;
    WF_CHECK(wf_richtext_init(&rt, "a\n\n\n\n\nb") == WF_OK);
    WF_CHECK(wf_richtext_sanitize(&rt, 1) == WF_OK);
    WF_CHECK(strcmp(rt.text, "a\n\nb") == 0);
    wf_richtext_free(&rt);
}

int main(void) {
    test_grapheme_len_codepoints();
    test_mention_multibyte_prefix();
    test_mention_multiple();
    test_mention_subdomain();
    test_mention_no_tld();
    test_link_trailing_punct();
    test_link_bare_trailing_comma();
    test_tag_fullwidth_offset();
    test_tag_ascii_multibyte();
    test_cashtag_bounds();
    test_insert_multibyte_adjusts();
    test_delete_outside_facet();
    test_sanitize_long_run();
    WF_TEST_SUMMARY();
}
