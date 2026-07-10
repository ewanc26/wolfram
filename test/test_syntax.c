#include "wolfram/syntax.h"
#include "test.h"
#include <string.h>

/* ── DID ─────────────────────────────────────────────────────────────── */

static void test_did_valid(void) {
    WF_CHECK(wf_syntax_did_is_valid("did:plc:z72i7hdynmk6r22z27h6tvur"));
    WF_CHECK(wf_syntax_did_is_valid("did:web:bluesky-social.github.io"));
    WF_CHECK(wf_syntax_did_is_valid("did:key:zQ3shokFTS3brHcDQrn82T5NnV2A5ZDeSjs2zQ4n2QmFJkfY3"));
    WF_CHECK(wf_syntax_did_is_valid("did:plc:ewvi7nxzyq6n7qoy7fe2v4ji"));
    WF_CHECK(wf_syntax_did_is_valid("did:example:123"));
    WF_CHECK(wf_syntax_did_is_valid("did:method:abc.def_ghi%FF"));
    WF_CHECK(wf_syntax_did_is_valid("did:123:abc"));
    WF_CHECK(wf_syntax_did_is_valid("did:m123:val"));
}

static void test_did_invalid(void) {
    WF_CHECK(!wf_syntax_did_is_valid("did:"));
    WF_CHECK(!wf_syntax_did_is_valid("did:method:"));
    WF_CHECK(!wf_syntax_did_is_valid("did:method:abc:"));
    WF_CHECK(!wf_syntax_did_is_valid("did:method:abc%"));
    WF_CHECK(!wf_syntax_did_is_valid("DID:method:abc"));
    WF_CHECK(!wf_syntax_did_is_valid("did:method:a_b.c-d:e%f"));
    WF_CHECK(!wf_syntax_did_is_valid(""));
    WF_CHECK(!wf_syntax_did_is_valid(NULL));
    WF_CHECK(!wf_syntax_did_is_valid("not-a-did"));
    WF_CHECK(!wf_syntax_did_is_valid("did:invalid"));
}

/* ── Handle ──────────────────────────────────────────────────────────── */

static void test_handle_valid(void) {
    WF_CHECK(wf_syntax_handle_is_valid("alice.bsky.social"));
    WF_CHECK(wf_syntax_handle_is_valid("a.b"));
    WF_CHECK(wf_syntax_handle_is_valid("a-b.c-d"));
    WF_CHECK(wf_syntax_handle_is_valid("test.example.com"));
    WF_CHECK(wf_syntax_handle_is_valid("xn--bcher-kva.example"));
    WF_CHECK(wf_syntax_handle_is_valid("1abc.example.com"));
}

static void test_handle_invalid(void) {
    WF_CHECK(!wf_syntax_handle_is_valid("alice"));
    WF_CHECK(!wf_syntax_handle_is_valid(""));
    WF_CHECK(!wf_syntax_handle_is_valid(NULL));
    WF_CHECK(!wf_syntax_handle_is_valid("-a.b"));
    WF_CHECK(!wf_syntax_handle_is_valid("a-.b"));
    WF_CHECK(!wf_syntax_handle_is_valid("a.b-"));
    WF_CHECK(!wf_syntax_handle_is_valid("a.1bc"));
    WF_CHECK(!wf_syntax_handle_is_valid("a..b"));
    WF_CHECK(!wf_syntax_handle_is_valid(".b"));
    WF_CHECK(!wf_syntax_handle_is_valid("a."));
}

/* ── at-identifier ───────────────────────────────────────────────────── */

static void test_at_identifier_valid(void) {
    WF_CHECK(wf_syntax_at_identifier_is_valid("did:plc:z72i7hdynmk6r22z27h6tvur"));
    WF_CHECK(wf_syntax_at_identifier_is_valid("alice.bsky.social"));
}

static void test_at_identifier_invalid(void) {
    WF_CHECK(!wf_syntax_at_identifier_is_valid(""));
    WF_CHECK(!wf_syntax_at_identifier_is_valid(NULL));
    WF_CHECK(!wf_syntax_at_identifier_is_valid("invalid"));
    WF_CHECK(!wf_syntax_at_identifier_is_valid("did:invalid"));
}

/* ── NSID ────────────────────────────────────────────────────────────── */

static void test_nsid_valid(void) {
    WF_CHECK(wf_syntax_nsid_is_valid("com.example.foo"));
    WF_CHECK(wf_syntax_nsid_is_valid("io.example.someThing"));
    WF_CHECK(wf_syntax_nsid_is_valid("org.bsky.app.bsky.feed.post"));
    WF_CHECK(wf_syntax_nsid_is_valid("a.b.c"));
    WF_CHECK(wf_syntax_nsid_is_valid("com.example.fooBar"));
    WF_CHECK(wf_syntax_nsid_is_valid("com.example.FOO"));
}

static void test_nsid_invalid(void) {
    WF_CHECK(!wf_syntax_nsid_is_valid(""));
    WF_CHECK(!wf_syntax_nsid_is_valid(NULL));
    WF_CHECK(!wf_syntax_nsid_is_valid("a.b"));
    WF_CHECK(!wf_syntax_nsid_is_valid("1com.example.foo"));
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.-foo"));
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.foo-"));
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.foo_bar"));
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example."));
    WF_CHECK(!wf_syntax_nsid_is_valid(".com.example.foo"));
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.foo!"));
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.1"));
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.foo-bar"));
}

/* ── Record key ──────────────────────────────────────────────────────── */

static void test_record_key_valid(void) {
    WF_CHECK(wf_syntax_record_key_is_valid("self"));
    WF_CHECK(wf_syntax_record_key_is_valid("3jzfcijpj2z2a"));
    WF_CHECK(wf_syntax_record_key_is_valid("lang:en"));
    WF_CHECK(wf_syntax_record_key_is_valid("~1.2-3_"));
    WF_CHECK(wf_syntax_record_key_is_valid("a"));
    WF_CHECK(wf_syntax_record_key_is_valid("abc123_def.ghi~jkl:mno-pqr"));
}

static void test_record_key_invalid(void) {
    WF_CHECK(!wf_syntax_record_key_is_valid(""));
    WF_CHECK(!wf_syntax_record_key_is_valid(NULL));
    WF_CHECK(!wf_syntax_record_key_is_valid("."));
    WF_CHECK(!wf_syntax_record_key_is_valid(".."));
    WF_CHECK(!wf_syntax_record_key_is_valid("hello world"));
    WF_CHECK(!wf_syntax_record_key_is_valid("hello/world"));
    WF_CHECK(!wf_syntax_record_key_is_valid("hello\nworld"));
}

/* ── TID ─────────────────────────────────────────────────────────────── */

static void test_tid_valid(void) {
    WF_CHECK(wf_syntax_tid_is_valid("3jzfcijpj2z2a"));
    WF_CHECK(wf_syntax_tid_is_valid("7777777777777"));
    WF_CHECK(wf_syntax_tid_is_valid("3zzzzzzzzzzzz"));
    WF_CHECK(wf_syntax_tid_is_valid("2aaaaaaaaaaaa"));
    WF_CHECK(wf_syntax_tid_is_valid("3jzfcijpj2z2l"));
    WF_CHECK(wf_syntax_tid_is_valid("aaaaaaaaaaaaa"));
}

static void test_tid_invalid(void) {
    WF_CHECK(!wf_syntax_tid_is_valid(""));
    WF_CHECK(!wf_syntax_tid_is_valid(NULL));
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z2"));
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z2ab"));
    WF_CHECK(!wf_syntax_tid_is_valid("1jzfcijpj2z2a"));
    WF_CHECK(!wf_syntax_tid_is_valid("8jzfcijpj2z2a"));
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z20"));
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z21"));
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z28"));
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z29"));
}

/* ── AT URI ──────────────────────────────────────────────────────────── */

static void test_aturi_valid(void) {
    wf_syntax_aturi parsed = {0};
    WF_CHECK(wf_syntax_aturi_parse(
        "at://did:plc:z72i7hdynmk6r22z27h6tvur", &parsed));
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(wf_syntax_aturi_parse(
        "at://alice.bsky.social/com.example.post/self", &parsed));
    WF_CHECK(parsed.authority && strcmp(parsed.authority, "alice.bsky.social") == 0);
    WF_CHECK(parsed.collection && strcmp(parsed.collection, "com.example.post") == 0);
    WF_CHECK(parsed.record_key && strcmp(parsed.record_key, "self") == 0);
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(wf_syntax_aturi_parse(
        "at://did:plc:abc/com.example.post", &parsed));
    WF_CHECK(parsed.collection && strcmp(parsed.collection, "com.example.post") == 0);
    WF_CHECK(parsed.record_key == NULL);
    wf_syntax_aturi_free(&parsed);
}

static void test_aturi_invalid(void) {
    wf_syntax_aturi parsed = {0};
    WF_CHECK(!wf_syntax_aturi_parse("", &parsed));
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(!wf_syntax_aturi_parse("at://", &parsed));
    wf_syntax_aturi_free(&parsed);
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(!wf_syntax_aturi_parse("at://a.b/c/d/e", &parsed));
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(!wf_syntax_aturi_parse("not-a-uri", &parsed));
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(!wf_syntax_aturi_parse(NULL, &parsed));
    wf_syntax_aturi_free(&parsed);
}

/* ── Datetime ────────────────────────────────────────────────────────── */

static void test_datetime_valid(void) {
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00Z"));
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00.000Z"));
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00.123456789Z"));
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00+00:00"));
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00.809-05:00"));
    WF_CHECK(wf_syntax_datetime_is_valid("0000-01-01T00:00:00Z"));
    WF_CHECK(wf_syntax_datetime_is_valid("9999-12-31T23:59:59Z"));
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00.1234567890Z"));
}

static void test_datetime_invalid(void) {
    WF_CHECK(!wf_syntax_datetime_is_valid(""));
    WF_CHECK(!wf_syntax_datetime_is_valid(NULL));
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15 12:30:00Z"));
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15T12:30:00"));
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15T12:30:00-00:00"));
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-13-15T12:30:00Z"));
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-32T12:30:00Z"));
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15T24:30:00Z"));
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15T12:30:61Z"));
}

/* ── Language ────────────────────────────────────────────────────────── */

static void test_language_valid(void) {
    WF_CHECK(wf_syntax_language_is_valid("en"));
    WF_CHECK(wf_syntax_language_is_valid("en-US"));
    WF_CHECK(wf_syntax_language_is_valid("zh-Hans-CN"));
    WF_CHECK(wf_syntax_language_is_valid("en-GB-oed"));
    WF_CHECK(wf_syntax_language_is_valid("x-private"));
    WF_CHECK(wf_syntax_language_is_valid("es-419"));
    WF_CHECK(wf_syntax_language_is_valid("de-DE-1901"));
    WF_CHECK(wf_syntax_language_is_valid("ja-Latn"));
}

static void test_language_invalid(void) {
    WF_CHECK(!wf_syntax_language_is_valid(""));
    WF_CHECK(!wf_syntax_language_is_valid(NULL));
    WF_CHECK(!wf_syntax_language_is_valid("-en"));
    WF_CHECK(!wf_syntax_language_is_valid("en--US"));
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void) {
    test_did_valid(); test_did_invalid();
    test_handle_valid(); test_handle_invalid();
    test_at_identifier_valid(); test_at_identifier_invalid();
    test_nsid_valid(); test_nsid_invalid();
    test_record_key_valid(); test_record_key_invalid();
    test_tid_valid(); test_tid_invalid();
    test_aturi_valid(); test_aturi_invalid();
    test_datetime_valid(); test_datetime_invalid();
    test_language_valid(); test_language_invalid();
    WF_TEST_SUMMARY();
}
