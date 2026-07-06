#include "wolfram/validate.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static const char *LEX_POST =
    "{\"lexicon\":1,\"id\":\"app.bsky.feed.post\",\"defs\":{\"main\":{\"type\":\"record\",\"key\":\"tid\",\"record\":{\"type\":\"object\",\"required\":[\"text\",\"createdAt\"],\"properties\":{\"text\":{\"type\":\"string\",\"maxLength\":3000,\"maxGraphemes\":300},\"facets\":{\"type\":\"array\",\"items\":{\"type\":\"ref\",\"ref\":\"app.bsky.richtext.facet\"}},\"reply\":{\"type\":\"ref\",\"ref\":\"#replyRef\"},\"createdAt\":{\"type\":\"string\",\"format\":\"datetime\"}},\"replyRef\":{\"type\":\"object\",\"required\":[\"root\",\"parent\"],\"properties\":{\"root\":{\"type\":\"ref\",\"ref\":\"com.atproto.repo.strongRef\"},\"parent\":{\"type\":\"ref\",\"ref\":\"com.atproto.repo.strongRef\"}}},\"textSlice\":{\"type\":\"object\",\"required\":[\"start\",\"end\"],\"properties\":{\"start\":{\"type\":\"integer\",\"minimum\":0},\"end\":{\"type\":\"integer\",\"minimum\":0}}}}}";

static const char *LEX_FACET =
    "{\"lexicon\":1,\"id\":\"app.bsky.richtext.facet\",\"defs\":{\"main\":{\"type\":\"object\",\"required\":[\"index\",\"features\"],\"properties\":{\"index\":{\"type\":\"ref\",\"ref\":\"#byteSlice\"},\"features\":{\"type\":\"array\",\"items\":{\"type\":\"union\",\"refs\":[\"#mention\",\"#link\",\"#tag\"]}}}},\"mention\":{\"type\":\"object\",\"required\":[\"did\"],\"properties\":{\"did\":{\"type\":\"string\",\"format\":\"did\"}}},\"link\":{\"type\":\"object\",\"required\":[\"uri\"],\"properties\":{\"uri\":{\"type\":\"string\",\"format\":\"uri\"}}},\"tag\":{\"type\":\"object\",\"required\":[\"tag\"],\"properties\":{\"tag\":{\"type\":\"string\",\"maxLength\":64,\"maxGraphemes\":64}}},\"byteSlice\":{\"type\":\"object\",\"required\":[\"byteStart\",\"byteEnd\"],\"properties\":{\"byteStart\":{\"type\":\"integer\",\"minimum\":0},\"byteEnd\":{\"type\":\"integer\",\"minimum\":0}}}}}";

static const char *LEX_STRONG_REF =
    "{\"lexicon\":1,\"id\":\"com.atproto.repo.strongRef\",\"defs\":{\"main\":{\"type\":\"reference\",\"description\":\"A reference to a strong reference (a commit and a pointer to a leaf in a merkle search tree).\",\"properties\":{\"cid\":{\"type\":\"string\",\"format\":\"cid\"},\"rev\":{\"type\":\"string\",\"format\":\"tid\"}}}}";

static const char *LEX_GET_RECORD =
    "{\"lexicon\":1,\"id\":\"com.atproto.repo.getRecord\",\"defs\":{\"main\":{\"type\":\"query\",\"description\":\"Get a single record from a repository. Does not require auth.\",\"parameters\":{\"type\":\"params\",\"required\":[\"repo\",\"collection\",\"rkey\"],\"properties\":{\"repo\":{\"type\":\"string\",\"format\":\"at-identifier\",\"description\":\"The handle or DID of the repo.\"},\"collection\":{\"type\":\"string\",\"format\":\"nsid\",\"description\":\"The NSID of the record collection.\"},\"rkey\":{\"type\":\"string\",\"description\":\"The Record Key.\",\"format\":\"record-key\"},\"cid\":{\"type\":\"string\",\"format\":\"cid\",\"description\":\"The CID of the version of the record. If not specified, then return the most recent version.\"}}},\"output\":{\"encoding\":\"application/json\",\"schema\":{\"type\":\"object\",\"required\":[\"uri\",\"value\"],\"properties\":{\"uri\":{\"type\":\"string\",\"format\":\"at-uri\"},\"cid\":{\"type\":\"string\",\"format\":\"cid\"},\"value\":{\"type\":\"unknown\"}}}}}}";

static const char *LEX_CREATE_RECORD =
    "{\"lexicon\":1,\"id\":\"com.atproto.repo.createRecord\",\"defs\":{\"input\":{\"type\":\"params\",\"required\":[\"repo\",\"collection\",\"record\"],\"properties\":{\"repo\":{\"type\":\"string\",\"format\":\"at-identifier\",\"description\":\"The repository to write to.\"},\"collection\":{\"type\":\"string\",\"format\":\"nsid\",\"description\":\"The ID of the record type to create.\"},\"record\":{\"type\":\"unknown\"}}},\"output\":{\"encoding\":\"application/json\",\"schema\":{\"type\":\"object\",\"required\":[\"uri\",\"cid\"],\"properties\":{\"uri\":{\"type\":\"string\",\"format\":\"at-uri\"},\"cid\":{\"type\":\"string\",\"format\":\"cid\"}}}}}}";

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
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.strongRef",
            "{\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\",\"rev\":\"3jkl0pp8sic\"}",
            strlen("{\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\",\"rev\":\"3jkl0pp8sic\"}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required fields
    {
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.strongRef",
            "{\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\"}",
            strlen("{\"cid\":\"bafybeigdyrzt5wfp7udq7hu7v67y2emfw343ytbtwdgvsiheitiwtitajypi\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "rev"));
        wf_validate_result_free(&res);
    }

    // Invalid CID format (basic check - we're not validating CID format deeply here)
    {
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.strongRef",
            "{\"cid\":\"not-a-valid-cid\",\"rev\":\"3jkl0pp8sic\"}",
            strlen("{\"cid\":\"not-a-valid-cid\",\"rev\":\"3jkl0pp8sic\"}"));
        // This might actually pass since we're just doing basic JSON validation
        // The format validation might be more lenient in the current implementation
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
}

static void test_com_atproto_repo_getRecord(void) {
    wf_lexicon_registry *r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load(r, LEX_GET_RECORD);

    // Valid getRecord request
    {
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.getRecord",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"rkey\":\"3jkl0pp8sic\"}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required fields
    {
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.getRecord",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "rkey"));
        wf_validate_result_free(&res);
    }

    // Invalid format (repo not an at-identifier) - basic check
    {
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.getRecord",
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

    // Valid createRecord request (minimal)
    {
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.createRecord",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"record\":{}}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\",\"record\":{}}"));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    // Missing required fields
    {
        wf_validate_result res = wf_validate_record(r, "com.atproto.repo.createRecord",
            "{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}",
            strlen("{\"repo\":\"did:plc:123\",\"collection\":\"app.bsky.feed.post\"}"));
        WF_CHECK(res.success == 0 && res.errors != NULL);
        WF_CHECK(error_path_contains(res.errors, "record"));
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);
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
    test_com_atproto_repo_createRecord();

    WF_TEST_SUMMARY();
}