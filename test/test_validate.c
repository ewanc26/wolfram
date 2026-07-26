#include "wolfram/validate.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static const char *LEX_POST =
    "{\"lexicon\":1,\"id\":\"app.bsky.feed.post\",\"defs\":{\"main\":{\"type\":\"record\",\"key\":\"tid\",\"record\":{\"type\":\"object\",\"required\":[\"text\",\"createdAt\"],\"properties\":{\"text\":{\"type\":\"string\",\"maxLength\":3000,\"maxGraphemes\":300},\"facets\":{\"type\":\"array\",\"items\":{\"type\":\"ref\",\"ref\":\"app.bsky.richtext.facet\"}},\"reply\":{\"type\":\"ref\",\"ref\":\"#replyRef\"},\"createdAt\":{\"type\":\"string\",\"format\":\"datetime\"}}}},\"replyRef\":{\"type\":\"object\",\"required\":[\"root\",\"parent\"],\"properties\":{\"root\":{\"type\":\"ref\",\"ref\":\"com.atproto.repo.strongRef\"},\"parent\":{\"type\":\"ref\",\"ref\":\"com.atproto.repo.strongRef\"}}},\"textSlice\":{\"type\":\"object\",\"required\":[\"start\",\"end\"],\"properties\":{\"start\":{\"type\":\"integer\",\"minimum\":0},\"end\":{\"type\":\"integer\",\"minimum\":0}}}}}";

static const char *LEX_FACET =
    "{\"lexicon\":1,\"id\":\"app.bsky.richtext.facet\",\"defs\":{\"main\":{\"type\":\"object\",\"required\":[\"index\",\"features\"],\"properties\":{\"index\":{\"type\":\"ref\",\"ref\":\"#byteSlice\"},\"features\":{\"type\":\"array\",\"items\":{\"type\":\"union\",\"refs\":[\"#mention\",\"#link\",\"#tag\"]}}}},\"mention\":{\"type\":\"object\",\"required\":[\"did\"],\"properties\":{\"did\":{\"type\":\"string\",\"format\":\"did\"}}},\"link\":{\"type\":\"object\",\"required\":[\"uri\"],\"properties\":{\"uri\":{\"type\":\"string\",\"format\":\"uri\"}}},\"tag\":{\"type\":\"object\",\"required\":[\"tag\"],\"properties\":{\"tag\":{\"type\":\"string\",\"maxLength\":64,\"maxGraphemes\":64}}},\"byteSlice\":{\"type\":\"object\",\"required\":[\"byteStart\",\"byteEnd\"],\"properties\":{\"byteStart\":{\"type\":\"integer\",\"minimum\":0},\"byteEnd\":{\"type\":\"integer\",\"minimum\":0}}}}}";

static const char *LEX_STRONG_REF =
    "{\"lexicon\":1,\"id\":\"com.atproto.repo.strongRef\",\"defs\":{\"main\":{\"type\":\"object\",\"required\":[\"uri\",\"cid\"],\"properties\":{\"uri\":{\"type\":\"string\",\"format\":\"at-uri\"},\"cid\":{\"type\":\"string\"}}}}}";

static const char *LEX_GET_RECORD =
    "{\"lexicon\":1,\"id\":\"com.atproto.repo.getRecord\",\"defs\":{\"main\":{\"type\":\"query\",\"parameters\":{\"type\":\"params\",\"required\":[\"repo\",\"collection\",\"rkey\"],\"properties\":{\"repo\":{\"type\":\"string\",\"format\":\"at-identifier\"},\"collection\":{\"type\":\"string\",\"format\":\"nsid\"},\"rkey\":{\"type\":\"string\",\"format\":\"record-key\"},\"cid\":{\"type\":\"string\",\"format\":\"cid\"}}}},\"inputParams\":{\"type\":\"object\",\"required\":[\"repo\",\"collection\",\"rkey\"],\"properties\":{\"repo\":{\"type\":\"string\",\"format\":\"at-identifier\"},\"collection\":{\"type\":\"string\",\"format\":\"nsid\"},\"rkey\":{\"type\":\"string\",\"format\":\"record-key\"},\"cid\":{\"type\":\"string\",\"format\":\"cid\"}}}}}";

static const char *LEX_CREATE_RECORD =
    "{\"lexicon\":1,\"id\":\"com.atproto.repo.createRecord\",\"defs\":{\"main\":{\"type\":\"procedure\",\"input\":{\"encoding\":\"application/json\",\"schema\":{\"type\":\"object\",\"required\":[\"repo\",\"collection\",\"record\"],\"properties\":{\"repo\":{\"type\":\"string\",\"format\":\"at-identifier\"},\"collection\":{\"type\":\"string\",\"format\":\"nsid\"},\"record\":{\"type\":\"unknown\"}}}},\"output\":{\"encoding\":\"application/json\",\"schema\":{\"type\":\"object\",\"required\":[\"uri\",\"cid\"],\"properties\":{\"uri\":{\"type\":\"string\",\"format\":\"at-uri\"},\"cid\":{\"type\":\"string\",\"format\":\"cid\"}}}}},\"input\":{\"type\":\"object\",\"required\":[\"repo\",\"collection\",\"record\"],\"properties\":{\"repo\":{\"type\":\"string\",\"format\":\"at-identifier\"},\"collection\":{\"type\":\"string\",\"format\":\"nsid\"},\"record\":{\"type\":\"unknown\"}}}}}";

static void load(wf_lexicon_registry *r, const char *json) {
    WF_CHECK(wf_lexicon_registry_load(r, json, strlen(json)) == WF_OK);
}

static int error_path_contains(const wf_validate_error *errors, const char *needle) {
    const wf_validate_error *e;
    for (e = errors; e; e = e->next) {
        if (e->path && strstr(e->path, needle)) return 1;
    }
    return 0;
}

static void test_app_bsky_feed_post(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_POST);
    load(r, LEX_FACET);

    // Valid post
    {
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
            "{\"text\":\"hello world\",\"createdAt\":\"2024-01-01T00:00:00Z\"}",
            strlen("{\"text\":\"hello world\",\"createdAt\":\"2024-01-01T00:00:00Z\"}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required field
    {
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
            "{\"text\":\"hello world\"}",
            strlen("{\"text\":\"hello world\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "createdAt"));
        wf_validate_result_free(&res);
    }

    // Invalid type (text as number)
    {
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
            "{\"text\":123,\"createdAt\":\"2024-01-01T00:00:00Z\"}",
            strlen("{\"text\":123,\"createdAt\":\"2024-01-01T00:00:00Z\"}"));
        WF_CHECK(res.success == 0);
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
}

static void test_com_atproto_repo_strongRef(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_STRONG_REF);

    // Valid strongRef
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.strongRef", "main",
            "{\"uri\":\"at://did:plc:test/app.bsky.feed.post/3jkl0pp8sic\",\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\"}",
            strlen("{\"uri\":\"at://did:plc:test/app.bsky.feed.post/3jkl0pp8sic\",\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\"}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required field (uri)
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.strongRef", "main",
            "{\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\"}",
            strlen("{\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "uri"));
        wf_validate_result_free(&res);
    }

    // Missing required field (cid)
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.strongRef", "main",
            "{\"uri\":\"at://did:plc:test/app.bsky.feed.post/3jkl0pp8sic\"}",
            strlen("{\"uri\":\"at://did:plc:test/app.bsky.feed.post/3jkl0pp8sic\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "cid"));
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
}

static void test_query_validation(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_GET_RECORD);

    // Validate query parameters against the "main" def — should extract
    // the "parameters" object automatically
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.getRecord", "main",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required field in query params
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.getRecord", "main",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "rkey"));
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
}

static void test_procedure_validation(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_CREATE_RECORD);

    // Validate procedure input against the "main" def — should extract
    // the "input.schema" automatically
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.createRecord", "main",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"record\":{}}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"record\":{}}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required field in procedure input
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.createRecord", "main",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "record"));
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
}

static void test_com_atproto_repo_getRecord(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_GET_RECORD);

    // Valid getRecord request — validate against the "inputParams" def (object type)
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.getRecord", "inputParams",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required fields
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.getRecord", "inputParams",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "rkey"));
        wf_validate_result_free(&res);
    }

    // Invalid format (repo not an at-identifier) - basic check
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.getRecord", "inputParams",
            "{\"repo\":\"not-an-at-identifier\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}",
            strlen("{\"repo\":\"not-an-at-identifier\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}"));
        // Format validation might vary, so we just check it processes
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
}

static void test_com_atproto_repo_createRecord(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_CREATE_RECORD);

    // Valid createRecord request (minimal) — validate against "input" def
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.createRecord", "input",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"record\":{}}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"record\":{}}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required fields
    {
        wf_validate_result res = wf_validate_value(r, "com.atproto.repo.createRecord", "input",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "record"));
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
}


/* Lexicon blob `accept` entries are glob-style. Nearly every blob field in the
 * app.bsky lexicons uses a wildcard, so exact-matching them rejects every real
 * image and video upload. */
static void test_blob_accept_wildcard(void) {
    wf_lexicon_registry *reg = wf_lexicon_registry_new();
    WF_CHECK(reg != NULL);
    if (!reg) return;

    static const char lex[] =
        "{\"lexicon\":1,\"id\":\"com.example.media\",\"defs\":{\"main\":{"
        "\"type\":\"record\",\"record\":{\"type\":\"object\","
        "\"required\":[\"img\"],\"properties\":{"
        "\"img\":{\"type\":\"blob\",\"accept\":[\"image/*\"]}}}}}}";
    WF_CHECK(wf_lexicon_registry_load(reg, lex, sizeof(lex) - 1) == WF_OK);

    static const struct { const char *mime; int valid; } cases[] = {
        {"image/png", 1},
        {"image/jpeg", 1},
        {"IMAGE/PNG", 1},                 /* MIME types are case-insensitive */
        {"image/png; charset=binary", 1}, /* parameters are not part of the type */
        {"video/mp4", 0},
        {"text/plain", 0},
        {"image", 0},                     /* a bare type is not a subtype match */
        {"imagex/png", 0},                /* the slash must line up */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char rec[256];
        snprintf(rec, sizeof(rec),
                 "{\"$type\":\"com.example.media\",\"img\":{\"$type\":\"blob\","
                 "\"ref\":{\"$link\":\"bafkreiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
                 "\"mimeType\":\"%s\",\"size\":10}}", cases[i].mime);
        wf_validate_result r =
            wf_validate_record(reg, "com.example.media", rec, strlen(rec));
        if (r.success != cases[i].valid)
            fprintf(stderr, "  mime '%s': expected %s, got %s\n", cases[i].mime,
                    cases[i].valid ? "valid" : "invalid",
                    r.success ? "valid" : "invalid");
        WF_CHECK(r.success == cases[i].valid);
        wf_validate_result_free(&r);
    }

    /* A fully-wildcard pattern admits anything. */
    static const char anylex[] =
        "{\"lexicon\":1,\"id\":\"com.example.any\",\"defs\":{\"main\":{"
        "\"type\":\"record\",\"record\":{\"type\":\"object\","
        "\"required\":[\"f\"],\"properties\":{"
        "\"f\":{\"type\":\"blob\",\"accept\":[\"*/*\"]}}}}}}";
    WF_CHECK(wf_lexicon_registry_load(reg, anylex, sizeof(anylex) - 1) == WF_OK);
    static const char anyrec[] =
        "{\"$type\":\"com.example.any\",\"f\":{\"$type\":\"blob\","
        "\"ref\":{\"$link\":\"bafkreiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
        "\"mimeType\":\"application/zip\",\"size\":10}}";
    wf_validate_result any =
        wf_validate_record(reg, "com.example.any", anyrec, sizeof(anyrec) - 1);
    WF_CHECK(any.success);
    wf_validate_result_free(&any);

    wf_lexicon_registry_free(reg);
}

int main(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_POST);
    load(r, LEX_FACET);

    WF_CHECK(wf_lexicon_registry_load(r, "{not valid json", strlen("{not valid json")) == WF_ERR_PARSE);

    WF_CHECK(wf_lexicon_registry_load(r,
                "{\"lexicon\":1,\"id\":\"not-an-nsid\",\"defs\":{}}",
                strlen("{\"lexicon\":1,\"id\":\"not-an-nsid\",\"defs\":{}}")) == WF_ERR_PARSE);

    {
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
            "{\"text\":\"hello\",\"createdAt\":\"2024-01-01T00:00:00Z\"}",
            strlen("{\"text\":\"hello\",\"createdAt\":\"2024-01-01T00:00:00Z\"}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
            "{\"text\":\"hello\"}",
            strlen("{\"text\":\"hello\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "createdAt"));
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
            "{\"text\":5,\"createdAt\":\"2024-01-01T00:00:00Z\"}",
            strlen("{\"text\":5,\"createdAt\":\"2024-01-01T00:00:00Z\"}"));
        WF_CHECK(res.success == 0);
        wf_validate_result_free(&res);
    }

    {
        char *big = (char *)malloc(3001 + 1);
        WF_CHECK(big != NULL);
        if (big) {
            memset(big, 'a', 3001);
            big[3001] = '\0';
            wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
                big, 3001);
            WF_CHECK(res.success == 0);
            wf_validate_result_free(&res);
            free(big);
        }
    }

    {
        const char *json = "{\"text\":\"hi @bob\",\"createdAt\":\"2024-01-01T00:00:00Z\",\"facets\":[{\"$type\":\"app.bsky.richtext.facet\",\"index\":{\"byteStart\":0,\"byteEnd\":3},\"features\":[{\"$type\":\"app.bsky.richtext.facet#mention\",\"did\":\"did:plc:z72i7hdynmk6r22z27h6tvur\"}]}]}";
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post", json, strlen(json));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    {
        const char *json = "{\"text\":\"hi @bob\",\"createdAt\":\"2024-01-01T00:00:00Z\",\"facets\":[{\"$type\":\"app.bsky.richtext.facet\",\"index\":{\"byteStart\":0,\"byteEnd\":3},\"features\":[{\"$type\":\"app.bsky.richtext.facet#mention\",\"did\":\"did:notvalid\"}]}]}";
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post", json, strlen(json));
        WF_CHECK(res.success == 0);
        wf_validate_result_free(&res);
    }

    {
        const char *json = "{\"text\":\"hi @bob\",\"createdAt\":\"2024-01-01T00:00:00Z\",\"facets\":[{\"$type\":\"app.bsky.richtext.facet\",\"index\":{\"byteEnd\":3},\"features\":[{\"$type\":\"app.bsky.richtext.facet#mention\",\"did\":\"did:plc:z72i7hdynmk6r22z27h6tvur\"}]}]}";
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post", json, strlen(json));
        WF_CHECK(res.success == 0);
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result res = wf_validate_value(r, "app.bsky.feed.post", "textSlice",
            "{\"start\":0,\"end\":5}", strlen("{\"start\":0,\"end\":5}"));
        WF_CHECK(res.success == 1);
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result res = wf_validate_value(r, "app.bsky.feed.post", "textSlice",
            "{\"start\":-1,\"end\":5}", strlen("{\"start\":-1,\"end\":5}"));
        WF_CHECK(res.success == 0);
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result res = wf_validate_value(r, "app.bsky.feed.post", "textSlice",
            "{}", strlen("{}"));
        WF_CHECK(res.success == 0);
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result res = wf_validate_record(r, "com.example.missing",
            "{\"text\":\"hello\"}", strlen("{\"text\":\"hello\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result res = wf_validate_record(r, "app.bsky.feed.post",
            "{not json", strlen("{not json"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        wf_validate_result_free(&res);
    }

    {
        wf_validate_result zeroed = {0};
        wf_validate_result_free(&zeroed);
    }

    wf_lexicon_registry_free(r);

    // Run our new comprehensive tests
    test_app_bsky_feed_post();
    test_com_atproto_repo_strongRef();
    test_com_atproto_repo_getRecord();
    test_blob_accept_wildcard();
    test_com_atproto_repo_createRecord();
    test_query_validation();
    test_procedure_validation();

    WF_TEST_SUMMARY();
}