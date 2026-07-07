/*
 * test_graph_typed.c — unit tests for the typed actor-list parser and the
 * wired agent wrappers (offline JSON parse + arg validation; no network).
 */

#include "wolfram/graph_typed.h"
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

    wf_agent_free(agent);
    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
