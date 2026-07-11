/*
 * test_feedgen_typed.c — tests for the feed-generator typed parsers.
 *
 * Exercises wf_agent_parse_generators, wf_agent_parse_feed_views (and the
 * reused wf_agent_parse_feed for getActorLikes) plus the wrapper NULL
 * validation. Uses offline fixtures from test/fixtures.
 */

#include "wolfram/feedgen_typed.h"
#include "wolfram/feed_typed.h"

#include "test.h"

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
    /* ---- invalid args ---- */
    wf_agent_generator_view_list gens = {0};
    wf_status s = wf_agent_parse_generators(NULL, 0, &gens);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    s = wf_agent_parse_generators("{}", 2, NULL);
    WF_CHECK(s == WF_ERR_INVALID_ARG);

    wf_agent_feed_view_list views = {0};
    s = wf_agent_parse_feed_views(NULL, 0, &views);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    s = wf_agent_parse_feed_views("{}", 2, NULL);
    WF_CHECK(s == WF_ERR_INVALID_ARG);

    /* wrapper NULL validation (agent NULL) */
    s = wf_agent_get_actor_feeds_typed(NULL, "alice", 10, NULL, &gens);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    s = wf_agent_get_feed_typed(NULL, "at://x", 10, NULL, &views);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    wf_agent_feed_list likes = {0};
    s = wf_agent_get_actor_likes_typed(NULL, "alice", 10, NULL, &likes);
    WF_CHECK(s == WF_ERR_INVALID_ARG);

    /* ---- malformed JSON ---- */
    s = wf_agent_parse_generators("not json{", 9, &gens);
    WF_CHECK(s == WF_ERR_PARSE);
    s = wf_agent_parse_feed_views("not json{", 9, &views);
    WF_CHECK(s == WF_ERR_PARSE);

    /* ---- missing arrays ---- */
    s = wf_agent_parse_generators("{\"cursor\":\"x\"}", 14, &gens);
    WF_CHECK(s == WF_ERR_PARSE);
    s = wf_agent_parse_feed_views("{\"cursor\":\"x\"}", 14, &views);
    WF_CHECK(s == WF_ERR_PARSE);

    /* ---- actorFeeds fixture ---- */
    size_t len = 0;
    char *json = load_fixture("actorFeeds.json", &len);
    WF_CHECK(json != NULL);
    if (json) {
        s = wf_agent_parse_generators(json, len, &gens);
        WF_CHECK(s == WF_OK);
        WF_CHECK(gens.generator_count == 2);
        WF_CHECK(gens.cursor != NULL);
        WF_CHECK(strcmp(gens.cursor, "cursor-for-feeds") == 0);
        if (gens.generator_count == 2) {
            wf_agent_generator_view *g0 = &gens.generators[0];
            WF_CHECK(g0->uri != NULL);
            WF_CHECK(strcmp(g0->uri,
                "at://did:plc:alice/app.bsky.feed.generator/aaa111") == 0);
            WF_CHECK(g0->did != NULL);
            WF_CHECK(strcmp(g0->did, "did:plc:alice") == 0);
            WF_CHECK(g0->creator.did != NULL);
            WF_CHECK(strcmp(g0->creator.did, "did:plc:alice") == 0);
            WF_CHECK(g0->creator.handle != NULL);
            WF_CHECK(strcmp(g0->creator.handle, "alice.bsky.social") == 0);
            WF_CHECK(g0->display_name != NULL);
            WF_CHECK(strcmp(g0->display_name, "Alice's Cool Feed") == 0);
            WF_CHECK(g0->description != NULL);
            WF_CHECK(g0->avatar != NULL);
            WF_CHECK(g0->indexed_at != NULL);
            WF_CHECK(g0->has_like_count == 1);
            WF_CHECK(g0->like_count == 42);

            wf_agent_generator_view *g1 = &gens.generators[1];
            WF_CHECK(g1->like_count == 7);
            WF_CHECK(strcmp(g1->display_name, "Bob's News Feed") == 0);
        }
        wf_agent_generator_view_list_free(&gens);
        WF_CHECK(gens.generators == NULL && gens.generator_count == 0);
        free(json);
    }

    /* ---- feed fixture (feedViewPost list) ---- */
    json = load_fixture("feed.json", &len);
    WF_CHECK(json != NULL);
    if (json) {
        s = wf_agent_parse_feed_views(json, len, &views);
        WF_CHECK(s == WF_OK);
        WF_CHECK(views.item_count == 2);
        WF_CHECK(views.cursor != NULL);
        WF_CHECK(strcmp(views.cursor, "cursor-for-feed-views") == 0);
        if (views.item_count == 2) {
            WF_CHECK(views.items[0].post.uri != NULL);
            WF_CHECK(strcmp(views.items[0].post.uri,
                "at://did:plc:alice/app.bsky.feed.post/aaa111") == 0);
            WF_CHECK(views.items[0].post.author.did != NULL);
            WF_CHECK(strcmp(views.items[0].post.author.did, "did:plc:alice") == 0);
            WF_CHECK(views.items[0].post.record != NULL);
            WF_CHECK(views.items[1].post.author.handle != NULL);
            WF_CHECK(strcmp(views.items[1].post.author.handle,
                "bob.bsky.social") == 0);
            WF_CHECK(views.items[1].reason != NULL);
        }
        wf_agent_feed_view_list_free(&views);
        free(json);
    }

    /* ---- actorLikes fixture (reuses wf_agent_parse_feed) ---- */
    json = load_fixture("actorLikes.json", &len);
    WF_CHECK(json != NULL);
    if (json) {
        s = wf_agent_parse_feed(json, len, &likes);
        WF_CHECK(s == WF_OK);
        WF_CHECK(likes.item_count == 1);
        WF_CHECK(likes.cursor != NULL);
        WF_CHECK(strcmp(likes.cursor, "cursor-for-actor-likes") == 0);
        if (likes.item_count == 1) {
            wf_agent_feed_item *it = &likes.items[0];
            WF_CHECK(it->post.uri != NULL);
            WF_CHECK(strcmp(it->post.uri,
                "at://did:plc:carol/app.bsky.feed.post/ccc333") == 0);
            WF_CHECK(it->post.author.did != NULL);
            WF_CHECK(strcmp(it->post.author.did, "did:plc:carol") == 0);
            WF_CHECK(it->post.record != NULL);
        }
        wf_agent_feed_list_free(&likes);
        free(json);
    }

    WF_TEST_SUMMARY();
}
