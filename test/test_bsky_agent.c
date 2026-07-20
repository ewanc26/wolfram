/*
 * test_bsky_agent.c — offline input-validation tests for wf_bsky_agent.
 *
 * These tests never touch the network (no login). They assert that every
 * wrapper returns WF_ERR_INVALID_ARG when the bsky agent is NULL or when a
 * required argument is NULL, and that init/free are safe no-ops on a zeroed
 * struct.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/bsky_agent.h"

/* A small driver so we can both verify the return value and report which call
 * failed. Returns 1 on success (got WF_ERR_INVALID_ARG), 0 on failure. */
#define EXPECT_INVALID_ARG(expr)                                        \
    do {                                                                \
        wf_status s = (expr);                                           \
        if (s != WF_ERR_INVALID_ARG) {                                  \
            fprintf(stderr, "FAIL: %s -> %d (expected WF_ERR_INVALID_ARG)\n", \
                    #expr, (int)s);                                     \
            return 0;                                                   \
        }                                                               \
    } while (0)

static int test_null_agent(void) {
    wf_agent_post_result post = {0};
    wf_agent_profile prof = {0};
    wf_agent_feed_list feed = {0};
    wf_agent_notification_list notifs = {0};
    wf_agent_actor_list actors = {0};
    wf_agent_thread thread = {0};
    char *did = NULL;

    EXPECT_INVALID_ARG(wf_bsky_agent_login(NULL, "https://bsky.social", "h", "p"));
    EXPECT_INVALID_ARG(wf_bsky_agent_login_session(NULL, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_logout(NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_post(NULL, "hi", &post));
    EXPECT_INVALID_ARG(wf_agent_reply_refs(NULL, "hi", "at://root", "root-cid",
                                           "at://parent", "parent-cid", &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_profile(NULL, "h", &prof));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_timeline(NULL, 10, NULL, &feed));
    EXPECT_INVALID_ARG(wf_bsky_agent_resolve_handle(NULL, "h", &did));
    EXPECT_INVALID_ARG(wf_bsky_agent_follow(NULL, "h", &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_unfollow(NULL, "at://x"));
    EXPECT_INVALID_ARG(wf_bsky_agent_like(NULL, "u", "c", &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_repost(NULL, "u", "c", &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_mute(NULL, "h"));
    EXPECT_INVALID_ARG(wf_bsky_agent_unmute(NULL, "h"));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_notifications(NULL, 10, NULL, &notifs));
    EXPECT_INVALID_ARG(wf_bsky_agent_search_actors(NULL, "q", 10, NULL, &actors));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_thread(NULL, "at://x", 1, &thread));
    return 1;
}

static int test_null_args(void) {
    wf_bsky_agent b;
    wf_bsky_agent_init(&b);

    wf_agent_post_result post = {0};
    wf_agent_profile prof = {0};
    wf_agent_actor_list actors = {0};
    wf_agent_thread thread = {0};
    char *did = NULL;

    EXPECT_INVALID_ARG(wf_bsky_agent_login(&b, NULL, "h", "p"));
    EXPECT_INVALID_ARG(wf_bsky_agent_login(&b, "s", NULL, "p"));
    EXPECT_INVALID_ARG(wf_bsky_agent_login(&b, "s", "h", NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_login_session(&b, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_post(&b, NULL, &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_post(&b, "hi", NULL));
    EXPECT_INVALID_ARG(wf_agent_reply_refs(b.agent, "hi", NULL, "root-cid",
                                           "at://parent", "parent-cid", &post));
    EXPECT_INVALID_ARG(wf_agent_reply_refs(b.agent, "hi", "at://root", "root-cid",
                                           "at://parent", NULL, &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_profile(&b, NULL, &prof));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_profile(&b, "h", NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_timeline(&b, 10, NULL, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_resolve_handle(&b, NULL, &did));
    EXPECT_INVALID_ARG(wf_bsky_agent_resolve_handle(&b, "h", NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_follow(&b, NULL, &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_follow(&b, "h", NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_unfollow(&b, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_like(&b, NULL, "c", &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_like(&b, "u", NULL, &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_like(&b, "u", "c", NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_repost(&b, NULL, "c", &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_repost(&b, "u", NULL, &post));
    EXPECT_INVALID_ARG(wf_bsky_agent_repost(&b, "u", "c", NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_mute(&b, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_unmute(&b, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_notifications(&b, 10, NULL, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_search_actors(&b, NULL, 10, NULL, &actors));
    EXPECT_INVALID_ARG(wf_bsky_agent_search_actors(&b, "q", 10, NULL, NULL));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_thread(&b, NULL, 1, &thread));
    EXPECT_INVALID_ARG(wf_bsky_agent_get_thread(&b, "at://x", 1, NULL));

    wf_bsky_agent_free(&b);
    return 1;
}

static int test_init_free_idempotent(void) {
    wf_bsky_agent b;
    wf_bsky_agent_init(&b);
    wf_bsky_agent_free(&b);   /* agent pointer NULL -> no-op */
    wf_bsky_agent_free(&b);   /* second free must be safe */
    return 1;
}

static int test_parse_profile_viewer(void) {
    const char *json =
        "{\"did\":\"did:plc:alice\",\"handle\":\"alice.test\","
        "\"avatar\":\"https://cdn.example/alice.png\","
        "\"viewer\":{\"following\":"
        "\"at://did:plc:me/app.bsky.graph.follow/one\"}}";
    wf_agent_profile profile = {0};
    if (wf_agent_parse_profile(json, strlen(json), &profile) != WF_OK ||
        !profile.avatar_cid || !profile.following ||
        strcmp(profile.avatar_cid, "https://cdn.example/alice.png") != 0 ||
        strcmp(profile.following,
               "at://did:plc:me/app.bsky.graph.follow/one") != 0) {
        wf_agent_profile_free(&profile);
        return 0;
    }
    wf_agent_profile_free(&profile);
    return 1;
}

int main(void) {
    if (!test_parse_profile_viewer()) {
        return 1;
    }
    if (!test_null_agent()) {
        return 1;
    }
    if (!test_null_args()) {
        return 1;
    }
    if (!test_init_free_idempotent()) {
        return 1;
    }
    printf("bsky_agent: all offline validation tests passed\n");
    return 0;
}
