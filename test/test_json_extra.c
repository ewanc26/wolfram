#include "wolfram/json.h"
#include "test.h"
#include <string.h>
#include <stdlib.h>

/* ── canonicalize: key order is preserved (round-trip, not sorted) ── */
static void test_canonicalize_order_preserved(void) {
    const char *in = "{\"z\":1,\"a\":2,\"m\":[3,4]}";
    char *out = NULL;
    wf_status s = wf_json_canonicalize(in, strlen(in), &out);
    WF_CHECK(s == WF_OK);
    WF_CHECK(out != NULL);
    WF_CHECK(strcmp(out, "{\"z\":1,\"a\":2,\"m\":[3,4]}") == 0);
    free(out);
}

static void test_canonicalize_nested(void) {
    const char *in = " { \"outer\" : { \"inner\" : { \"k\" : \"v\" } } } ";
    char *out = NULL;
    wf_status s = wf_json_canonicalize(in, strlen(in), &out);
    WF_CHECK(s == WF_OK);
    WF_CHECK(strcmp(out, "{\"outer\":{\"inner\":{\"k\":\"v\"}}}") == 0);
    free(out);
}

static void test_canonicalize_arrays(void) {
    const char *in = " [ 1 , 2 , [ 3 , 4 ] , \"five\" ] ";
    char *out = NULL;
    wf_status s = wf_json_canonicalize(in, strlen(in), &out);
    WF_CHECK(s == WF_OK);
    WF_CHECK(strcmp(out, "[1,2,[3,4],\"five\"]") == 0);
    free(out);
}

static void test_canonicalize_whitespace_and_newlines(void) {
    const char *in = "{\n  \"a\": 1,\t\"b\":\n2\n}\n";
    char *out = NULL;
    wf_status s = wf_json_canonicalize(in, strlen(in), &out);
    WF_CHECK(s == WF_OK);
    WF_CHECK(strcmp(out, "{\"a\":1,\"b\":2}") == 0);
    free(out);
}

static void test_canonicalize_empty_containers(void) {
    const char *e1 = "{}";
    char *o1 = NULL;
    WF_CHECK(wf_json_canonicalize(e1, strlen(e1), &o1) == WF_OK);
    WF_CHECK(strcmp(o1, "{}") == 0);
    free(o1);

    const char *e2 = "[]";
    char *o2 = NULL;
    WF_CHECK(wf_json_canonicalize(e2, strlen(e2), &o2) == WF_OK);
    WF_CHECK(strcmp(o2, "[]") == 0);
    free(o2);
}

static void test_canonicalize_invalid(void) {
    const char *cases[] = {
        "{ not valid json ",
        "{\"unterminated\":",
        "1 2 trailing",
        "",
        "{\"a\": }",
        "[1,2",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *out = NULL;
        wf_status s = wf_json_canonicalize(cases[i], strlen(cases[i]), &out);
        WF_CHECK(s == WF_ERR_PARSE);
        WF_CHECK(out == NULL);
    }
}

static void test_canonicalize_null_args(void) {
    char *out = NULL;
    WF_CHECK(wf_json_canonicalize(NULL, 0, &out) == WF_ERR_INVALID_ARG);
    const char *in = "{}";
    WF_CHECK(wf_json_canonicalize(in, strlen(in), NULL) == WF_ERR_INVALID_ARG);
}

/* ── validate: nested properties + required ── */
static void test_validate_nested(void) {
    const char *sch =
        "{\"type\":\"object\","
         "\"required\":[\"user\"],"
         "\"properties\":{"
         "  \"user\":{\"type\":\"object\",\"required\":[\"id\"],"
         "    \"properties\":{\"id\":{\"type\":\"string\"},"
         "      \"age\":{\"type\":\"number\"}}}}}";
    const char *ok = "{\"user\":{\"id\":\"x\",\"age\":4}}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), ok, strlen(ok), &err) == WF_OK);
    if (err) free(err);

    const char *missing_inner = "{\"user\":{\"age\":4}}";
    err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), missing_inner,
                              strlen(missing_inner), &err) != WF_OK);
    if (err) free(err);

    const char *missing_outer = "{\"other\":{}}";
    err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), missing_outer,
                              strlen(missing_outer), &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: items applied to every element ── */
static void test_validate_items_objects(void) {
    const char *sch =
        "{\"type\":\"array\","
         "\"items\":{\"type\":\"object\",\"required\":[\"v\"],"
         "  \"properties\":{\"v\":{\"type\":\"number\"}}}}";
    const char *ok = "[{\"v\":1},{\"v\":2},{\"v\":3}]";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), ok, strlen(ok), &err) == WF_OK);
    if (err) free(err);

    const char *bad = "[{\"v\":1},{\"v\":\"x\"}]";
    err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), bad, strlen(bad), &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: additionalProperties as schema ── */
static void test_validate_additional_schema(void) {
    const char *sch =
        "{\"type\":\"object\",\"properties\":{\"known\":{\"type\":\"number\"}},"
         "\"additionalProperties\":{\"type\":\"string\"}}";
    const char *ok = "{\"known\":1,\"extra\":\"hi\"}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), ok, strlen(ok), &err) == WF_OK);
    if (err) free(err);

    const char *bad = "{\"known\":1,\"extra\":5}";
    err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), bad, strlen(bad), &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: enum / const with booleans and objects ── */
static void test_validate_enum_bool(void) {
    const char *sch = "{\"enum\":[true,false]}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), "true", 4, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "false", 5, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "null", 4, &err) != WF_OK);
    if (err) free(err);
}

static void test_validate_const_object(void) {
    const char *c = "{\"const\":{\"a\":1,\"b\":2}}";
    const char *ok = "{\"b\":2,\"a\":1}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(c, strlen(c), ok, strlen(ok), &err) == WF_OK);
    if (err) free(err);
    const char *bad = "{\"a\":1,\"b\":3}";
    WF_CHECK(wf_json_validate(c, strlen(c), bad, strlen(bad), &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: oneOf exactly-one and not ── */
static void test_validate_oneof_and_not(void) {
    const char *one =
        "{\"oneOf\":[{\"type\":\"string\"},{\"type\":\"number\"}]}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(one, strlen(one), "\"x\"", 3, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(one, strlen(one), "5", 1, &err) == WF_OK);
    if (err) free(err);
    /* both match (number is also accepted? no, number not string -> exactly one) */
    WF_CHECK(wf_json_validate(one, strlen(one), "true", 4, &err) != WF_OK);
    if (err) free(err);

    const char *no = "{\"not\":{\"enum\":[1,2,3]}}";
    WF_CHECK(wf_json_validate(no, strlen(no), "9", 1, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(no, strlen(no), "2", 1, &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: multipleOf ── */
static void test_validate_multiple_of(void) {
    const char *sch = "{\"multipleOf\":4}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), "12", 2, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "8", 1, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "14", 2, &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: pattern is case-sensitive, anchors matter ── */
static void test_validate_pattern_anchors(void) {
    const char *sch = "{\"pattern\":\"^[0-9]+$\"}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), "\"123\"", 5, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "\"12a3\"", 6, &err) != WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "\"abc123\"", 7, &err) != WF_OK);
    if (err) free(err);

    const char *casey = "{\"pattern\":\"abc\"}";
    WF_CHECK(wf_json_validate(casey, strlen(casey), "\"ABC\"", 5, &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: numeric boundary inclusivity ── */
static void test_validate_numeric_bounds(void) {
    const char *sch = "{\"minimum\":0,\"maximum\":10}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), "0", 1, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "10", 2, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "11", 2, &err) != WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "-1", 2, &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: format uri requires scheme separator ── */
static void test_validate_format_uri(void) {
    const char *sch = "{\"format\":\"uri\"}";
    char *err = NULL;
    WF_CHECK(wf_json_validate(sch, strlen(sch), "\"http://x\"", 10, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "\"ftp://x\"", 9, &err) == WF_OK);
    if (err) free(err);
    WF_CHECK(wf_json_validate(sch, strlen(sch), "\"example.com\"", 13, &err) != WF_OK);
    if (err) free(err);
}

/* ── validate: null args / malformed schema ── */
static void test_validate_null_args(void) {
    char *err = NULL;
    WF_CHECK(wf_json_validate(NULL, 0, "1", 1, &err) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_json_validate("{}", 2, NULL, 0, &err) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_json_validate("{}", 2, "1", 1, NULL) == WF_ERR_INVALID_ARG);

    const char *badschema = "{not json";
    WF_CHECK(wf_json_validate(badschema, strlen(badschema), "1", 1, &err) ==
             WF_ERR_INVALID_ARG);
    if (err) free(err);
}

int main(void) {
    test_canonicalize_order_preserved();
    test_canonicalize_nested();
    test_canonicalize_arrays();
    test_canonicalize_whitespace_and_newlines();
    test_canonicalize_empty_containers();
    test_canonicalize_invalid();
    test_canonicalize_null_args();
    test_validate_nested();
    test_validate_items_objects();
    test_validate_additional_schema();
    test_validate_enum_bool();
    test_validate_const_object();
    test_validate_oneof_and_not();
    test_validate_multiple_of();
    test_validate_pattern_anchors();
    test_validate_numeric_bounds();
    test_validate_format_uri();
    test_validate_null_args();
    WF_TEST_SUMMARY();
}
