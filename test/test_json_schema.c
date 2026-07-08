#include "wolfram/json.h"
#include "test.h"
#include <string.h>
#include <stdlib.h>

static void check_valid(const char *schema, const char *doc) {
    char *err = NULL;
    wf_status s = wf_json_validate(schema, strlen(schema), doc, strlen(doc), &err);
    if (err) free(err);
    WF_CHECK(s == WF_OK);
}

static void check_invalid(const char *schema, const char *doc) {
    char *err = NULL;
    wf_status s = wf_json_validate(schema, strlen(schema), doc, strlen(doc), &err);
    WF_CHECK(s != WF_OK);
    WF_CHECK(err != NULL);
    if (err) free(err);
}

static void test_enum_const(void) {
    const char *sch = "{\"enum\":[1,2,3]}";
    check_valid(sch, "2");
    check_invalid(sch, "5");

    const char *schs = "{\"enum\":[\"a\",\"b\"]}";
    check_valid(schs, "\"a\"");
    check_invalid(schs, "\"c\"");

    const char *c = "{\"const\":42}";
    check_valid(c, "42");
    check_invalid(c, "43");

    const char *cs = "{\"const\":\"x\"}";
    check_valid(cs, "\"x\"");
    check_invalid(cs, "\"y\"");
}

static void test_format(void) {
    const char *dt = "{\"format\":\"date-time\"}";
    check_valid(dt, "\"2024-01-02T03:04:05Z\"");
    check_invalid(dt, "\"not-a-date\"");

    const char *em = "{\"format\":\"email\"}";
    check_valid(em, "\"a@b.com\"");
    check_invalid(em, "\"no-at-sign\"");

    const char *uri = "{\"format\":\"uri\"}";
    check_valid(uri, "\"https://example.com\"");
    check_invalid(uri, "\"example.com\"");

    const char *hn = "{\"format\":\"hostname\"}";
    check_valid(hn, "\"example.com\"");
    check_invalid(hn, "\".bad.\"");

    const char *unk = "{\"format\":\"unknown-format\"}";
    check_valid(unk, "\"anything\"");

    const char *nonstring = "{\"format\":\"date-time\"}";
    check_valid(nonstring, "5");
}

static void test_numeric(void) {
    const char *min = "{\"minimum\":10}";
    check_valid(min, "10");
    check_valid(min, "11");
    check_invalid(min, "9");

    const char *exmin = "{\"exclusiveMinimum\":10}";
    check_valid(exmin, "11");
    check_invalid(exmin, "10");

    const char *max = "{\"maximum\":10}";
    check_valid(max, "10");
    check_invalid(max, "11");

    const char *exmax = "{\"exclusiveMaximum\":10}";
    check_valid(exmax, "9");
    check_invalid(exmax, "10");

    const char *mult = "{\"multipleOf\":3}";
    check_valid(mult, "9");
    check_invalid(mult, "10");
}

static void test_string(void) {
    const char *minlen = "{\"minLength\":3}";
    check_valid(minlen, "\"abc\"");
    check_invalid(minlen, "\"ab\"");

    const char *maxlen = "{\"maxLength\":3}";
    check_valid(maxlen, "\"abc\"");
    check_invalid(maxlen, "\"abcd\"");

    const char *pat = "{\"pattern\":\"^a.*z$\"}";
    check_valid(pat, "\"abcz\"");
    check_invalid(pat, "\"abc\"");

    const char *badpat = "{\"pattern\":\"[[\"}";
    check_invalid(badpat, "\"x\"");
}

static void test_array(void) {
    const char *mini = "{\"minItems\":2}";
    check_valid(mini, "[1,2]");
    check_invalid(mini, "[1]");

    const char *maxi = "{\"maxItems\":2}";
    check_valid(maxi, "[1,2]");
    check_invalid(maxi, "[1,2,3]");

    const char *uni = "{\"uniqueItems\":true}";
    check_valid(uni, "[1,2,3]");
    check_invalid(uni, "[1,1,3]");
}

static void test_additional(void) {
    const char *sch = "{\"type\":\"object\",\"properties\":{\"a\":{}},"
                      "\"additionalProperties\":false}";
    check_valid(sch, "{\"a\":1}");
    check_invalid(sch, "{\"a\":1,\"b\":2}");

    const char *sch2 = "{\"type\":\"object\",\"properties\":{\"a\":{}},"
                       "\"additionalProperties\":{\"type\":\"string\"}}";
    check_valid(sch2, "{\"a\":1,\"b\":\"ok\"}");
    check_invalid(sch2, "{\"a\":1,\"b\":5}");
}

static void test_combinators(void) {
    const char *any = "{\"anyOf\":[{\"type\":\"string\"},{\"type\":\"number\"}]}";
    check_valid(any, "\"x\"");
    check_valid(any, "5");
    check_invalid(any, "true");

    const char *one = "{\"oneOf\":[{\"minimum\":0},{\"maximum\":0}]}";
    check_valid(one, "5");
    check_invalid(one, "0");
    check_invalid(one, "true");

    const char *no = "{\"not\":{\"type\":\"string\"}}";
    check_valid(no, "5");
    check_invalid(no, "\"x\"");
}

int main(void) {
    test_enum_const();
    test_format();
    test_numeric();
    test_string();
    test_array();
    test_additional();
    test_combinators();
    WF_TEST_SUMMARY();
}
