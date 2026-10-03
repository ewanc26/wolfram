/*
 * test_post_view_typed.c — tests for the flattened post record/embed readers.
 */

#include "wolfram/post_view_typed.h"
#include "test.h"

#include <cJSON.h>

#include <string.h>

static void test_record(void) {
    cJSON *j = cJSON_Parse(
        "{\"text\":\"hi @a #t "
        "http://x\",\"createdAt\":\"2025-01-02T03:04:05Z\","
        "\"reply\":{\"root\":{\"uri\":\"at://r\",\"cid\":\"cr\"}},"
        "\"facets\":["
        "{\"index\":{\"byteStart\":3,\"byteEnd\":5},\"features\":["
        "{\"$type\":\"app.bsky.richtext.facet#bogus\"},"
        "{\"$type\":\"app.bsky.richtext.facet#mention\",\"did\":\"did:plc:a\"}]"
        "},"
        "{\"index\":{\"byteStart\":6,\"byteEnd\":8},\"features\":["
        "{\"$type\":\"app.bsky.richtext.facet#tag\",\"tag\":\"t\"}]},"
        "{\"features\":[{\"$type\":\"app.bsky.richtext.facet#link\"}]},"
        "{\"index\":{\"byteStart\":9,\"byteEnd\":19},\"features\":["
        "{\"$type\":\"app.bsky.richtext.facet#link\"}]}]}");
    wf_post_record rec;
    WF_CHECK(wf_post_record_from_json(j, &rec) == WF_OK);
    WF_CHECK(strcmp(rec.text, "hi @a #t http://x") == 0);
    WF_CHECK(strcmp(rec.created_at, "2025-01-02T03:04:05Z") == 0);
    WF_CHECK(rec.facet_count == 3);
    WF_CHECK(rec.facets[0].kind == WF_POST_FACET_MENTION);
    WF_CHECK(strcmp(rec.facets[0].target, "did:plc:a") == 0);
    WF_CHECK(rec.facets[0].byte_start == 3 && rec.facets[0].byte_end == 5);
    WF_CHECK(rec.facets[1].kind == WF_POST_FACET_TAG);
    WF_CHECK(rec.facets[2].kind == WF_POST_FACET_LINK);
    WF_CHECK(rec.facets[2].target[0] == '\0');
    WF_CHECK(strcmp(rec.reply_root.uri, "at://r") == 0);
    WF_CHECK(strcmp(rec.reply_root.cid, "cr") == 0);
    wf_post_record_free(&rec);
    cJSON_Delete(j);

    WF_CHECK(wf_post_record_from_json(NULL, &rec) == WF_OK);
    WF_CHECK(rec.text == NULL && rec.facet_count == 0 &&
             rec.reply_root.uri == NULL);
    wf_post_record_free(&rec);
    WF_CHECK(wf_post_record_from_json(NULL, NULL) == WF_ERR_INVALID_ARG);

    /* A root with only a uri is not a root. */
    j = cJSON_Parse("{\"root\":{\"uri\":\"at://r\"}}");
    wf_post_reply_root root;
    WF_CHECK(wf_post_reply_root_from_json(j, &root) == WF_OK);
    WF_CHECK(root.uri == NULL && root.cid == NULL);
    wf_post_reply_root_free(&root);
    cJSON_Delete(j);
}

static void test_reason(void) {
    cJSON *j =
        cJSON_Parse("{\"$type\":\"app.bsky.feed.defs#reasonRepost\",\"by\":"
                    "{\"handle\":\"bob.test\",\"displayName\":\"Bob\"}}");
    wf_post_reason r;
    WF_CHECK(wf_post_reason_from_json(j, &r) == WF_OK);
    WF_CHECK(r.is_repost && strcmp(r.by_display_name, "Bob") == 0);
    WF_CHECK(strcmp(r.by_handle, "bob.test") == 0);
    wf_post_reason_free(&r);
    cJSON_Delete(j);

    j = cJSON_Parse("{\"$type\":\"app.bsky.feed.defs#reasonPin\"}");
    WF_CHECK(wf_post_reason_from_json(j, &r) == WF_OK);
    WF_CHECK(!r.is_repost);
    wf_post_reason_free(&r);
    cJSON_Delete(j);
}

static void test_embed(void) {
    wf_post_embed e;
    cJSON *j =
        cJSON_Parse("{\"$type\":\"app.bsky.embed.images#view\",\"images\":["
                    "{\"thumb\":\"https://t/1\",\"alt\":\"a\",\"aspectRatio\":"
                    "{\"width\":4,\"height\":3}},{\"alt\":\"no thumb\"},"
                    "{\"thumb\":\"https://t/2\"}]}");
    WF_CHECK(wf_post_embed_from_json(j, &e) == WF_OK);
    WF_CHECK(e.image_count == 2);
    WF_CHECK(strcmp(e.images[0].thumb, "https://t/1") == 0);
    WF_CHECK(e.images[0].width == 4 && e.images[0].height == 3);
    WF_CHECK(e.images[1].alt == NULL && e.images[1].width == 0);
    WF_CHECK(!e.has_external && !e.has_quote);
    wf_post_embed_free(&e);
    cJSON_Delete(j);

    j = cJSON_Parse("{\"$type\":\"app.bsky.embed.external#view\",\"external\":"
                    "{\"uri\":\"https://x.test/"
                    "a\",\"title\":\"T\",\"description\":\"D\"}}");
    WF_CHECK(wf_post_embed_from_json(j, &e) == WF_OK);
    WF_CHECK(e.has_external && strcmp(e.external_title, "T") == 0);
    WF_CHECK(e.external_thumb == NULL);
    wf_post_embed_free(&e);
    cJSON_Delete(j);

    j = cJSON_Parse(
        "{\"$type\":\"app.bsky.embed.recordWithMedia#view\",\"record\":"
        "{\"record\":{\"$type\":\"app.bsky.embed.record#viewRecord\","
        "\"author\":{\"handle\":\"q.test\",\"displayName\":\"Q\"},"
        "\"value\":{\"text\":\"quoted\"}}},\"media\":"
        "{\"$type\":\"app.bsky.embed.images#view\",\"images\":"
        "[{\"thumb\":\"https://t/3\"}]}}");
    WF_CHECK(wf_post_embed_from_json(j, &e) == WF_OK);
    WF_CHECK(strstr(e.type, "recordWithMedia") != NULL);
    WF_CHECK(strstr(e.media_type, "images") != NULL);
    WF_CHECK(e.image_count == 1);
    WF_CHECK(e.has_quote && strcmp(e.quote_text, "quoted") == 0);
    WF_CHECK(strcmp(e.quote_author_handle, "q.test") == 0);
    wf_post_embed_free(&e);
    cJSON_Delete(j);

    /* A blocked quote is not drawable. */
    j = cJSON_Parse("{\"$type\":\"app.bsky.embed.record#view\",\"record\":"
                    "{\"$type\":\"app.bsky.embed.record#viewBlocked\"}}");
    WF_CHECK(wf_post_embed_from_json(j, &e) == WF_OK);
    WF_CHECK(!e.has_quote);
    wf_post_embed_free(&e);
    cJSON_Delete(j);

    WF_CHECK(wf_post_embed_from_json(NULL, &e) == WF_OK);
    WF_CHECK(e.type == NULL && e.image_count == 0);
    wf_post_embed_free(&e);
}

int main(void) {
    test_record();
    test_reason();
    test_embed();
    WF_TEST_SUMMARY();
}
