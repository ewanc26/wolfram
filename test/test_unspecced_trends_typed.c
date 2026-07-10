/*
 * test_unspecced_trends_typed.c — offline tests for the owned typed parsers
 * and agent wrappers in unspecced_trends_typed.c. Builds sample JSON with cJSON,
 * exercises each parser, asserts fields, frees, and asserts the agent wrappers
 * reject NULL/invalid arguments with WF_ERR_INVALID_ARG.
 */

#include "wolfram/unspecced_trends_typed.h"

#include <cJSON.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);  \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static char *to_string(cJSON *node) {
    char *s = cJSON_PrintUnformatted(node);
    return s;
}

static int test_parse_trends(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *trends = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON_AddItemToObject(t, "topic", cJSON_CreateString("BreakingNews"));
    cJSON_AddItemToObject(t, "displayName", cJSON_CreateString("Breaking News"));
    cJSON_AddItemToObject(t, "link", cJSON_CreateString("#breakingnews"));
    cJSON_AddItemToObject(t, "startedAt", cJSON_CreateString("2026-01-01T00:00:00Z"));
    cJSON_AddItemToObject(t, "postCount", cJSON_CreateNumber(1234));
    cJSON_AddItemToObject(t, "status", cJSON_CreateString("active"));
    cJSON_AddItemToObject(t, "category", cJSON_CreateString("news"));
    cJSON *actors = cJSON_CreateArray();
    cJSON *a = cJSON_CreateObject();
    cJSON_AddItemToObject(a, "did", cJSON_CreateString("did:plc:abc"));
    cJSON_AddItemToObject(a, "handle", cJSON_CreateString("alice.bsky.social"));
    cJSON_AddItemToObject(a, "displayName", cJSON_CreateString("Alice"));
    cJSON_AddItemToObject(a, "avatar", cJSON_CreateString("https://x/a.png"));
    cJSON_AddItemToArray(actors, a);
    cJSON_AddItemToObject(t, "actors", actors);
    cJSON_AddItemToArray(trends, t);
    cJSON_AddItemToObject(root, "trends", trends);

    char *json = to_string(root);
    cJSON_Delete(root);
    CHECK(json != NULL);

    wf_unspecced_trend_list list = {0};
    wf_status st = wf_unspecced_parse_trends(json, strlen(json), &list);
    free(json);
    CHECK(st == WF_OK);
    CHECK(list.trend_count == 1);
    CHECK(list.trends != NULL);
    CHECK(list.trends[0].topic != NULL);
    CHECK(strcmp(list.trends[0].topic, "BreakingNews") == 0);
    CHECK(list.trends[0].display_name != NULL);
    CHECK(strcmp(list.trends[0].display_name, "Breaking News") == 0);
    CHECK(list.trends[0].link != NULL);
    CHECK(list.trends[0].started_at != NULL);
    CHECK(list.trends[0].has_post_count == 1);
    CHECK(list.trends[0].post_count == 1234);
    CHECK(list.trends[0].status != NULL);
    CHECK(list.trends[0].category != NULL);
    CHECK(list.trends[0].actors.actor_count == 1);
    CHECK(list.trends[0].actors.actors != NULL);
    CHECK(list.trends[0].actors.actors[0].did != NULL);
    CHECK(strcmp(list.trends[0].actors.actors[0].did, "did:plc:abc") == 0);

    wf_unspecced_trend_list_free(&list);
    CHECK(list.trend_count == 0);
    CHECK(list.trends == NULL);
    return 0;
}

static int test_parse_suggested_users(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *actors = cJSON_CreateArray();
    cJSON *a = cJSON_CreateObject();
    cJSON_AddItemToObject(a, "did", cJSON_CreateString("did:plc:u1"));
    cJSON_AddItemToObject(a, "handle", cJSON_CreateString("u1.bsky.social"));
    cJSON_AddItemToArray(actors, a);
    cJSON_AddItemToObject(root, "actors", actors);

    char *json = to_string(root);
    cJSON_Delete(root);
    CHECK(json != NULL);

    wf_agent_actor_list list = {0};
    wf_status st = wf_unspecced_parse_suggested_users(json, strlen(json), &list);
    free(json);
    CHECK(st == WF_OK);
    CHECK(list.actor_count == 1);
    CHECK(list.actors != NULL);
    CHECK(strcmp(list.actors[0].did, "did:plc:u1") == 0);

    wf_agent_actor_list_free(&list);
    return 0;
}

static int test_parse_suggested_feeds(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *feeds = cJSON_CreateArray();
    cJSON *f = cJSON_CreateObject();
    cJSON_AddItemToObject(f, "uri", cJSON_CreateString("at://did/app.bsky.feed.generator/1"));
    cJSON_AddItemToObject(f, "displayName", cJSON_CreateString("My Feed"));
    cJSON_AddItemToArray(feeds, f);
    cJSON_AddItemToObject(root, "feeds", feeds);

    char *json = to_string(root);
    cJSON_Delete(root);
    CHECK(json != NULL);

    wf_agent_generator_view_list list = {0};
    wf_status st = wf_unspecced_parse_suggested_feeds(json, strlen(json), &list);
    free(json);
    CHECK(st == WF_OK);
    CHECK(list.generator_count == 1);
    CHECK(list.generators != NULL);
    CHECK(strcmp(list.generators[0].display_name, "My Feed") == 0);

    wf_agent_generator_view_list_free(&list);
    return 0;
}

static int test_parse_thread_v2(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *thread = cJSON_CreateArray();
    cJSON *it = cJSON_CreateObject();
    cJSON_AddItemToObject(it, "uri", cJSON_CreateString("at://did/app.bsky.feed.post/1"));
    cJSON_AddItemToObject(it, "depth", cJSON_CreateNumber(0));
    cJSON *val = cJSON_CreateObject();
    cJSON_AddItemToObject(val, "text", cJSON_CreateString("hello"));
    cJSON_AddItemToObject(it, "value", val);
    cJSON_AddItemToArray(thread, it);
    cJSON_AddItemToObject(root, "thread", thread);
    cJSON_AddItemToObject(root, "hasOtherReplies", cJSON_CreateBool(1));

    char *json = to_string(root);
    cJSON_Delete(root);
    CHECK(json != NULL);

    wf_unspecced_thread_v2 list = {0};
    wf_status st = wf_unspecced_parse_thread_v2(json, strlen(json), &list);
    free(json);
    CHECK(st == WF_OK);
    CHECK(list.item_count == 1);
    CHECK(list.items != NULL);
    CHECK(list.items[0].uri != NULL);
    CHECK(strcmp(list.items[0].uri, "at://did/app.bsky.feed.post/1") == 0);
    CHECK(list.items[0].has_depth == 1);
    CHECK(list.items[0].depth == 0);
    CHECK(list.items[0].value != NULL);
    cJSON *text = cJSON_GetObjectItemCaseSensitive(list.items[0].value, "text");
    CHECK(text != NULL && cJSON_IsString(text));
    CHECK(list.has_other_replies == 1);

    wf_unspecced_thread_v2_free(&list);
    CHECK(list.item_count == 0);
    CHECK(list.items == NULL);
    return 0;
}

/* The getSuggestedUsersFor{Discover,Explore,SeeMore} and
 * getSuggestedOnboardingUsers endpoints return an "actors" array of profileView
 * plus a recId/recIdStr recommendation hint. The shared suggested-users parser
 * must extract the actors and ignore the trailing hint fields. */
static int test_parse_suggested_users_with_recid(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *actors = cJSON_CreateArray();
    cJSON *a = cJSON_CreateObject();
    cJSON_AddItemToObject(a, "did", cJSON_CreateString("did:plc:disc1"));
    cJSON_AddItemToObject(a, "handle", cJSON_CreateString("disc1.bsky.social"));
    cJSON_AddItemToObject(a, "displayName", cJSON_CreateString("Discover One"));
    cJSON_AddItemToArray(actors, a);
    cJSON_AddItemToObject(root, "actors", actors);
    cJSON_AddItemToObject(root, "recId", cJSON_CreateNumber(42));
    cJSON_AddItemToObject(root, "recIdStr", cJSON_CreateString("snowflake-42"));

    char *json = to_string(root);
    cJSON_Delete(root);
    CHECK(json != NULL);

    wf_agent_actor_list list = {0};
    wf_status st = wf_unspecced_parse_suggested_users(json, strlen(json), &list);
    free(json);
    CHECK(st == WF_OK);
    CHECK(list.actor_count == 1);
    CHECK(list.actors != NULL);
    CHECK(strcmp(list.actors[0].did, "did:plc:disc1") == 0);
    CHECK(list.actors[0].handle != NULL);
    CHECK(strcmp(list.actors[0].handle, "disc1.bsky.social") == 0);

    wf_agent_actor_list_free(&list);
    return 0;
}

static int test_invalid_args(void) {
    wf_unspecced_trend_list t = {0};
    wf_agent_actor_list a = {0};
    wf_agent_generator_view_list g = {0};
    wf_unspecced_thread_v2 th = {0};

    CHECK(wf_agent_get_trends_typed(NULL, 10, &t) == WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_trends_typed((wf_agent *)1, 26, &t) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_typed(NULL, NULL, 10, &a) == WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_typed((wf_agent *)1, NULL, 51, &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_feeds_typed(NULL, 10, &g) == WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_feeds_typed((wf_agent *)1, 26, &g) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_post_thread_v2_typed(NULL, "at://x", 1, 6, 10, NULL, &th) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_post_thread_v2_typed((wf_agent *)1, NULL, 1, 6, 10, NULL, &th) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_post_thread_other_v2_typed(NULL, "at://x", &th) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_post_thread_other_v2_typed((wf_agent *)1, NULL, &th) ==
          WF_ERR_INVALID_ARG);

    /* Sibling suggested-user feeds reject NULL agent and NULL out. */
    CHECK(wf_agent_get_suggested_users_for_discover_typed(NULL, 10, &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_discover_typed((wf_agent *)1, 51,
                                                          &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_discover_typed((wf_agent *)1, 10, NULL) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_explore_typed(NULL, NULL, 10, &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_explore_typed((wf_agent *)1, "art", 51,
                                                         &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_explore_typed((wf_agent *)1, "art", 10,
                                                         NULL) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_see_more_typed(NULL, NULL, 10, &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_see_more_typed((wf_agent *)1, "art", 51,
                                                          &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_users_for_see_more_typed((wf_agent *)1, "art", 10,
                                                          NULL) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_onboarding_users_typed(NULL, NULL, 10, &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_onboarding_users_typed((wf_agent *)1, NULL, 51,
                                                        &a) ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_get_suggested_onboarding_users_typed((wf_agent *)1, NULL, 10,
                                                        NULL) ==
          WF_ERR_INVALID_ARG);

    CHECK(wf_unspecced_parse_trends(NULL, 0, &t) == WF_ERR_INVALID_ARG);
    CHECK(wf_unspecced_parse_thread_v2(NULL, 0, &th) == WF_ERR_INVALID_ARG);
    return 0;
}

int main(void) {
    if (test_parse_trends()) {
        return 1;
    }
    if (test_parse_suggested_users()) {
        return 1;
    }
    if (test_parse_suggested_feeds()) {
        return 1;
    }
    if (test_parse_thread_v2()) {
        return 1;
    }
    if (test_parse_suggested_users_with_recid()) {
        return 1;
    }
    if (test_invalid_args()) {
        return 1;
    }
    printf("ok: unspecced_trends_typed\n");
    return 0;
}
