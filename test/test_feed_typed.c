/*
 * test_feed_typed.c — tests for the typed feed parser (getTimeline shape).
 */

#include "wolfram/feed_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    char *buf = NULL;
    long size;
    size_t len;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    len = (size_t)size;
    buf = malloc(len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, len, fp) != len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    if (len_out) *len_out = len;
    return buf;
}

static char *load_fixture(const char *name, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, name);
    return read_entire_file(path, len_out);
}

int main(void) {
    /* Invalid args. */
    wf_agent_feed_list list = {0};
    WF_CHECK(wf_agent_parse_feed(NULL, 0, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_feed("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    /* Malformed JSON. */
    WF_CHECK(wf_agent_parse_feed("{not json", 8, &list) == WF_ERR_PARSE);

    /* Missing required `feed` array. */
    WF_CHECK(wf_agent_parse_feed("{\"cursor\":\"x\"}", 14, &list) == WF_ERR_PARSE);

    /* Parse the fixture. */
    size_t len = 0;
    char *json = load_fixture("timeline.json", &len);
    WF_CHECK(json != NULL);

    if (json) {
        wf_status st = wf_agent_parse_feed(json, len, &list);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.item_count == 3);
        WF_CHECK(list.cursor && strcmp(list.cursor, "cursor-abc-123") == 0);

        /* Item 0: simple post with viewer.like. */
        wf_agent_feed_item *it0 = &list.items[0];
        WF_CHECK(strcmp(it0->post.uri,
                        "at://did:plc:alice/app.bsky.feed.post/aaa111") == 0);
        WF_CHECK(strcmp(it0->post.cid,
                        "bafyreighpostaaaa1111111111111111111111111111111111111111aaa") == 0);
        WF_CHECK(strcmp(it0->post.author.did, "did:plc:alice") == 0);
        WF_CHECK(strcmp(it0->post.author.handle, "alice.bsky.social") == 0);
        WF_CHECK(strcmp(it0->post.author.display_name, "Alice") == 0);
        WF_CHECK(it0->post.record != NULL);
        WF_CHECK(it0->post.embed == NULL);
        WF_CHECK(it0->post.has_reply_count && it0->post.reply_count == 3);
        WF_CHECK(it0->post.has_repost_count && it0->post.repost_count == 5);
        WF_CHECK(it0->post.has_like_count && it0->post.like_count == 42);
        WF_CHECK(it0->post.has_quote_count && it0->post.quote_count == 1);
        WF_CHECK(strcmp(it0->post.indexed_at, "2024-01-01T00:00:01.000Z") == 0);
        WF_CHECK(it0->post.viewer.like &&
                 strcmp(it0->post.viewer.like,
                        "at://did:plc:me/app.bsky.feed.like/like1") == 0);
        WF_CHECK(it0->post.viewer.has_reply_disabled &&
                 it0->post.viewer.reply_disabled == 0);
        WF_CHECK(it0->reason == NULL);
        WF_CHECK(it0->reply == NULL);

        /* Item 1: post with embed + reply ref + feedContext. */
        wf_agent_feed_item *it1 = &list.items[1];
        WF_CHECK(it1->post.embed != NULL);
        WF_CHECK(it1->post.has_bookmark_count && it1->post.bookmark_count == 2);
        WF_CHECK(it1->reply != NULL);
        WF_CHECK(it1->feed_context &&
                 strcmp(it1->feed_context, "ctx-bob-42") == 0);

        /* Item 2: reason repost + viewer.repost/bookmarked/pinned. */
        wf_agent_feed_item *it2 = &list.items[2];
        WF_CHECK(it2->reason != NULL);
        WF_CHECK(it2->post.viewer.repost &&
                 strcmp(it2->post.viewer.repost,
                        "at://did:plc:me/app.bsky.feed.repost/myrepost") == 0);
        WF_CHECK(it2->post.viewer.has_bookmarked && it2->post.viewer.bookmarked == 1);
        WF_CHECK(it2->post.viewer.has_pinned && it2->post.viewer.pinned == 0);
        WF_CHECK(it2->post.has_like_count && it2->post.like_count == 100);

        wf_agent_feed_list_free(&list);
        WF_CHECK(list.items == NULL);
        WF_CHECK(list.item_count == 0);
        WF_CHECK(list.cursor == NULL);

        free(json);
    }

    /* Wrapper arg validation (no network; NULL agent/args rejected). */
    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);
    wf_agent_feed_list wlist = {0};
    WF_CHECK(wf_agent_get_timeline_typed(NULL, 10, NULL, &wlist) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_timeline_typed(agent, 10, NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_author_feed_typed(agent, NULL, 10, NULL, NULL, NULL) == WF_ERR_INVALID_ARG);
    wf_agent_free(agent);

    WF_TEST_SUMMARY();
}
