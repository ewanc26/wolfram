/*
 * test_graph_typed.c — unit tests for the typed actor-list parser and the
 * wired agent wrappers (offline JSON parse + arg validation; no network).
 */

#include "wolfram/graph_typed.h"
#include "wolfram/actor_typed.h"
#include "wolfram/feed_typed.h"
#include "wolfram/thread_typed.h"
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

    if (!fp) {
        return NULL;
    }
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
    if (len_out) {
        *len_out = len;
    }
    return buf;
}

static char *load_fixture(const char *filename, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, filename);
    return read_entire_file(path, len_out);
}

int main(void) {
    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);

    wf_agent_actor_list list = {0};

    /* Invalid-argument handling (parser). */
    WF_CHECK(wf_agent_parse_actors(NULL, 0, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_actors("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    /* Malformed JSON and missing `actors`. */
    WF_CHECK(wf_agent_parse_actors("not json", 8, &list) == WF_ERR_PARSE);
    WF_CHECK(wf_agent_parse_actors("{\"cursor\":\"x\"}", 13, &list) == WF_ERR_PARSE);

    /* Wrapper arg validation. */
    wf_agent_actor_list out = {0};
    WF_CHECK(wf_agent_get_follows_typed(NULL, "did:plc:x", 10, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_follows_typed(agent, NULL, 10, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_follows_typed(agent, "did:plc:x", 10, NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_followers_typed(NULL, "did:plc:x", 10, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_search_actors_typed(agent, NULL, 10, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_reposted_by_typed(agent, NULL, 10, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_reposted_by_typed(agent, "at://x", 10, NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_likes_typed(agent, NULL, 10, NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_likes_typed(agent, "at://x", 10, NULL, NULL) == WF_ERR_INVALID_ARG);

    const char *two_actors[] = { "did:plc:a", "did:plc:b" };
    wf_agent_actor_list plist = {0};
    WF_CHECK(wf_agent_get_profiles_typed(agent, NULL, 2, 10, NULL, &plist) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_profiles_typed(agent, two_actors, 0, 10, NULL, &plist) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_profiles_typed(agent, two_actors, 2, 10, NULL, NULL) == WF_ERR_INVALID_ARG);

    /* Valid fixture. */
    size_t len = 0;
    char *json = load_fixture("follows.json", &len);
    WF_CHECK(json != NULL);

    if (json) {
        wf_status status = wf_agent_parse_actors(json, len, &list);
        WF_CHECK(status == WF_OK);
        WF_CHECK(list.actor_count == 2);
        WF_CHECK(list.cursor && strcmp(list.cursor, "follows-cursor-99") == 0);
        if (list.actor_count == 2) {
            WF_CHECK(list.actors[0].did && strcmp(list.actors[0].did, "did:plc:follower1") == 0);
            WF_CHECK(list.actors[0].handle && strcmp(list.actors[0].handle, "carol.example.com") == 0);
            WF_CHECK(list.actors[0].display_name && strcmp(list.actors[0].display_name, "Carol") == 0);
            WF_CHECK(list.actors[0].avatar && strstr(list.actors[0].avatar, "carol.jpg") != NULL);
            WF_CHECK(list.actors[1].did && strcmp(list.actors[1].did, "did:plc:follower2") == 0);
            WF_CHECK(list.actors[1].avatar == NULL);
        }

        wf_agent_actor_list_free(&list);
        WF_CHECK(list.actors == NULL);
        WF_CHECK(list.actor_count == 0);
        wf_agent_actor_list_free(&list); /* double-free is safe */

        free(json);
    }

    /* likes list parse */
    {
        size_t llen = 0;
        char *ljson = load_fixture("likes.json", &llen);
        WF_CHECK(ljson != NULL);
        if (ljson) {
            wf_agent_like_list likes = {0};
            WF_CHECK(wf_agent_parse_likes(ljson, llen, &likes) == WF_OK);
            WF_CHECK(likes.like_count == 2);
            WF_CHECK(likes.cursor && strcmp(likes.cursor, "likes-cursor-7") == 0);
            if (likes.like_count == 2) {
                WF_CHECK(likes.likes[0].actor.did && strcmp(likes.likes[0].actor.did, "did:plc:liker1") == 0);
                WF_CHECK(likes.likes[0].created_at && strcmp(likes.likes[0].created_at, "2026-07-02T11:00:00Z") == 0);
                WF_CHECK(likes.likes[0].indexed_at && strcmp(likes.likes[0].indexed_at, "2026-07-02T11:00:01Z") == 0);
                WF_CHECK(likes.likes[1].actor.handle && strcmp(likes.likes[1].actor.handle, "frank.example.com") == 0);
            }
            wf_agent_like_list_free(&likes);
            WF_CHECK(likes.likes == NULL);
            wf_agent_like_list_free(&likes);
            free(ljson);
        }
    }

    /* repostedBy list parse (profileView list under "repostedBy") */
    {
        size_t rlen = 0;
        char *rjson = load_fixture("repostedBy.json", &rlen);
        WF_CHECK(rjson != NULL);
        if (rjson) {
            wf_agent_actor_list rb = {0};
            WF_CHECK(wf_agent_parse_reposted_by(rjson, rlen, &rb) == WF_OK);
            WF_CHECK(rb.actor_count == 2);
            WF_CHECK(rb.cursor && strcmp(rb.cursor, "reposted-cursor-3") == 0);
            WF_CHECK(rb.actors[0].did && strcmp(rb.actors[0].did, "did:plc:reposter1") == 0);
            WF_CHECK(rb.actors[1].display_name == NULL);
            wf_agent_actor_list_free(&rb);
            free(rjson);
        }
    }

    /* profiles list parse (profileView list under "profiles") */
    {
        size_t plen = 0;
        char *pjson = load_fixture("profiles.json", &plen);
        WF_CHECK(pjson != NULL);
        if (pjson) {
            wf_agent_actor_list pv = {0};
            WF_CHECK(wf_agent_parse_profile_views(pjson, plen, "profiles", &pv) == WF_OK);
            WF_CHECK(pv.actor_count == 2);
            WF_CHECK(pv.actors[0].avatar && strstr(pv.actors[0].avatar, "ivan.jpg") != NULL);
            wf_agent_actor_list_free(&pv);
            free(pjson);
        }
    }

    wf_agent_free(agent);
    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
