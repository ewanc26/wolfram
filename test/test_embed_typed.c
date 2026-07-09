/*
 * test_embed_typed.c — offline tests for the getEmbedExternalView typed parser
 * and agent wrapper. Builds representative response bodies with cJSON, asserts
 * the owned struct is populated correctly, then frees it. The agent wrapper
 * requires live auth and is exercised only for argument validation.
 */

#include "wolfram/embed_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *json_of(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

int main(void) {
    /* ---- Invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_embed_external_view v = {0};
    WF_CHECK(wf_embed_parse_external_view(NULL, 0, &v) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_embed_parse_external_view("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    /* ---- Malformed JSON -> WF_ERR_PARSE ---- */
    WF_CHECK(wf_embed_parse_external_view("not json", 8, &v) == WF_ERR_PARSE);
    WF_CHECK(wf_embed_parse_external_view("[]", 2, &v) == WF_ERR_PARSE);

    /* ---- Empty ({}) response: no view resolved ---- */
    WF_CHECK(wf_embed_parse_external_view("{}", 2, &v) == WF_OK);
    WF_CHECK(v.has_view == false);
    WF_CHECK(v.uri == NULL && v.view == NULL);
    WF_CHECK(v.associated_refs == NULL && v.associated_records == NULL);
    wf_embed_external_view_free(&v);

    /* ---- Full hydrated view round-trip ---- */
    cJSON *root = cJSON_CreateObject();

    cJSON *view = cJSON_CreateObject();
    cJSON *external = cJSON_CreateObject();
    cJSON_AddStringToObject(external, "uri", "https://example.com/article");
    cJSON_AddStringToObject(external, "title", "An Example Article");
    cJSON_AddStringToObject(external, "description", "A short description.");
    cJSON_AddStringToObject(external, "thumb",
                            "https://cdn.bsky.app/img/thumb.jpg");
    /* An open/unbounded nested field that should be preserved inside `view`. */
    cJSON *source = cJSON_CreateObject();
    cJSON_AddStringToObject(source, "title", "Example Publication");
    cJSON_AddItemToObject(external, "source", source);
    cJSON_AddItemToObject(view, "external", external);
    cJSON_AddItemToObject(root, "view", view);

    cJSON *refs = cJSON_CreateArray();
    cJSON *ref = cJSON_CreateObject();
    cJSON_AddStringToObject(ref, "uri",
                            "at://did:plc:x/site.standard.document/1");
    cJSON_AddStringToObject(ref, "cid", "bafyref1111111111111111111111111111");
    cJSON_AddItemToArray(refs, ref);
    cJSON_AddItemToObject(root, "associatedRefs", refs);

    cJSON *records = cJSON_CreateArray();
    cJSON *rec = cJSON_CreateObject();
    cJSON_AddStringToObject(rec, "$type", "site.standard.document");
    cJSON_AddItemToArray(records, rec);
    cJSON_AddItemToObject(root, "associatedRecords", records);

    char *body = json_of(root);

    WF_CHECK(wf_embed_parse_external_view(body, strlen(body), &v) == WF_OK);
    WF_CHECK(v.has_view == true);
    WF_CHECK(v.uri && strcmp(v.uri, "https://example.com/article") == 0);
    WF_CHECK(v.title && strcmp(v.title, "An Example Article") == 0);
    WF_CHECK(v.description && strcmp(v.description, "A short description.") == 0);
    WF_CHECK(v.thumb &&
             strcmp(v.thumb, "https://cdn.bsky.app/img/thumb.jpg") == 0);
    /* The full "view" subtree is retained, including nested open fields. */
    WF_CHECK(v.view != NULL);
    cJSON *v_external = cJSON_GetObjectItemCaseSensitive(v.view, "external");
    WF_CHECK(cJSON_IsObject(v_external));
    cJSON *v_source = cJSON_GetObjectItemCaseSensitive(v_external, "source");
    WF_CHECK(cJSON_IsObject(v_source));
    /* Detached arrays are preserved. */
    WF_CHECK(cJSON_IsArray(v.associated_refs) &&
             cJSON_GetArraySize(v.associated_refs) == 1);
    WF_CHECK(cJSON_IsArray(v.associated_records) &&
             cJSON_GetArraySize(v.associated_records) == 1);

    wf_embed_external_view_free(&v);
    /* Free resets the struct (idempotent). */
    WF_CHECK(v.uri == NULL && v.view == NULL && v.has_view == false);
    wf_embed_external_view_free(&v); /* second free on a reset struct is safe */
    free(body);

    /* ---- View present but without an "external" object ---- */
    cJSON *root2 = cJSON_CreateObject();
    cJSON *view2 = cJSON_CreateObject();
    cJSON_AddItemToObject(root2, "view", view2);
    char *body2 = json_of(root2);
    WF_CHECK(wf_embed_parse_external_view(body2, strlen(body2), &v) == WF_OK);
    WF_CHECK(v.has_view == true);
    WF_CHECK(v.uri == NULL && v.title == NULL);
    WF_CHECK(v.view != NULL);
    wf_embed_external_view_free(&v);
    free(body2);

    /* ---- Agent wrapper argument validation ---- */
    const char *uris[] = {"at://did:plc:x/site.standard.document/1"};
    const char *empty_uris[] = {""};
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", uris, 1, &v) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, NULL, uris, 1, &v) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", NULL, 0, &v) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", empty_uris, 1, &v) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", uris, 1, NULL) ==
             WF_ERR_INVALID_ARG);

    /* ---- Inline embed view parsers ---- */
    const char *EOL = "";
    (void)EOL;

    /* images#view */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *images = cJSON_CreateArray();
        cJSON *img = cJSON_CreateObject();
        cJSON_AddStringToObject(img, "thumb", "https://cdn/thumb/1");
        cJSON_AddStringToObject(img, "fullsize", "https://cdn/full/1");
        cJSON_AddStringToObject(img, "alt", "a cat");
        cJSON *ar = cJSON_CreateObject();
        cJSON_AddNumberToObject(ar, "width", 4);
        cJSON_AddNumberToObject(ar, "height", 3);
        cJSON_AddItemToObject(img, "aspectRatio", ar);
        cJSON_AddItemToArray(images, img);
        cJSON_AddItemToObject(root, "images", images);
        char *body = json_of(root);

        wf_embed_view_images v = {0};
        WF_CHECK(wf_embed_parse_images_view(NULL, 0, &v) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_embed_parse_images_view("{}", 2, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_embed_parse_images_view("not json", 8, &v) == WF_ERR_PARSE);
        WF_CHECK(wf_embed_parse_images_view(body, strlen(body), &v) == WF_OK);
        WF_CHECK(v.count == 1);
        if (v.count == 1) {
            WF_CHECK(strcmp(v.items[0].thumb, "https://cdn/thumb/1") == 0);
            WF_CHECK(strcmp(v.items[0].fullsize, "https://cdn/full/1") == 0);
            WF_CHECK(strcmp(v.items[0].alt, "a cat") == 0);
            WF_CHECK(v.items[0].has_aspect && v.items[0].width == 4 &&
                     v.items[0].height == 3);
        }
        wf_embed_view_images_free(&v);
        WF_CHECK(v.count == 0 && v.items == NULL);
        free(body);
    }

    /* video#view */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "cid", "bafyrevid");
        cJSON_AddStringToObject(root, "playlist", "https://cdn/blob/p.m3u8");
        cJSON_AddStringToObject(root, "thumbnail", "https://cdn/img/thumb/v");
        cJSON_AddStringToObject(root, "alt", "a clip");
        cJSON_AddStringToObject(root, "presentation", "gif");
        cJSON *ar = cJSON_CreateObject();
        cJSON_AddNumberToObject(ar, "width", 16);
        cJSON_AddNumberToObject(ar, "height", 9);
        cJSON_AddItemToObject(root, "aspectRatio", ar);
        char *body = json_of(root);

        wf_embed_view_video v = {0};
        WF_CHECK(wf_embed_parse_video_view(body, strlen(body), &v) == WF_OK);
        WF_CHECK(strcmp(v.cid, "bafyrevid") == 0);
        WF_CHECK(strcmp(v.playlist, "https://cdn/blob/p.m3u8") == 0);
        WF_CHECK(strcmp(v.thumbnail, "https://cdn/img/thumb/v") == 0);
        WF_CHECK(strcmp(v.alt, "a clip") == 0);
        WF_CHECK(strcmp(v.presentation, "gif") == 0);
        WF_CHECK(v.has_aspect && v.width == 16 && v.height == 9);
        wf_embed_view_video_free(&v);

        /* Missing required cid -> PARSE */
        cJSON *bad = cJSON_CreateObject();
        cJSON_AddStringToObject(bad, "playlist", "x");
        char *bad_body = json_of(bad);
        WF_CHECK(wf_embed_parse_video_view(bad_body, strlen(bad_body),
                                          &v) == WF_ERR_PARSE);
        free(bad_body);
        free(body);
    }

    /* external#view (card) */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *external = cJSON_CreateObject();
        cJSON_AddStringToObject(external, "uri", "https://example.com/a");
        cJSON_AddStringToObject(external, "title", "Article");
        cJSON_AddStringToObject(external, "description", "desc");
        cJSON_AddStringToObject(external, "thumb", "https://cdn/thumb/a");
        cJSON_AddStringToObject(external, "createdAt", "2024-03-01T00:00:00Z");
        cJSON_AddNumberToObject(external, "readingTime", 5);
        cJSON *labels = cJSON_CreateArray();
        cJSON_AddItemToObject(external, "labels", labels);
        cJSON_AddItemToObject(root, "external", external);
        char *body = json_of(root);

        wf_embed_view_external v = {0};
        WF_CHECK(wf_embed_parse_external_embed_view(NULL, 0, &v) ==
                  WF_ERR_INVALID_ARG);
        WF_CHECK(wf_embed_parse_external_embed_view(body, strlen(body),
                                                  &v) == WF_OK);
        WF_CHECK(strcmp(v.uri, "https://example.com/a") == 0);
        WF_CHECK(strcmp(v.title, "Article") == 0);
        WF_CHECK(strcmp(v.description, "desc") == 0);
        WF_CHECK(strcmp(v.thumb, "https://cdn/thumb/a") == 0);
        WF_CHECK(strcmp(v.created_at, "2024-03-01T00:00:00Z") == 0);
        WF_CHECK(v.has_reading_time && v.reading_time == 5);
        WF_CHECK(cJSON_IsArray(v.labels));
        wf_embed_view_external_free(&v);
        WF_CHECK(v.uri == NULL && v.labels == NULL);
        free(body);
    }

    /* record#view — viewRecord form */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *rec = cJSON_CreateObject();
        cJSON_AddStringToObject(rec, "$type", "app.bsky.embed.record#viewRecord");
        cJSON_AddStringToObject(rec, "uri", "at://did:plc:x/app.bsky.feed.post/r1");
        cJSON_AddStringToObject(rec, "cid", "bafyrec1");
        cJSON *author = cJSON_CreateObject();
        cJSON_AddStringToObject(author, "did", "did:plc:alice");
        cJSON_AddItemToObject(rec, "author", author);
        cJSON *value = cJSON_CreateObject();
        cJSON_AddStringToObject(value, "text", "quoted");
        cJSON_AddItemToObject(rec, "value", value);
        cJSON_AddStringToObject(rec, "indexedAt", "2024-04-01T00:00:00Z");
        cJSON_AddNumberToObject(rec, "replyCount", 1);
        cJSON_AddNumberToObject(rec, "likeCount", 9);
        cJSON_AddItemToObject(root, "record", rec);
        char *body = json_of(root);

        wf_embed_record_embed_view v = {0};
        WF_CHECK(wf_embed_parse_record_view(NULL, 0, &v) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_embed_parse_record_view(body, strlen(body), &v) == WF_OK);
        WF_CHECK(v.kind == WF_EMBED_REC_VIEW_RECORD);
        WF_CHECK(strcmp(v.uri, "at://did:plc:x/app.bsky.feed.post/r1") == 0);
        WF_CHECK(strcmp(v.record.cid, "bafyrec1") == 0);
        WF_CHECK(strcmp(v.record.indexed_at, "2024-04-01T00:00:00Z") == 0);
        WF_CHECK(v.record.has_reply_count && v.record.reply_count == 1);
        WF_CHECK(v.record.has_like_count && v.record.like_count == 9);
        WF_CHECK(v.record.author != NULL && v.record.value != NULL);
        wf_embed_record_embed_view_free(&v);
        WF_CHECK(v.uri == NULL && v.record.cid == NULL);
        free(body);
    }

    /* record#view — notFound variant (detached into extra) */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *rec = cJSON_CreateObject();
        cJSON_AddStringToObject(rec, "uri", "at://did:plc:x/app.bsky.feed.post/missing");
        cJSON_AddBoolToObject(rec, "notFound", 1);
        cJSON_AddItemToObject(root, "record", rec);
        char *body = json_of(root);

        wf_embed_record_embed_view v = {0};
        WF_CHECK(wf_embed_parse_record_view(body, strlen(body), &v) == WF_OK);
        WF_CHECK(v.kind == WF_EMBED_REC_VIEW_NOT_FOUND);
        WF_CHECK(strcmp(v.uri,
                         "at://did:plc:x/app.bsky.feed.post/missing") == 0);
        WF_CHECK(v.record.uri == NULL && v.extra != NULL);
        wf_embed_record_embed_view_free(&v);
        free(body);
    }

    /* recordWithMedia#view */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "$type",
                                "app.bsky.embed.recordWithMedia#view");
        cJSON *rec = cJSON_CreateObject();
        cJSON_AddStringToObject(rec, "$type", "app.bsky.embed.record#viewRecord");
        cJSON_AddStringToObject(rec, "uri", "at://did:plc:x/post/rm");
        cJSON_AddStringToObject(rec, "cid", "bafyrm");
        cJSON_AddStringToObject(rec, "indexedAt", "2024-05-01T00:00:00Z");
        cJSON *value = cJSON_CreateObject();
        cJSON_AddStringToObject(value, "text", "rm text");
        cJSON_AddItemToObject(rec, "value", value);
        cJSON_AddItemToObject(root, "record", rec);
        cJSON *media = cJSON_CreateObject();
        cJSON_AddStringToObject(media, "$type", "app.bsky.embed.images#view");
        cJSON *images = cJSON_CreateArray();
        cJSON_AddItemToObject(media, "images", images);
        cJSON_AddItemToObject(root, "media", media);
        char *body = json_of(root);

        wf_embed_record_with_media_view v = {0};
        WF_CHECK(wf_embed_parse_record_with_media_view(NULL, 0, &v) ==
                  WF_ERR_INVALID_ARG);
        WF_CHECK(wf_embed_parse_record_with_media_view(body, strlen(body),
                                                      &v) == WF_OK);
        WF_CHECK(v.record.kind == WF_EMBED_REC_VIEW_RECORD);
        WF_CHECK(strcmp(v.record.uri, "at://did:plc:x/post/rm") == 0);
        WF_CHECK(v.media != NULL);
        wf_embed_record_with_media_view_free(&v);
        free(body);
    }

    /* gallery#view (uses "thumbnail" key) */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *items = cJSON_CreateArray();
        cJSON *img = cJSON_CreateObject();
        cJSON_AddStringToObject(img, "thumbnail", "https://cdn/thumb/g1");
        cJSON_AddStringToObject(img, "fullsize", "https://cdn/full/g1");
        cJSON_AddStringToObject(img, "alt", "g1");
        cJSON *ar = cJSON_CreateObject();
        cJSON_AddNumberToObject(ar, "width", 1);
        cJSON_AddNumberToObject(ar, "height", 1);
        cJSON_AddItemToObject(img, "aspectRatio", ar);
        cJSON_AddItemToArray(items, img);
        cJSON_AddItemToObject(root, "items", items);
        char *body = json_of(root);

        wf_embed_view_gallery v = {0};
        WF_CHECK(wf_embed_parse_gallery_view(NULL, 0, &v) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_embed_parse_gallery_view(body, strlen(body), &v) == WF_OK);
        WF_CHECK(v.count == 1);
        if (v.count == 1) {
            WF_CHECK(strcmp(v.items[0].thumb, "https://cdn/thumb/g1") == 0);
            WF_CHECK(v.items[0].has_aspect && v.items[0].width == 1);
        }
        wf_embed_view_gallery_free(&v);
        free(body);
    }

    /* Dispatcher: parse an arbitrary embed object by $type */
    {
        /* images dispatch */
        cJSON *ri = cJSON_CreateObject();
        cJSON_AddStringToObject(ri, "$type", "app.bsky.embed.images#view");
        cJSON *images = cJSON_CreateArray();
        cJSON *img = cJSON_CreateObject();
        cJSON_AddStringToObject(img, "thumb", "https://cdn/t");
        cJSON_AddStringToObject(img, "fullsize", "https://cdn/f");
        cJSON_AddStringToObject(img, "alt", "x");
        cJSON_AddItemToArray(images, img);
        cJSON_AddItemToObject(ri, "images", images);
        char *bi = json_of(ri);
        wf_embed_view vi = {0};
        WF_CHECK(wf_embed_parse_view(bi, strlen(bi), &vi) == WF_OK);
        WF_CHECK(vi.kind == WF_EMBED_VIEW_IMAGES);
        WF_CHECK(vi.u.images.count == 1);
        wf_embed_view_free(&vi);
        free(bi);

        /* unknown $type -> PARSE */
        cJSON *ru = cJSON_CreateObject();
        cJSON_AddStringToObject(ru, "$type", "app.bsky.embed.unknown#view");
        char *bu = json_of(ru);
        wf_embed_view vu = {0};
        WF_CHECK(wf_embed_parse_view(bu, strlen(bu), &vu) == WF_ERR_PARSE);
        wf_embed_view_free(&vu);
        free(bu);
    }

    printf("embed_typed: all checks passed\n");
    return 0;
}
