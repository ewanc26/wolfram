#include "wolfram/syntax.h"
#include "test.h"
#include <string.h>

/* ── DID: additional cases ── */
static void test_did_valid_extra(void) {
    WF_CHECK(wf_syntax_did_is_valid("did:web:example.com"));
    WF_CHECK(wf_syntax_did_is_valid("did:a:bc"));
    WF_CHECK(wf_syntax_did_is_valid("did:method:ab.cd"));
    WF_CHECK(wf_syntax_did_is_valid("did:key:z6MkhaXgBZDvotDkL5257faiztiGiC2QtKLGpbnnSKS1Hoxg"));
}

static void test_did_invalid_extra(void) {
    WF_CHECK(!wf_syntax_did_is_valid("did:PLC:abc"));      /* method must be lowercase */
    WF_CHECK(!wf_syntax_did_is_valid("did:method:abc def"));
    WF_CHECK(!wf_syntax_did_is_valid("did:method:abc@"));   /* @ not allowed */
    WF_CHECK(!wf_syntax_did_is_valid("did:method:abc/def")); /* '/' not allowed */
    WF_CHECK(!wf_syntax_did_is_valid("did:"));
    WF_CHECK(!wf_syntax_did_is_valid("did:method:abc "));   /* trailing space */
}

/* ── Handle: additional cases ── */
static void test_handle_valid_extra(void) {
    WF_CHECK(wf_syntax_handle_is_valid("Example.com"));        /* uppercase allowed */
    WF_CHECK(wf_syntax_handle_is_valid("sub.domain.example.org"));
    WF_CHECK(wf_syntax_handle_is_valid("a-b.c-d.e-f"));
    WF_CHECK(wf_syntax_handle_is_valid("123.example.com"));    /* digit-start label */
    WF_CHECK(wf_syntax_handle_is_valid("example.c"));          /* single-char TLD */
}

static void test_handle_invalid_extra(void) {
    WF_CHECK(!wf_syntax_handle_is_valid("example.com."));  /* trailing dot */
    WF_CHECK(!wf_syntax_handle_is_valid("example..com"));
    WF_CHECK(!wf_syntax_handle_is_valid("exa mple.com"));  /* space */
    WF_CHECK(!wf_syntax_handle_is_valid(".example.com"));
    WF_CHECK(!wf_syntax_handle_is_valid("example"));
}

/* ── at-identifier ── */
static void test_at_identifier_valid_extra(void) {
    WF_CHECK(wf_syntax_at_identifier_is_valid("did:web:example.com"));
    WF_CHECK(wf_syntax_at_identifier_is_valid("Example.com"));
    WF_CHECK(wf_syntax_at_identifier_is_valid("did:plc:z72i7hdynmk6r22z27h6tvur"));
}

static void test_at_identifier_invalid_extra(void) {
    WF_CHECK(!wf_syntax_at_identifier_is_valid("did:method:abc def"));
    WF_CHECK(!wf_syntax_at_identifier_is_valid("Example.com."));
    WF_CHECK(!wf_syntax_at_identifier_is_valid("did:web:"));
}

/* ── NSID ── */
static void test_nsid_valid_extra(void) {
    WF_CHECK(wf_syntax_nsid_is_valid("com.example.foo.bar"));
    WF_CHECK(wf_syntax_nsid_is_valid("net.example.thing"));
    WF_CHECK(wf_syntax_nsid_is_valid("co.example.foo"));
    WF_CHECK(wf_syntax_nsid_is_valid("com.ex-ample.foo"));
}

static void test_nsid_invalid_extra(void) {
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.foo."));     /* trailing dot */
    WF_CHECK(!wf_syntax_nsid_is_valid("com..example.foo"));      /* empty middle */
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example"));          /* only 2 segments */
    WF_CHECK(!wf_syntax_nsid_is_valid("com.example.foo-bar"));  /* trailing dash... */
}

/* ── Record key ── */
static void test_record_key_valid_extra(void) {
    WF_CHECK(wf_syntax_record_key_is_valid("a~b"));
    WF_CHECK(wf_syntax_record_key_is_valid("x.y.z"));
    WF_CHECK(wf_syntax_record_key_is_valid("123"));
    WF_CHECK(wf_syntax_record_key_is_valid("hello_world"));
    WF_CHECK(wf_syntax_record_key_is_valid(":colon:"));
}

static void test_record_key_invalid_extra(void) {
    WF_CHECK(!wf_syntax_record_key_is_valid("a b"));     /* space */
    WF_CHECK(!wf_syntax_record_key_is_valid("a\\b"));    /* backslash */
    WF_CHECK(!wf_syntax_record_key_is_valid("a#b"));     /* hash */
    WF_CHECK(!wf_syntax_record_key_is_valid("a=b"));     /* equals */
    WF_CHECK(!wf_syntax_record_key_is_valid("a!b"));     /* bang */
}

/* ── TID ── */
static void test_tid_valid_extra(void) {
    WF_CHECK(wf_syntax_tid_is_valid("2cccccccccccc"));
    WF_CHECK(wf_syntax_tid_is_valid("4bbbbbbbbbbbb"));
    WF_CHECK(wf_syntax_tid_is_valid("3aaaaaaaaaaaa"));
}

static void test_tid_invalid_extra(void) {
    WF_CHECK(!wf_syntax_tid_is_valid("0jzfcijpj2z2a"));   /* first char 0 */
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z2!"));   /* illegal char */
    WF_CHECK(!wf_syntax_tid_is_valid("3jzfcijpj2z2 "));   /* trailing space */
    WF_CHECK(!wf_syntax_tid_is_valid("kjzfcijpj2z2a"));   /* k not valid first */
}

/* ── AT URI ── */
static void test_aturi_valid_extra(void) {
    wf_syntax_aturi parsed = {0};
    WF_CHECK(wf_syntax_aturi_parse(
        "at://did:plc:abc/com.example.post/self#frag", &parsed));
    WF_CHECK(parsed.authority && strcmp(parsed.authority, "did:plc:abc") == 0);
    WF_CHECK(parsed.collection && strcmp(parsed.collection, "com.example.post") == 0);
    WF_CHECK(parsed.record_key && strcmp(parsed.record_key, "self") == 0);
    WF_CHECK(parsed.fragment && strcmp(parsed.fragment, "frag") == 0);
    wf_syntax_aturi_free(&parsed);
}

static void test_aturi_invalid_extra(void) {
    wf_syntax_aturi parsed = {0};
    WF_CHECK(!wf_syntax_aturi_parse("at://did:plc:abc/com.example.post/extra/too", &parsed));
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(!wf_syntax_aturi_parse("at://alice.bsky.social/bad_nsid/self", &parsed));
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(!wf_syntax_aturi_parse("at://did:plc:abc//self", &parsed));
    wf_syntax_aturi_free(&parsed);
    WF_CHECK(!wf_syntax_aturi_parse("at://did:plc:abc/com.example.post/", &parsed));
    wf_syntax_aturi_free(&parsed);
}

/* ── Datetime ── */
static void test_datetime_valid_extra(void) {
    WF_CHECK(wf_syntax_datetime_is_valid("2024-02-29T00:00:00Z"));   /* leap day */
    WF_CHECK(wf_syntax_datetime_is_valid("2024-12-31T23:59:60Z"));   /* leap second */
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00+05:30"));
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00-08:00"));
    WF_CHECK(wf_syntax_datetime_is_valid("2024-01-15T12:30:00.5Z"));
}

static void test_datetime_invalid_extra(void) {
    WF_CHECK(!wf_syntax_datetime_is_valid("2023-02-29T00:00:00Z"));  /* not leap */
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15T25:00:00Z"));  /* hour 25 */
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15T12:30:00+24:00"));
    WF_CHECK(!wf_syntax_datetime_is_valid("24-01-15T12:30:00Z"));    /* 2-digit year */
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-1-15T12:30:00Z"));   /* 1-digit month */
    WF_CHECK(!wf_syntax_datetime_is_valid("2024-01-15T12:30:00Zextra"));
}

/* ── Language ── */
static void test_language_valid_extra(void) {
    WF_CHECK(wf_syntax_language_is_valid("fr-FR"));
    WF_CHECK(wf_syntax_language_is_valid("pt-BR"));
    WF_CHECK(wf_syntax_language_is_valid("zh-CN"));
    WF_CHECK(wf_syntax_language_is_valid("en-Latn-US"));
    WF_CHECK(wf_syntax_language_is_valid("i-klingon"));   /* grandfathered */
    WF_CHECK(wf_syntax_language_is_valid("x-abc"));       /* private use */
}

static void test_language_invalid_extra(void) {
    WF_CHECK(!wf_syntax_language_is_valid("123"));        /* first subtag not alpha */
    WF_CHECK(!wf_syntax_language_is_valid("en-1"));       /* bad second subtag */
    WF_CHECK(!wf_syntax_language_is_valid("x"));          /* private use needs >=2 */
    WF_CHECK(!wf_syntax_language_is_valid("en--US"));
    WF_CHECK(!wf_syntax_language_is_valid("a"));
}

int main(void) {
    test_did_valid_extra(); test_did_invalid_extra();
    test_handle_valid_extra(); test_handle_invalid_extra();
    test_at_identifier_valid_extra(); test_at_identifier_invalid_extra();
    test_nsid_valid_extra(); test_nsid_invalid_extra();
    test_record_key_valid_extra(); test_record_key_invalid_extra();
    test_tid_valid_extra(); test_tid_invalid_extra();
    test_aturi_valid_extra(); test_aturi_invalid_extra();
    test_datetime_valid_extra(); test_datetime_invalid_extra();
    test_language_valid_extra(); test_language_invalid_extra();
    WF_TEST_SUMMARY();
}
