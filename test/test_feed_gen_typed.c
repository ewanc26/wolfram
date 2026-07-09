/*
 * test_feed_gen_typed.c — offline tests for the feed-generator + discovery
 * typed parsers. Builds representative response bodies with cJSON, asserts the
 * owned structs are populated correctly, then frees them. Agent wrappers
 * require live auth and are exercised only for NULL-argument validation.
 */

#include "wolfram/feed_gen_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a minimal profileView object under `parent` at `key`. */
static void add_profile(cJSON *parent, const char *key, const char *did,
                        const char *handle, const char *name) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "did", did);
    cJSON_AddStringToObject(p, "handle", handle);
    cJSON_AddStringToObject(p, "displayName", name);
    cJSON_AddItemToObject(parent, key, p);
}

/* Build a generatorView object. */
static cJSON *make_generator(const char *uri, const char *cid,
                             const char *display_name, const char *desc,
                             int64_t like_count) {
    cJSON *g = cJSON_CreateObject();
    cJSON_AddStringToObject(g, "uri", uri);
    cJSON_AddStringToObject(g, "cid", cid);
    add_profile(g, "creator", "did:plc:creator000000000000000000",
                "maker.bsky.social", "Maker");
    cJSON_AddStringToObject(g, "displayName", display_name);
    cJSON_AddStringToObject(g, "description", desc);
    cJSON_AddStringToObject(g, "avatar", "https://cdn.bsky.app/img/gen.jpg");
    cJSON_AddNumberToObject(g, "likeCount", (double)like_count);
    cJSON_AddBoolToObject(g, "acceptsInteractions", 1);
    cJSON_AddStringToObject(g, "contentMode", "app.bsky.feed.defs#contentPost");
    cJSON_AddStringToObject(g, "indexedAt", "2026-07-01T09:00:00.000Z");
    /* Open/unbounded fields that must land in `extra`. */
    cJSON *facets = cJSON_CreateArray();
    cJSON_AddItemToObject(g, "descriptionFacets", facets);
    cJSON *labels = cJSON_CreateArray();
    cJSON_AddItemToObject(g, "labels", labels);
    cJSON *viewer = cJSON_CreateObject();
    cJSON_AddStringToObject(viewer, "like", "at://did:plc:x/app.bsky.feed.generator.like/y");
    cJSON_AddItemToObject(g, "viewer", viewer);
    return g;
}

static char *json_of(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

int main(void) {
    /* ---- Invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_feedgen_generator_list gl = {0};
    WF_CHECK(wf_feedgen_parse_generators(NULL, 0, &gl) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_parse_generators("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    wf_feedgen_generator_detail gd = {0};
    WF_CHECK(wf_feedgen_parse_feed_generator(NULL, 0, &gd) ==
             WF_ERR_INVALID_ARG);

    wf_feedgen_search_result_list sl = {0};
    WF_CHECK(wf_feedgen_parse_search_posts(NULL, 0, &sl) ==
             WF_ERR_INVALID_ARG);

    /* ---- getFeedGenerators body (built with cJSON) ---- */
    cJSON *root = cJSON_CreateObject();
    cJSON *feeds = cJSON_CreateArray();
    cJSON_AddItemToArray(feeds, make_generator(
                             "at://did:plc:gen1/app.bsky.feed.generator/self",
                             "bafygencid1111111111111111111111111111111",
                             "What's Hot", "Trending posts.", 1234));
    cJSON_AddItemToArray(feeds, make_generator(
                             "at://did:plc:gen2/app.bsky.feed.generator/self",
                             "bafygencid2222222222222222222222222222222",
                             "Cat Pics", "Just cats.", 7));
    cJSON_AddItemToObject(root, "feeds", feeds);
    cJSON_AddStringToObject(root, "cursor", "next-cursor");
    char *gen_json = json_of(root);

    WF_CHECK(wf_feedgen_parse_generators(gen_json, strlen(gen_json), &gl) ==
             WF_OK);
    WF_CHECK(gl.generator_count == 2);
    WF_CHECK(gl.cursor && strcmp(gl.cursor, "next-cursor") == 0);
    WF_CHECK(gl.generators[0].uri &&
             strstr(gl.generators[0].uri, "did:plc:gen1") != NULL);
    WF_CHECK(gl.generators[0].cid && strncmp(gl.generators[0].cid, "bafy", 4) == 0);
    WF_CHECK(gl.generators[0].creator.did &&
             strcmp(gl.generators[0].creator.did,
                    "did:plc:creator000000000000000000") == 0);
    WF_CHECK(gl.generators[0].creator.handle &&
             strcmp(gl.generators[0].creator.handle, "maker.bsky.social") == 0);
    WF_CHECK(gl.generators[0].display_name &&
             strcmp(gl.generators[0].display_name, "What's Hot") == 0);
    WF_CHECK(gl.generators[0].description &&
             strcmp(gl.generators[0].description, "Trending posts.") == 0);
    WF_CHECK(gl.generators[0].avatar &&
             strcmp(gl.generators[0].avatar, "https://cdn.bsky.app/img/gen.jpg") == 0);
    WF_CHECK(gl.generators[0].has_like_count && gl.generators[0].like_count == 1234);
    WF_CHECK(gl.generators[0].has_accepts_interactions &&
             gl.generators[0].accepts_interactions == true);
    WF_CHECK(gl.generators[0].content_mode &&
             strstr(gl.generators[0].content_mode, "contentPost") != NULL);
    WF_CHECK(gl.generators[0].indexed_at &&
             strcmp(gl.generators[0].indexed_at, "2026-07-01T09:00:00.000Z") == 0);
    WF_CHECK(gl.generators[0].extra != NULL);
    WF_CHECK(gl.generators[1].display_name &&
             strcmp(gl.generators[1].display_name, "Cat Pics") == 0);
    WF_CHECK(gl.generators[1].has_like_count && gl.generators[1].like_count == 7);
    wf_feedgen_generator_list_free(&gl);
    WF_CHECK(gl.generator_count == 0 && gl.generators == NULL && gl.cursor == NULL);
    free(gen_json);

    /* ---- getFeedGenerator detail body ---- */
    cJSON *droot = cJSON_CreateObject();
    cJSON_AddItemToObject(droot, "view",
                          make_generator(
                              "at://did:plc:gen1/app.bsky.feed.generator/self",
                              "bafygencid1111111111111111111111111111111",
                              "What's Hot", "Trending posts.", 1234));
    cJSON_AddBoolToObject(droot, "isOnline", 1);
    cJSON_AddBoolToObject(droot, "isValid", 1);
    char *detail_json = json_of(droot);

    WF_CHECK(wf_feedgen_parse_feed_generator(detail_json, strlen(detail_json),
                                             &gd) == WF_OK);
    WF_CHECK(gd.is_online == true && gd.is_valid == true);
    WF_CHECK(gd.view.display_name &&
             strcmp(gd.view.display_name, "What's Hot") == 0);
    WF_CHECK(gd.view.extra != NULL);
    wf_feedgen_generator_detail_free(&gd);
    WF_CHECK(gd.view.uri == NULL && gd.view.display_name == NULL);
    free(detail_json);

    /* ---- getLikes body (reused wf_agent_like_list) ---- */
    cJSON *lroot = cJSON_CreateObject();
    cJSON *likes = cJSON_CreateArray();
    for (int i = 0; i < 2; ++i) {
        cJSON *like = cJSON_CreateObject();
        char did[64];
        snprintf(did, sizeof(did), "did:plc:liker%08x", i);
        add_profile(like, "actor", did, "liker.bsky.social", "Liker");
        cJSON_AddStringToObject(like, "createdAt", "2026-07-02T08:00:00.000Z");
        cJSON_AddStringToObject(like, "indexedAt", "2026-07-02T08:00:00.000Z");
        cJSON_AddItemToArray(likes, like);
    }
    cJSON_AddItemToObject(lroot, "likes", likes);
    cJSON_AddStringToObject(lroot, "cursor", "like-cursor");
    char *likes_json = json_of(lroot);

    wf_agent_like_list ll = {0};
    WF_CHECK(wf_agent_parse_likes(likes_json, strlen(likes_json), &ll) ==
             WF_OK);
    WF_CHECK(ll.like_count == 2);
    WF_CHECK(ll.cursor && strcmp(ll.cursor, "like-cursor") == 0);
    WF_CHECK(ll.likes[0].actor.did != NULL);
    wf_agent_like_list_free(&ll);
    free(likes_json);

    /* ---- searchPosts body (postView array) ---- */
    cJSON *sroot = cJSON_CreateObject();
    cJSON *posts = cJSON_CreateArray();
    cJSON *post = cJSON_CreateObject();
    cJSON_AddStringToObject(post, "uri", "at://did:plc:a/app.bsky.feed.post/1");
    cJSON_AddStringToObject(post, "cid", "bafypost11111111111111111111111111");
    add_profile(post, "author", "did:plc:author000000000000000000",
                "author.bsky.social", "Author");
    cJSON_AddNumberToObject(post, "likeCount", 5.0);
    cJSON_AddNumberToObject(post, "repostCount", 2.0);
    cJSON_AddStringToObject(post, "indexedAt", "2026-07-03T10:00:00.000Z");
    cJSON_AddItemToArray(posts, post);
    cJSON_AddItemToObject(sroot, "posts", posts);
    cJSON_AddStringToObject(sroot, "cursor", "post-cursor");
    cJSON_AddNumberToObject(sroot, "hitsTotal", 42.0);
    char *search_json = json_of(sroot);

    WF_CHECK(wf_feedgen_parse_search_posts(search_json, strlen(search_json),
                                           &sl) == WF_OK);
    WF_CHECK(sl.post_count == 1);
    WF_CHECK(sl.has_hits_total && sl.hits_total == 42);
    WF_CHECK(sl.cursor && strcmp(sl.cursor, "post-cursor") == 0);
    WF_CHECK(sl.posts[0].uri &&
             strstr(sl.posts[0].uri, "app.bsky.feed.post/1") != NULL);
    WF_CHECK(sl.posts[0].author.handle &&
             strcmp(sl.posts[0].author.handle, "author.bsky.social") == 0);
    WF_CHECK(sl.posts[0].has_like_count && sl.posts[0].like_count == 5);
    WF_CHECK(sl.posts[0].has_repost_count && sl.posts[0].repost_count == 2);
    WF_CHECK(sl.posts[0].extra != NULL);
    wf_feedgen_search_result_list_free(&sl);
    WF_CHECK(sl.post_count == 0 && sl.posts == NULL);
    free(search_json);

    /* ---- Agent wrapper NULL validation ---- */
    const char *feeds_arr[] = {"at://did:plc:gen1/app.bsky.feed.generator/self"};
    const char *actor = "did:plc:alice";
    wf_feedgen_generator_detail d2 = {0};
    wf_feedgen_generator_list g2 = {0};
    wf_agent_like_list l2 = {0};
    wf_agent_actor_list a2 = {0};
    wf_agent_feed_list f2 = {0};
    wf_feedgen_search_result_list s2 = {0};

    WF_CHECK(wf_feedgen_get_feed_generator_typed(NULL, feeds_arr[0], &d2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_feed_generator_typed(NULL, NULL, &d2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_feed_generator_typed(NULL, feeds_arr[0], NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_feed_generators_typed(NULL, feeds_arr, 1, &g2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_feed_generators_typed(NULL, NULL, 0, &g2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_feed_generators_typed(NULL, feeds_arr, 1, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_actor_feeds_typed(NULL, actor, 10, NULL, &g2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_actor_feeds_typed(NULL, NULL, 10, NULL, &g2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_actor_feeds_typed(NULL, actor, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_suggested_feeds_typed(NULL, 10, NULL, &g2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_suggested_feeds_typed(NULL, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_likes_typed(NULL, "uri", NULL, 10, NULL, &l2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_likes_typed(NULL, NULL, NULL, 10, NULL, &l2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_likes_typed(NULL, "uri", NULL, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_reposted_by_typed(NULL, "uri", NULL, 10, NULL,
                                              &a2) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_reposted_by_typed(NULL, NULL, NULL, 10, NULL,
                                              &a2) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_quotes_typed(NULL, "uri", 10, NULL, &f2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_quotes_typed(NULL, NULL, 10, NULL, &f2) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_actor_likes_typed(NULL, actor, 10, NULL, &f2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_actor_likes_typed(NULL, NULL, 10, NULL, &f2) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_get_list_feed_typed(NULL, "list", 10, NULL, &f2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_get_list_feed_typed(NULL, NULL, 10, NULL, &f2) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_search_posts_typed(NULL, "q", 10, NULL, &s2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_search_posts_typed(NULL, NULL, 10, NULL, &s2) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_feedgen_search_posts_v2_typed(NULL, "q", 10, NULL, &s2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_feedgen_search_posts_v2_typed(NULL, NULL, 10, NULL, &s2) ==
             WF_ERR_INVALID_ARG);

    printf("feed_gen_typed: all checks passed\n");
    return 0;
}
