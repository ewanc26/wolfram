/*
 * test_repo_typed.c — offline tests for the com.atproto.repo typed parsers
 * and the applyWrites input builder. Hardcodes representative response bodies
 * and asserts the owned structs are populated correctly, then freed. Agent
 * wrappers require live auth and are exercised only for NULL-argument
 * validation.
 */

#include "wolfram/repo_typed.h"
#include "wolfram/atproto_lex.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* com.atproto.repo.getRecord response (uri, cid, value). */
static const char *kGetRecordJson =
    "{"
    "  \"uri\": \"at://did:plc:alice/app.bsky.feed.post/abc123\","
    "  \"cid\": \"bafyreigabc123\","
    "  \"value\": {"
    "    \"$type\": \"app.bsky.feed.post\","
    "    \"text\": \"hello world\","
    "    \"createdAt\": \"2026-07-01T10:00:00.000Z\""
    "  }"
    "}";

/* com.atproto.repo.listRecords response (records[] + cursor). */
static const char *kListRecordsJson =
    "{"
    "  \"cursor\": \"next-cursor\","
    "  \"records\": ["
    "    {"
    "      \"uri\": \"at://did:plc:alice/app.bsky.feed.post/aaa\","
    "      \"cid\": \"bafyreigaaa\","
    "      \"value\": {\"$type\": \"app.bsky.feed.post\", \"text\": \"one\"}"
    "    },"
    "    {"
    "      \"uri\": \"at://did:plc:alice/app.bsky.feed.post/bbb\","
    "      \"value\": {\"$type\": \"app.bsky.feed.post\", \"text\": \"two\"}"
    "    }"
    "  ]"
    "}";

/* com.atproto.repo.describeRepo response (handle, did, didDoc, collections,
 * handleIsCorrect). */
static const char *kDescribeRepoJson =
    "{"
    "  \"handle\": \"alice.bsky.social\","
    "  \"did\": \"did:plc:alice\","
    "  \"didDoc\": {\"@context\": [\"https://www.w3.org/ns/did/v1\"], \"id\": \"did:plc:alice\"},"
    "  \"collections\": [\"app.bsky.feed.post\", \"app.bsky.actor.profile\"],"
    "  \"handleIsCorrect\": true"
    "}";

/* com.atproto.repo.listMissingBlobs response (blobs[] + cursor). */
static const char *kListMissingBlobsJson =
    "{"
    "  \"cursor\": \"blob-cursor\","
    "  \"blobs\": ["
    "    {\"cid\": \"bafyreiblob1\", \"recordUri\": \"at://did:plc:alice/app.bsky.feed.post/x\"},"
    "    {\"cid\": \"bafyreiblob2\", \"recordUri\": \"at://did:plc:alice/app.bsky.feed.post/y\"}"
    "  ]"
    "}";

/* com.atproto.repo.applyWrites response (commit + results union array). */
static const char *kApplyWritesJson =
    "{"
    "  \"commit\": {\"cid\": \"bafyreicommit\", \"rev\": \"rev-42\"},"
    "  \"results\": ["
    "    {\"$type\": \"com.atproto.repo.applyWrites#createResult\","
    "     \"uri\": \"at://did:plc:alice/app.bsky.feed.post/z\", \"cid\": \"bafyreiout\"},"
    "    {\"$type\": \"com.atproto.repo.applyWrites#deleteResult\"}"
    "  ]"
    "}";

int main(void) {
    /* ---- getRecord ---- */
    {
        wf_repo_record r = {0};
        WF_CHECK(wf_repo_parse_get_record(kGetRecordJson, strlen(kGetRecordJson),
                                          &r) == WF_OK);
        WF_CHECK(r.uri && strcmp(r.uri,
                      "at://did:plc:alice/app.bsky.feed.post/abc123") == 0);
        WF_CHECK(r.has_cid && r.cid && strcmp(r.cid, "bafyreigabc123") == 0);
        WF_CHECK(r.value && cJSON_IsObject(r.value));
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(r.value, "text") &&
                 strcmp(cJSON_GetObjectItemCaseSensitive(r.value, "text")
                            ->valuestring,
                        "hello world") == 0);
        wf_repo_record_free(&r);
        WF_CHECK(r.uri == NULL && r.cid == NULL && r.value == NULL);
    }

    /* ---- listRecords ---- */
    {
        wf_repo_record_list l = {0};
        WF_CHECK(wf_repo_parse_list_records(kListRecordsJson,
                                            strlen(kListRecordsJson), &l) ==
                 WF_OK);
        WF_CHECK(l.count == 2);
        WF_CHECK(l.cursor && strcmp(l.cursor, "next-cursor") == 0);
        WF_CHECK(l.items[0].uri &&
                 strcmp(l.items[0].uri,
                        "at://did:plc:alice/app.bsky.feed.post/aaa") == 0);
        WF_CHECK(l.items[0].has_cid &&
                 strcmp(l.items[0].cid, "bafyreigaaa") == 0);
        WF_CHECK(!l.items[1].has_cid && l.items[1].cid == NULL);
        WF_CHECK(l.items[1].value && cJSON_IsObject(l.items[1].value));
        wf_repo_record_list_free(&l);
        WF_CHECK(l.items == NULL && l.count == 0 && l.cursor == NULL);
    }

    /* ---- describeRepo ---- */
    {
        wf_repo_description d = {0};
        WF_CHECK(wf_repo_parse_describe_repo(kDescribeRepoJson,
                                             strlen(kDescribeRepoJson), &d) ==
                 WF_OK);
        WF_CHECK(d.handle && strcmp(d.handle, "alice.bsky.social") == 0);
        WF_CHECK(d.did && strcmp(d.did, "did:plc:alice") == 0);
        WF_CHECK(d.did_doc && cJSON_IsObject(d.did_doc));
        WF_CHECK(d.collection_count == 2);
        WF_CHECK(d.collections[0] &&
                 strcmp(d.collections[0], "app.bsky.feed.post") == 0);
        WF_CHECK(d.collections[1] &&
                 strcmp(d.collections[1], "app.bsky.actor.profile") == 0);
        WF_CHECK(d.has_handle_is_correct && d.handle_is_correct);
        wf_repo_description_free(&d);
        WF_CHECK(d.handle == NULL && d.did == NULL &&
                 d.did_doc == NULL && d.collections == NULL);
    }

    /* ---- listMissingBlobs ---- */
    {
        wf_repo_missing_blob_list l = {0};
        WF_CHECK(wf_repo_parse_list_missing_blobs(kListMissingBlobsJson,
                                                 strlen(kListMissingBlobsJson),
                                                 &l) == WF_OK);
        WF_CHECK(l.count == 2);
        WF_CHECK(l.cursor && strcmp(l.cursor, "blob-cursor") == 0);
        WF_CHECK(l.items[0].cid &&
                 strcmp(l.items[0].cid, "bafyreiblob1") == 0);
        WF_CHECK(l.items[0].record_uri &&
                 strcmp(l.items[0].record_uri,
                        "at://did:plc:alice/app.bsky.feed.post/x") == 0);
        WF_CHECK(l.items[1].cid &&
                 strcmp(l.items[1].cid, "bafyreiblob2") == 0);
        wf_repo_missing_blob_list_free(&l);
        WF_CHECK(l.items == NULL && l.count == 0 && l.cursor == NULL);
    }

    /* ---- applyWrites result ---- */
    {
        wf_repo_apply_writes_result r = {0};
        WF_CHECK(wf_repo_parse_apply_writes(kApplyWritesJson,
                                            strlen(kApplyWritesJson), &r) ==
                 WF_OK);
        WF_CHECK(r.has_commit_cid &&
                 r.commit_cid && strcmp(r.commit_cid, "bafyreicommit") == 0);
        WF_CHECK(r.has_commit_rev &&
                 r.commit_rev && strcmp(r.commit_rev, "rev-42") == 0);
        WF_CHECK(r.commit && cJSON_IsObject(r.commit));
        WF_CHECK(r.results && cJSON_IsArray(r.results) &&
                 cJSON_GetArraySize(r.results) == 2);
        wf_repo_apply_writes_result_free(&r);
        WF_CHECK(r.commit == NULL && r.commit_cid == NULL &&
                 r.commit_rev == NULL && r.results == NULL);
    }

    /* ---- malformed input yields WF_ERR_PARSE ---- */
    {
        wf_repo_record r = {0};
        WF_CHECK(wf_repo_parse_get_record("not json", 8, &r) == WF_ERR_PARSE);
        wf_repo_record_list l = {0};
        WF_CHECK(wf_repo_parse_list_records("{\"records\": 5}", 14, &l) ==
                 WF_ERR_PARSE);
    }

    /* ---- applyWrites builder round-trip ---- */
    {
        wf_repo_writes_builder *b = NULL;
        WF_CHECK(wf_repo_writes_builder_init(&b) == WF_OK && b != NULL);

        cJSON *v1 = cJSON_Parse("{\"$type\":\"app.bsky.feed.post\",\"text\":\"hi\"}");
        cJSON *v2 = cJSON_Parse("{\"$type\":\"app.bsky.feed.post\",\"text\":\"upd\"}");
        WF_CHECK(wf_repo_writes_add_create(b, "app.bsky.feed.post", NULL, v1) ==
                 WF_OK);
        WF_CHECK(wf_repo_writes_add_update(b, "app.bsky.feed.post", "rk2",
                                           v2) == WF_OK);
        WF_CHECK(wf_repo_writes_add_delete(b, "app.bsky.feed.post", "rk3") ==
                 WF_OK);

        char *json = NULL;
        WF_CHECK(wf_repo_writes_build_json(b, "did:plc:alice", 1, NULL,
                                           &json) == WF_OK);
        WF_CHECK(json != NULL);

        cJSON *root = cJSON_Parse(json);
        WF_CHECK(root && cJSON_IsObject(root));
        cJSON *repo = cJSON_GetObjectItemCaseSensitive(root, "repo");
        cJSON *validate = cJSON_GetObjectItemCaseSensitive(root, "validate");
        cJSON *writes = cJSON_GetObjectItemCaseSensitive(root, "writes");
        WF_CHECK(repo && strcmp(repo->valuestring, "did:plc:alice") == 0);
        WF_CHECK(validate && cJSON_IsTrue(validate));
        WF_CHECK(writes && cJSON_IsArray(writes) &&
                 cJSON_GetArraySize(writes) == 3);
        cJSON *c0 = cJSON_GetArrayItem(writes, 0);
        cJSON *c1 = cJSON_GetArrayItem(writes, 1);
        cJSON *c2 = cJSON_GetArrayItem(writes, 2);
        WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(c0, "$type")
                            ->valuestring,
                        "com.atproto.repo.applyWrites#create") == 0);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(c0, "rkey") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(c0, "value") &&
                 cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(c0, "value")));
        WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(c1, "$type")
                            ->valuestring,
                        "com.atproto.repo.applyWrites#update") == 0);
        WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(c1, "rkey")
                            ->valuestring,
                        "rk2") == 0);
        WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(c2, "$type")
                            ->valuestring,
                        "com.atproto.repo.applyWrites#delete") == 0);
        WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(c2, "rkey")
                            ->valuestring,
                        "rk3") == 0);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(c2, "value") == NULL);

        free(json);
        cJSON_Delete(root);
        wf_repo_writes_builder_free(b);
    }

    /* ---- builder with swapCommit + validate unset ---- */
    {
        wf_repo_writes_builder *b = NULL;
        WF_CHECK(wf_repo_writes_builder_init(&b) == WF_OK);
        WF_CHECK(wf_repo_writes_add_delete(b, "app.bsky.feed.post", "rk") ==
                 WF_OK);
        char *json = NULL;
        WF_CHECK(wf_repo_writes_build_json(b, "did:plc:bob", -1, "bafyrswap",
                                           &json) == WF_OK);
        cJSON *root = cJSON_Parse(json);
        WF_CHECK(root && cJSON_GetObjectItemCaseSensitive(root, "validate") ==
                 NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "swapCommit") &&
                 strcmp(cJSON_GetObjectItemCaseSensitive(root, "swapCommit")
                            ->valuestring,
                        "bafyrswap") == 0);
        free(json);
        cJSON_Delete(root);
        wf_repo_writes_builder_free(b);
    }

    /* ---- Agent wrapper NULL validation (NULL agent passed so the wrappers
       never dereference a fake pointer). ---- */
    {
        wf_repo_record rec = {0};
        wf_repo_record_list rl = {0};
        wf_repo_description desc = {0};
        wf_repo_missing_blob_list bl = {0};
        wf_repo_apply_writes_result aw = {0};

        WF_CHECK(wf_agent_get_record_typed(NULL, "r", "c", "k", NULL, &rec) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_record_typed(NULL, NULL, "c", "k", NULL, &rec) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_record_typed(NULL, "r", "c", "k", NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_list_records_typed(NULL, "r", "c", 10, NULL, 0,
                                             &rl) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_records_typed(NULL, NULL, "c", 10, NULL, 0,
                                             &rl) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_records_typed(NULL, "r", "c", 10, NULL, 0,
                                             NULL) == WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_describe_repo_typed(NULL, "r", &desc) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_describe_repo_typed(NULL, NULL, &desc) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_describe_repo_typed(NULL, "r", NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_list_missing_blobs_typed(NULL, 10, NULL, &bl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_missing_blobs_typed(NULL, 10, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_apply_writes_typed(NULL, "{}", &aw) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_apply_writes_typed(NULL, NULL, &aw) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_apply_writes_typed(NULL, "{}", NULL) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- Generated applyWrites union array encoding. Multiple entries are
       important: the union element is larger than wf_lex_json, so allocating
       this array as generic JSON would make the encoder stride out of bounds
       after the first write. ---- */
    {
        static const char create[] =
            "{\"$type\":\"com.atproto.repo.applyWrites#create\","
            "\"collection\":\"app.bsky.feed.post\",\"value\":{\"text\":\"one\"}}";
        static const char delete_[] =
            "{\"$type\":\"com.atproto.repo.applyWrites#delete\","
            "\"collection\":\"app.bsky.feed.post\",\"rkey\":\"two\"}";
        wf_lex_com_atproto_repo_apply_writes_main_input_writes_item_union
            writes[2] = {0};
        writes[0].kind = -1;
        writes[0].data = create;
        writes[0].length = strlen(create);
        writes[1].kind = -1;
        writes[1].data = delete_;
        writes[1].length = strlen(delete_);

        wf_lex_com_atproto_repo_apply_writes_main_input input = {0};
        input.repo = "did:plc:alice";
        input.writes.items = writes;
        input.writes.count = 2;

        char *json = NULL;
        WF_CHECK(wf_lex_com_atproto_repo_apply_writes_main_input_encode_json(
                     &input, &json) == WF_OK);
        cJSON *root = cJSON_Parse(json);
        cJSON *encoded = cJSON_GetObjectItemCaseSensitive(root, "writes");
        WF_CHECK(cJSON_IsArray(encoded));
        WF_CHECK(cJSON_GetArraySize(encoded) == 2);
        WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(
                            cJSON_GetArrayItem(encoded, 1), "$type")->valuestring,
                        "com.atproto.repo.applyWrites#delete") == 0);
        cJSON_Delete(root);
        wf_lex_com_atproto_repo_apply_writes_main_json_free(json);
    }

    /* ---- createRecord / putRecord result parser ---- */
    {
        static const char *json =
            "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/abc123\","
            "\"cid\":\"bafyreig\",\"commit\":{\"cid\":\"bafy:rev\","
            "\"rev\":\"3\"},\"validationStatus\":\"valid\"}";
        wf_repo_write_record_result out = {0};
        wf_status s = wf_repo_parse_write_record_result(
            json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.uri &&
                 strcmp(out.uri,
                        "at://did:plc:abc/app.bsky.feed.post/abc123") == 0);
        WF_CHECK(out.cid && strcmp(out.cid, "bafyreig") == 0);
        WF_CHECK(out.extra != NULL);
        if (out.extra) {
            cJSON *commit =
                cJSON_GetObjectItemCaseSensitive(out.extra, "commit");
            WF_CHECK(cJSON_IsObject(commit));
        }
        wf_repo_write_record_result_free(&out);
        wf_repo_write_record_result_free(&out); /* idempotent */

        WF_CHECK(wf_repo_parse_write_record_result(NULL, 0, &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_repo_parse_write_record_result(NULL, 0, NULL) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- uploadBlob result parser ---- */
    {
        static const char *json =
            "{\"blob\":{\"cid\":\"bafy:blob\",\"mimeType\":\"image/png\","
            "\"size\":1234}}";
        wf_repo_upload_blob_result out = {0};
        wf_status s = wf_repo_parse_upload_blob_result(
            json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.cid && strcmp(out.cid, "bafy:blob") == 0);
        WF_CHECK(out.mime_type &&
                 strcmp(out.mime_type, "image/png") == 0);
        WF_CHECK(out.has_size && out.size == 1234);
        wf_repo_upload_blob_result_free(&out);

        static const char *bad = "{\"blob\":{\"mimeType\":\"x\"}}";
        wf_repo_upload_blob_result bad_out = {0};
        WF_CHECK(wf_repo_parse_upload_blob_result(
                     bad, strlen(bad), &bad_out) == WF_OK);
        WF_CHECK(bad_out.cid == NULL && bad_out.has_size == false);
        wf_repo_upload_blob_result_free(&bad_out);

        WF_CHECK(wf_repo_parse_upload_blob_result(NULL, 0, &out) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- New agent wrapper NULL validation (NULL agent passed so the
       wrappers never dereference a fake pointer; the wrappers validate the
       agent first, so NULL agent is enough to exercise the invalid path). ---- */
    {
        wf_repo_write_record_result wr = {0};
        wf_repo_upload_blob_result ub = {0};

        WF_CHECK(wf_agent_create_record_typed(NULL, "r", "c", NULL, -1,
                                             "{}", NULL, &wr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_record_typed(NULL, "r", "c", NULL, -1, "{}",
                                             NULL, NULL) == WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_put_record_typed(NULL, "r", "c", "k", -1, "{}", NULL,
                                           NULL, &wr) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_put_record_typed(NULL, "r", "c", "k", -1, "{}", NULL,
                                           NULL, NULL) == WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_delete_record_typed(NULL, "r", "c", "k", NULL,
                                              NULL) == WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_upload_blob_typed(NULL, "x", 1, "image/png", &ub) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_upload_blob_typed(NULL, "x", 1, "image/png", NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_import_repo_typed(NULL, "x", 1) ==
                 WF_ERR_INVALID_ARG);
    }

    printf("repo_typed: all checks passed\n");
    return 0;
}
