/*
 * test_post_display.c — tests for post_display.h (display helpers over typed
 * feed posts) and wf_agent_fetch_public's argument/credential handling.
 */

#include "wolfram/post_display.h"

#include "../src/agent/_internal.h"
#include "test.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Build a post view whose record/embed are parsed from JSON (NULL = absent). */
static wf_agent_post_view make_post(const char *record, const char *embed) {
    wf_agent_post_view p;
    memset(&p, 0, sizeof(p));
    if (record) p.record = cJSON_Parse(record);
    if (embed) p.embed = cJSON_Parse(embed);
    return p;
}

static void free_post(wf_agent_post_view *p) {
    cJSON_Delete(p->record);
    cJSON_Delete(p->embed);
}

/* Run display on a record JSON; the caller frees `d`. */
static wf_status disp(const char *record, const char *embed,
                      wf_post_display *d) {
    wf_agent_post_view p = make_post(record, embed);
    wf_status s = wf_agent_post_view_display(&p, d);
    free_post(&p);
    return s;
}

static int facet_is(const wf_post_display *d, size_t i, wf_facet_kind kind,
                    size_t start, size_t end, const char *target) {
    return i < d->facet_count && d->facets[i].kind == kind &&
           d->facets[i].byte_start == start && d->facets[i].byte_end == end &&
           d->facets[i].target && strcmp(d->facets[i].target, target) == 0;
}

#define FACET(s, e, feat)                                                      \
    "{\"index\":{\"byteStart\":" #s ",\"byteEnd\":" #e "},\"features\":[" feat \
    "]}"
#define LINK(u) "{\"$type\":\"app.bsky.richtext.facet#link\",\"uri\":\"" u "\"}"
#define MENTION(d)                                                             \
    "{\"$type\":\"app.bsky.richtext.facet#mention\",\"did\":\"" d "\"}"
#define TAG(t) "{\"$type\":\"app.bsky.richtext.facet#tag\",\"tag\":\"" t "\"}"

static void test_basics(void) {
    wf_post_display d;
    /* NULL / odd arguments */
    WF_CHECK(wf_agent_post_view_display(NULL, &d) == WF_ERR_INVALID_ARG);
    wf_agent_post_view empty;
    memset(&empty, 0, sizeof(empty));
    WF_CHECK(wf_agent_post_view_display(&empty, NULL) == WF_ERR_INVALID_ARG);
    wf_post_display_free(NULL);

    /* Missing record and embed */
    WF_CHECK(wf_agent_post_view_display(&empty, &d) == WF_OK);
    WF_CHECK(d.text && d.text[0] == '\0');
    WF_CHECK(d.created_at == NULL && d.facet_count == 0 && d.facets == NULL);
    WF_CHECK(d.embed_kind == WF_EMBED_NONE && d.image_count == 0);
    WF_CHECK(d.is_reply == 0);
    wf_post_display_free(&d);
    WF_CHECK(d.text == NULL);
    wf_post_display_free(&d); /* double free is safe */

    /* Plain text, createdAt, reply */
    WF_CHECK(disp("{\"text\":\"hello\",\"createdAt\":\"2026-01-02T03:04:05Z\","
                  "\"reply\":{\"root\":{},\"parent\":{}}}",
                  NULL, &d) == WF_OK);
    WF_CHECK(strcmp(d.text, "hello") == 0);
    WF_CHECK(d.created_at && strcmp(d.created_at, "2026-01-02T03:04:05Z") == 0);
    WF_CHECK(d.is_reply == 1);
    wf_post_display_free(&d);

    /* Odd shapes: record is not an object / fields wrong types */
    const char *odd[] = {"[]",
                         "\"str\"",
                         "42",
                         "null",
                         "{\"text\":5,\"createdAt\":[],\"facets\":\"x\","
                         "\"reply\":3}",
                         "{\"facets\":[1,null,\"a\",[],{}]}",
                         "{\"text\":\"abc\",\"facets\":[{\"index\":5,"
                         "\"features\":7}]}"};
    for (size_t i = 0; i < sizeof(odd) / sizeof(odd[0]); ++i) {
        WF_CHECK(disp(odd[i], "[1]", &d) == WF_OK);
        WF_CHECK(d.text != NULL);
        WF_CHECK(d.facet_count == 0 && d.is_reply == 0);
        WF_CHECK(d.embed_kind == WF_EMBED_NONE);
        wf_post_display_free(&d);
    }
}

static void test_facets(void) {
    wf_post_display d;

    /* ASCII: link + mention + tag, given out of order -> sorted */
    WF_CHECK(
        disp(
            "{\"text\":\"hi @bob see x.com #tag\",\"facets\":[" FACET(
                18, 22,
                TAG("tag")) "," FACET(3, 7,
                                      MENTION(
                                          "did:plc:b")) "," FACET(12, 17,
                                                                  LINK(
                                                                      "https:/"
                                                                      "/x."
                                                                      "com")) "]}",
            NULL, &d) == WF_OK);
    WF_CHECK(d.facet_count == 3);
    WF_CHECK(facet_is(&d, 0, WF_FACET_MENTION, 3, 7, "did:plc:b"));
    WF_CHECK(facet_is(&d, 1, WF_FACET_LINK, 12, 17, "https://x.com"));
    WF_CHECK(facet_is(&d, 2, WF_FACET_TAG, 18, 22, "tag"));
    wf_post_display_free(&d);

    /* Emoji (4 bytes) before the facet shifts byte offsets: "😀 hi" -> 'hi' at
     * bytes 5..7. */
    WF_CHECK(disp("{\"text\":\"\xF0\x9F\x98\x80 hi\",\"facets\":[" FACET(
                      5, 7, LINK("https://a.b")) "]}",
                  NULL, &d) == WF_OK);
    WF_CHECK(d.facet_count == 1 &&
             facet_is(&d, 0, WF_FACET_LINK, 5, 7, "https://a.b"));
    wf_post_display_free(&d);

    /* CJK: each char is 3 bytes; "日本語" facet over the middle char 3..6 ok,
     * 1..6 / 3..5 are mid-codepoint and dropped. */
    WF_CHECK(disp("{\"text\":\"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\","
                  "\"facets\":[" FACET(3, 6, TAG("a")) "]}",
                  NULL, &d) == WF_OK);
    WF_CHECK(d.facet_count == 1 && facet_is(&d, 0, WF_FACET_TAG, 3, 6, "a"));
    wf_post_display_free(&d);
    const char *cjk = "{\"text\":\"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\","
                      "\"facets\":[%s]}";
    const char *bad_cjk[] = {FACET(1, 6, TAG("a")), FACET(3, 5, TAG("a")),
                             FACET(4, 9, TAG("a")), FACET(0, 4, TAG("a"))};
    for (size_t i = 0; i < 4; ++i) {
        char buf[512];
        snprintf(buf, sizeof(buf), cjk, bad_cjk[i]);
        WF_CHECK(disp(buf, NULL, &d) == WF_OK);
        WF_CHECK(d.facet_count == 0);
        wf_post_display_free(&d);
    }

    /* Whole-text facet reaching exactly the end is fine */
    WF_CHECK(disp("{\"text\":\"abc\",\"facets\":[" FACET(0, 3, TAG("t")) "]}",
                  NULL, &d) == WF_OK);
    WF_CHECK(d.facet_count == 1);
    wf_post_display_free(&d);

    /* Out of bounds / inverted / empty / negative / fractional / non-number */
    const char *bad[] = {
        FACET(0, 4, TAG("t")),
        FACET(4, 5, TAG("t")),
        FACET(2, 1, TAG("t")),
        FACET(2, 2, TAG("t")),
        FACET(-1, 2, TAG("t")),
        "{\"index\":{\"byteStart\":0.5,\"byteEnd\":2},\"features\":[" TAG(
            "t") "]}",
        "{\"index\":{\"byteStart\":\"0\",\"byteEnd\":2},\"features\":[" TAG(
            "t") "]}",
        "{\"index\":{\"byteStart\":0,\"byteEnd\":1e30},\"features\":[" TAG(
            "t") "]}",
        "{\"index\":{\"byteStart\":0},\"features\":[" TAG("t") "]}",
        "{\"features\":[" TAG("t") "]}",
        "{\"index\":{\"byteStart\":0,\"byteEnd\":2}}",
        "{\"index\":{\"byteStart\":0,\"byteEnd\":2},\"features\":[]}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char buf[512];
        snprintf(buf, sizeof(buf), "{\"text\":\"abc\",\"facets\":[%s]}",
                 bad[i]);
        WF_CHECK(disp(buf, NULL, &d) == WF_OK);
        WF_CHECK(d.facet_count == 0);
        wf_post_display_free(&d);
    }

    /* Overlap: later-by-start facet overlapping an earlier kept one is
     * dropped; an abutting one is kept. Out-of-order input. */
    WF_CHECK(
        disp(
            "{\"text\":\"0123456789\",\"facets\":[" FACET(4, 8, TAG("c")) "," FACET(
                0, 4, TAG("a")) "," FACET(2, 5,
                                          TAG("b")) "," FACET(8, 10,
                                                              TAG("d")) "]}",
            NULL, &d) == WF_OK);
    WF_CHECK(d.facet_count == 3);
    WF_CHECK(facet_is(&d, 0, WF_FACET_TAG, 0, 4, "a"));
    WF_CHECK(facet_is(&d, 1, WF_FACET_TAG, 4, 8, "c"));
    WF_CHECK(facet_is(&d, 2, WF_FACET_TAG, 8, 10, "d"));
    wf_post_display_free(&d);

    /* Identical ranges: exactly one survives */
    WF_CHECK(disp("{\"text\":\"abcd\",\"facets\":[" FACET(
                      0, 2, TAG("x")) "," FACET(0, 2, TAG("y")) "]}",
                  NULL, &d) == WF_OK);
    WF_CHECK(d.facet_count == 1);
    wf_post_display_free(&d);

    /* Unknown feature types skipped; first known one wins; a facet with only
     * unknown features, or a known feature missing its target, is dropped. */
    WF_CHECK(
        disp(
            "{\"text\":\"abcdef\",\"facets\":[" FACET(
                0, 2,
                "{\"$type\":\"x.y#z\",\"uri\":\"no\"}," LINK("https://yes") "," TAG(
                    "later")) "," FACET(2, 4,
                                        "{\"$type\":\"x.y#z\"}") "," FACET(4, 6,
                                                                           "{\""
                                                                           "$ty"
                                                                           "pe"
                                                                           "\":"
                                                                           "\"a"
                                                                           "pp."
                                                                           "bsk"
                                                                           "y."
                                                                           "ric"
                                                                           "hte"
                                                                           "xt."
                                                                           "fac"
                                                                           "et#"
                                                                           "lin"
                                                                           "k\""
                                                                           "}") "," FACET(4,
                                                                                          6,
                                                                                          "5,null," TAG(
                                                                                              "ok")) "]}",
            NULL, &d) == WF_OK);
    WF_CHECK(d.facet_count == 2);
    WF_CHECK(facet_is(&d, 0, WF_FACET_LINK, 0, 2, "https://yes"));
    WF_CHECK(facet_is(&d, 1, WF_FACET_TAG, 4, 6, "ok"));
    wf_post_display_free(&d);

    /* Facets with no text at all */
    WF_CHECK(disp("{\"facets\":[" FACET(0, 1, TAG("t")) "]}", NULL, &d) ==
             WF_OK);
    WF_CHECK(d.facet_count == 0);
    wf_post_display_free(&d);
}

#define IMGS_VIEW                                                              \
    "{\"$type\":\"app.bsky.embed.images#view\",\"images\":[{},{},{}]}"
#define EXT_VIEW                                                               \
    "{\"$type\":\"app.bsky.embed.external#view\",\"external\":{\"uri\":"       \
    "\"https://e.x/p\",\"title\":\"T\",\"description\":\"d\"}}"
#define VIDEO_VIEW "{\"$type\":\"app.bsky.embed.video#view\"}"
#define VIEW_RECORD                                                            \
    "{\"$type\":\"app.bsky.embed.record#viewRecord\",\"uri\":\"at://"          \
    "did:plc:q/"                                                               \
    "app.bsky.feed.post/1\",\"author\":{\"handle\":\"q.test\"},\"value\":"     \
    "{\"text\":\"quoted\"}}"

static void test_embeds(void) {
    wf_post_display d;

    WF_CHECK(disp("{}", IMGS_VIEW, &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_IMAGES && d.image_count == 3);
    WF_CHECK(d.external_uri == NULL && d.quote_uri == NULL);
    wf_post_display_free(&d);

    WF_CHECK(disp("{}", VIDEO_VIEW, &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_VIDEO && d.image_count == 0);
    wf_post_display_free(&d);

    WF_CHECK(disp("{}", EXT_VIEW, &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_EXTERNAL);
    WF_CHECK(d.external_title && strcmp(d.external_title, "T") == 0);
    WF_CHECK(d.external_uri && strcmp(d.external_uri, "https://e.x/p") == 0);
    wf_post_display_free(&d);

    WF_CHECK(
        disp("{}",
             "{\"$type\":\"app.bsky.embed.record#view\",\"record\":" VIEW_RECORD
             "}",
             &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_RECORD);
    WF_CHECK(d.quote_uri && strcmp(d.quote_uri, "at://did:plc:q/"
                                                "app.bsky.feed.post/1") == 0);
    WF_CHECK(d.quote_author_handle &&
             strcmp(d.quote_author_handle, "q.test") == 0);
    WF_CHECK(d.quote_text && strcmp(d.quote_text, "quoted") == 0);
    wf_post_display_free(&d);

    /* Non-viewable quoted records: kind RECORD, no quote fields */
    const char *nv[] = {
        "{\"$type\":\"app.bsky.embed.record#view\",\"record\":{\"$type\":"
        "\"app.bsky.embed.record#viewNotFound\",\"uri\":\"at://x\"}}",
        "{\"$type\":\"app.bsky.embed.record#view\",\"record\":{\"$type\":"
        "\"app.bsky.embed.record#viewBlocked\",\"uri\":\"at://x\"}}",
        "{\"$type\":\"app.bsky.embed.record#view\",\"record\":[]}",
        "{\"$type\":\"app.bsky.embed.record#view\"}"};
    for (size_t i = 0; i < 4; ++i) {
        WF_CHECK(disp("{}", nv[i], &d) == WF_OK);
        WF_CHECK(d.embed_kind == WF_EMBED_RECORD && d.quote_uri == NULL &&
                 d.quote_text == NULL && d.quote_author_handle == NULL);
        wf_post_display_free(&d);
    }

    /* viewRecord with partial fields */
    WF_CHECK(
        disp("{}",
             "{\"$type\":\"app.bsky.embed.record#view\",\"record\":{"
             "\"$type\":\"app.bsky.embed.record#viewRecord\",\"uri\":\"at://"
             "x\",\"author\":5,\"value\":{\"text\":7}}}",
             &d) == WF_OK);
    WF_CHECK(d.quote_uri && !d.quote_author_handle && !d.quote_text);
    wf_post_display_free(&d);

    /* recordWithMedia with each media kind */
    const char *media[] = {IMGS_VIEW, EXT_VIEW, VIDEO_VIEW};
    for (size_t i = 0; i < 3; ++i) {
        char buf[2048];
        snprintf(buf, sizeof(buf),
                 "{\"$type\":\"app.bsky.embed.recordWithMedia#view\","
                 "\"record\":{\"record\":%s},\"media\":%s}",
                 VIEW_RECORD, media[i]);
        WF_CHECK(disp("{}", buf, &d) == WF_OK);
        WF_CHECK(d.embed_kind == WF_EMBED_RECORD_WITH_MEDIA);
        WF_CHECK(d.quote_text && strcmp(d.quote_text, "quoted") == 0);
        WF_CHECK(d.image_count == (i == 0 ? 3u : 0u));
        WF_CHECK((d.external_uri != NULL) == (i == 1));
        wf_post_display_free(&d);
    }
    WF_CHECK(disp("{}", "{\"$type\":\"app.bsky.embed.recordWithMedia#view\"}",
                  &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_RECORD_WITH_MEDIA && !d.quote_uri);
    wf_post_display_free(&d);

    /* Unknown / odd embeds */
    const char *unk[] = {
        "{\"$type\":\"app.bsky.embed.future#view\"}", "{}", "{\"$type\":5}",
        "{\"$type\":\"app.bsky.embed.images\"}", "{\"images\":[{}]}"};
    for (size_t i = 0; i < 5; ++i) {
        WF_CHECK(disp("{}", unk[i], &d) == WF_OK);
        WF_CHECK(d.embed_kind == WF_EMBED_UNKNOWN && d.image_count == 0);
        wf_post_display_free(&d);
    }
    /* images with a non-array images member */
    WF_CHECK(disp("{}",
                  "{\"$type\":\"app.bsky.embed.images#view\",\"images\":{}}",
                  &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_IMAGES && d.image_count == 0);
    wf_post_display_free(&d);
    /* Non-object embeds are treated as absent */
    WF_CHECK(disp("{}", "\"x\"", &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_NONE);
    wf_post_display_free(&d);
}

/* The thumbnail data a client draws from: every image keeps its place in the
 * author's order, an image with no thumb is still counted, and the declared
 * aspect ratio is read as-is when it is sane and dropped when it is not. */
static void test_embed_images(void) {
    wf_post_display d;
    const char *view =
        "{\"$type\":\"app.bsky.embed.images#view\",\"images\":["
        "{\"thumb\":\"https://cdn/a.jpg\",\"alt\":\"a cat\","
        "\"aspectRatio\":{\"width\":4,\"height\":3}},"
        "{\"thumb\":\"https://cdn/b.jpg\"},"
        "{},"
        "{\"thumb\":\"\",\"aspectRatio\":{\"width\":0,\"height\":0}}]}";

    WF_CHECK(disp("{}", view, &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_IMAGES && d.image_count == 4);
    WF_CHECK(d.images[0].thumb &&
             strcmp(d.images[0].thumb, "https://cdn/a.jpg") == 0);
    WF_CHECK(d.images[0].alt && strcmp(d.images[0].alt, "a cat") == 0);
    WF_CHECK(d.images[0].width == 4 && d.images[0].height == 3);
    WF_CHECK(d.images[1].thumb && !d.images[1].alt);
    WF_CHECK(d.images[1].width == 0 && d.images[1].height == 0);
    WF_CHECK(d.images[2].thumb == NULL && d.images[2].alt == NULL);
    WF_CHECK(d.images[3].thumb == NULL);
    wf_post_display_free(&d);

    /* Alt text that is present but empty is not alt text, and a thumb that is
     * present but empty is not a URL: neither may be handed out as a string. */
    WF_CHECK(disp("{}",
                  "{\"$type\":\"app.bsky.embed.images#view\",\"images\":["
                  "{\"thumb\":\"https://cdn/a.jpg\",\"alt\":\"\"}]}",
                  &d) == WF_OK);
    WF_CHECK(d.image_count == 1 && d.images[0].thumb &&
             d.images[0].alt == NULL);
    wf_post_display_free(&d);

    /* Nonsense aspect ratios are dropped rather than believed, so a caller
     * dividing by width cannot divide by zero or by a number that overflows
     * its own box. */
    const char *ratios[] = {"{\"width\":0,\"height\":3}",
                            "{\"width\":3,\"height\":0}",
                            "{\"width\":-4,\"height\":3}",
                            "{\"width\":1.5,\"height\":3}",
                            "{\"width\":1e12,\"height\":3}",
                            "{\"width\":\"4\",\"height\":3}",
                            "{\"width\":4}",
                            "[]",
                            "5",
                            "{}",
                            "null",
                            "{\"width\":4,\"height\":\"3\"}"};
    for (size_t i = 0; i < sizeof(ratios) / sizeof(ratios[0]); ++i) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\"$type\":\"app.bsky.embed.images#view\",\"images\":"
                 "[{\"thumb\":\"https://cdn/a.jpg\",\"aspectRatio\":%s}]}",
                 ratios[i]);
        WF_CHECK(disp("{}", buf, &d) == WF_OK);
        /* Every one of these is dropped whole, and the thumb survives it: a
         * missing ratio costs the box its shape, not the image. */
        WF_CHECK(d.image_count == 1 && d.images[0].thumb);
        WF_CHECK(d.images[0].width == 0 && d.images[0].height == 0);
        wf_post_display_free(&d);
    }

    /* The media half of a recordWithMedia carries its images the same way. */
    WF_CHECK(
        disp("{}",
             "{\"$type\":\"app.bsky.embed.recordWithMedia#view\",\"record\":"
             "{\"record\":" VIEW_RECORD "},\"media\":" IMGS_VIEW "}",
             &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_RECORD_WITH_MEDIA && d.image_count == 3 &&
             d.images != NULL);
    WF_CHECK(d.images[0].thumb == NULL);
    wf_post_display_free(&d);
}

/* A link card needs the description and thumbnail beside the title and URI, and
 * an external view that has neither is still a link. */
static void test_embed_external(void) {
    wf_post_display d;

    WF_CHECK(disp("{}",
                  "{\"$type\":\"app.bsky.embed.external#view\",\"external\":"
                  "{\"uri\":\"https://e.x/p\",\"title\":\"T\","
                  "\"description\":\"d\",\"thumb\":\"https://cdn/t.jpg\"}}",
                  &d) == WF_OK);
    WF_CHECK(d.external_description &&
             strcmp(d.external_description, "d") == 0);
    WF_CHECK(d.external_thumb &&
             strcmp(d.external_thumb, "https://cdn/t.jpg") == 0);
    WF_CHECK(d.image_count == 0 && d.images == NULL);
    wf_post_display_free(&d);

#define EXT_BARE_TITLE                                                         \
    "{\"$type\":\"app.bsky.embed.external#view\",\"external\":{\"uri\":"       \
    "\"https://e.x/p\",\"description\":\"\",\"thumb\":7}}"
    const char *bare[] = {
        "{\"$type\":\"app.bsky.embed.external#view\"}",
        "{\"$type\":\"app.bsky.embed.external#view\",\"external\":{}}",
        EXT_BARE_TITLE};
    for (size_t i = 0; i < 3; ++i) {
        WF_CHECK(disp("{}", bare[i], &d) == WF_OK);
        WF_CHECK(d.embed_kind == WF_EMBED_EXTERNAL);
        WF_CHECK(d.external_description == NULL && d.external_thumb == NULL);
        wf_post_display_free(&d);
    }
}

/* A video view keeps its poster, alt text and whole aspect ratio; with none of
 * them it is still a video, and half a ratio is no ratio. */
static void test_embed_video(void) {
    wf_post_display d;

    WF_CHECK(disp("{}",
                  "{\"$type\":\"app.bsky.embed.video#view\",\"cid\":\"c\","
                  "\"playlist\":\"https://v/p.m3u8\","
                  "\"thumbnail\":\"https://v/t.jpg\",\"alt\":\"a cat\","
                  "\"aspectRatio\":{\"width\":1280,\"height\":720}}",
                  &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_VIDEO);
    WF_CHECK(d.video_thumb && strcmp(d.video_thumb, "https://v/t.jpg") == 0);
    WF_CHECK(d.video_alt && strcmp(d.video_alt, "a cat") == 0);
    WF_CHECK(d.video_width == 1280 && d.video_height == 720);
    WF_CHECK(d.image_count == 0 && d.external_thumb == NULL);
    wf_post_display_free(&d);

    WF_CHECK(disp("{}",
                  "{\"$type\":\"app.bsky.embed.video#view\","
                  "\"aspectRatio\":{\"width\":1280}}",
                  &d) == WF_OK);
    WF_CHECK(d.embed_kind == WF_EMBED_VIDEO && d.video_thumb == NULL &&
             d.video_alt == NULL && d.video_width == 0 && d.video_height == 0);
    wf_post_display_free(&d);
}

static void test_reposted_by(void) {
    wf_agent_feed_item it;
    char *name = (char *)0x1;

    WF_CHECK(wf_agent_feed_item_reposted_by(NULL, &name) == WF_ERR_INVALID_ARG);
    memset(&it, 0, sizeof(it));
    WF_CHECK(wf_agent_feed_item_reposted_by(&it, NULL) == WF_ERR_INVALID_ARG);

    /* absent reason */
    WF_CHECK(wf_agent_feed_item_reposted_by(&it, &name) == WF_OK);
    WF_CHECK(name == NULL);

    struct {
        const char *reason;
        const char *want; /* NULL = no name */
    } cases[] = {
        {"{\"$type\":\"app.bsky.feed.defs#reasonRepost\",\"by\":{\"handle\":"
         "\"h.test\",\"displayName\":\"Ann\"}}",
         "Ann"},
        {"{\"$type\":\"app.bsky.feed.defs#reasonRepost\",\"by\":{\"handle\":"
         "\"h.test\",\"displayName\":\"\"}}",
         "h.test"},
        {"{\"$type\":\"app.bsky.feed.defs#reasonRepost\",\"by\":{\"handle\":"
         "\"h.test\"}}",
         "h.test"},
        {"{\"$type\":\"app.bsky.feed.defs#reasonRepost\",\"by\":{}}", NULL},
        {"{\"$type\":\"app.bsky.feed.defs#reasonRepost\",\"by\":7}", NULL},
        {"{\"$type\":\"app.bsky.feed.defs#reasonRepost\"}", NULL},
        {"{\"$type\":\"app.bsky.feed.defs#reasonPin\",\"by\":{\"handle\":\"x\"}"
         "}",
         NULL},
        {"{\"by\":{\"handle\":\"x\"}}", NULL},
        {"[]", NULL},
        {"\"str\"", NULL},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        it.reason = cJSON_Parse(cases[i].reason);
        name = (char *)0x1;
        WF_CHECK(wf_agent_feed_item_reposted_by(&it, &name) == WF_OK);
        if (cases[i].want) {
            WF_CHECK(name && strcmp(name, cases[i].want) == 0);
        } else {
            WF_CHECK(name == NULL);
        }
        free(name);
        cJSON_Delete(it.reason);
        it.reason = NULL;
    }
}

/* ── wf_agent_fetch_public ─────────────────────────────────────────── */

static struct {
    int calls;
    int saw_auth;
    char url[256];
} g_cap;

static wf_status capture_handler(void *ud, const char *method, const char *url,
                                 const char *ct, const char *body,
                                 size_t body_len, const wf_http_header *hdrs,
                                 size_t nhdrs, wf_response *out) {
    (void)ud;
    (void)ct;
    (void)body;
    (void)body_len;
    g_cap.calls++;
    g_cap.saw_auth = 0;
    snprintf(g_cap.url, sizeof(g_cap.url), "%s", url);
    for (size_t i = 0; i < nhdrs; ++i) {
        if (hdrs[i].name && strncasecmp(hdrs[i].name, "authorization", 13) == 0)
            g_cap.saw_auth = 1;
        if (hdrs[i].name && strncasecmp(hdrs[i].name, "dpop", 4) == 0)
            g_cap.saw_auth = 1;
    }
    WF_CHECK(strcmp(method, "GET") == 0);
    out->status = 200;
    out->body = strdup("png");
    out->body_len = 3;
    return WF_OK;
}

static void test_fetch_public(void) {
    wf_response r;
    wf_agent *agent = wf_agent_new("https://pds.example");
    WF_CHECK(agent != NULL);
    if (!agent) return;

    memset(&r, 0, sizeof(r));
    WF_CHECK(wf_agent_fetch_public(NULL, "https://a.b/x", 10, &r) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_fetch_public(agent, NULL, 10, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_fetch_public(agent, "https://a.b/x", 10, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_fetch_public(agent, "https://a.b/x", 0, &r) ==
             WF_ERR_INVALID_ARG);
    const char *bad_urls[] = {
        "http://a.b/x", "ftp://a.b/x",  "file:///etc/passwd", "a.b/x", "",
        "https://",     "HTTPS://a.b/x"};
    for (size_t i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); ++i) {
        WF_CHECK(wf_agent_fetch_public(agent, bad_urls[i], 10, &r) ==
                 WF_ERR_INVALID_ARG);
    }
    WF_CHECK(g_cap.calls == 0);

    /* Credential property, using the transport test seam. */
    wf_xrpc_set_handler(agent->client, capture_handler, NULL);
    wf_xrpc_client_set_auth(agent->client, "secret-token");

    /* control: the authenticated path does send it */
    WF_CHECK(wf_http_get(agent->client, "https://pds.example/x", &r) == WF_OK);
    WF_CHECK(g_cap.saw_auth == 1);
    wf_response_free(&r);

    WF_CHECK(wf_agent_fetch_public(agent, "https://cdn.example/a.jpg", 1024,
                                   &r) == WF_OK);
    WF_CHECK(g_cap.saw_auth == 0);
    WF_CHECK(strcmp(g_cap.url, "https://cdn.example/a.jpg") == 0);
    WF_CHECK(r.body_len == 3 && memcmp(r.body, "png", 3) == 0);
    wf_response_free(&r);

    /* the agent's own client still carries its credential afterwards */
    WF_CHECK(wf_http_get(agent->client, "https://pds.example/x", &r) == WF_OK);
    WF_CHECK(g_cap.saw_auth == 1);
    wf_response_free(&r);

    wf_agent_free(agent);
}

int main(void) {
    test_basics();
    test_facets();
    test_embeds();
    test_embed_images();
    test_embed_external();
    test_embed_video();
    test_reposted_by();
    test_fetch_public();
    WF_TEST_SUMMARY();
}
