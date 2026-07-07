#include "wolfram/json.h"
#include "test.h"
#include <string.h>
#include <stdlib.h>

static void test_canonicalize(void) {
    const char *in = "  { \"b\" : 1 , \"a\" : 2 }  ";
    char *out = NULL;
    wf_status s = wf_json_canonicalize(in, strlen(in), &out);
    WF_CHECK(s == WF_OK);
    WF_CHECK(out != NULL);
    WF_CHECK(strcmp(out, "{\"b\":1,\"a\":2}") == 0);
    free(out);

    const char *bad = "{ not valid json ";
    char *out2 = NULL;
    wf_status s2 = wf_json_canonicalize(bad, strlen(bad), &out2);
    WF_CHECK(s2 == WF_ERR_PARSE);
    WF_CHECK(out2 == NULL);
}

static void test_validate_pass(void) {
    const char *schema =
        "{\"type\":\"object\",\"required\":[\"name\"],"
        "\"properties\":{\"name\":{\"type\":\"string\"},\"age\":{\"type\":\"number\"}}}";
    const char *doc = "{\"name\":\"x\",\"age\":3}";
    char *err = NULL;
    wf_status s = wf_json_validate(schema, strlen(schema), doc, strlen(doc), &err);
    WF_CHECK(s == WF_OK);
    WF_CHECK(err == NULL);
}

static void test_validate_fail(void) {
    const char *schema =
        "{\"type\":\"object\",\"required\":[\"name\"],"
        "\"properties\":{\"name\":{\"type\":\"string\"},\"age\":{\"type\":\"number\"}}}";

    /* missing required */
    const char *doc_missing = "{\"age\":3}";
    char *err = NULL;
    wf_status s = wf_json_validate(schema, strlen(schema), doc_missing, strlen(doc_missing), &err);
    WF_CHECK(s != WF_OK);
    WF_CHECK(err != NULL);
    free(err);

    /* wrong type */
    const char *doc_wrong = "{\"name\":5}";
    err = NULL;
    s = wf_json_validate(schema, strlen(schema), doc_wrong, strlen(doc_wrong), &err);
    WF_CHECK(s != WF_OK);
    WF_CHECK(err != NULL);
    free(err);

    /* array items wrong type */
    const char *arr_schema = "{\"type\":\"array\",\"items\":{\"type\":\"string\"}}";
    const char *arr_doc = "[1,2]";
    err = NULL;
    s = wf_json_validate(arr_schema, strlen(arr_schema), arr_doc, strlen(arr_doc), &err);
    WF_CHECK(s != WF_OK);
    WF_CHECK(err != NULL);
    free(err);

    /* array items correct type passes */
    const char *arr_doc_ok = "[\"a\",\"b\"]";
    err = NULL;
    s = wf_json_validate(arr_schema, strlen(arr_schema), arr_doc_ok, strlen(arr_doc_ok), &err);
    WF_CHECK(s == WF_OK);
    WF_CHECK(err == NULL);
}

int main(void) {
    test_canonicalize();
    test_validate_pass();
    test_validate_fail();
    WF_TEST_SUMMARY();
}
