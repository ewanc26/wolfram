/*
 * test_moderation_typed.c — offline tests for the moderation/social-graph
 * typed wrappers in moderation_typed.c.
 *
 * Covers argument validation (NULL agent/actor/out), malformed JSON, missing
 * arrays, and parsing of the blocks/mutes/knownFollowers fixtures asserting
 * actor_count / cursor / field values. The shared `wf_agent_parse_profile_views`
 * parser is exercised directly against the fixtures; the wrappers themselves
 * are exercised for their NULL-argument guards (network calls are not run here).
 */

#include "wolfram/moderation_typed.h"
#include "wolfram/actor_typed.h"

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

static char *load_fixture(const char *filename, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, filename);
    return read_entire_file(path, len_out);
}

static void assert_actor(wf_agent_actor_list *list, size_t idx,
                         const char *did, const char *handle,
                         const char *display_name) {
    WF_CHECK(idx < list->actor_count);
    if (idx >= list->actor_count) return;
    wf_agent_profile_view *p = &list->actors[idx];
    WF_CHECK(p->did && strcmp(p->did, did) == 0);
    WF_CHECK(p->handle && strcmp(p->handle, handle) == 0);
    WF_CHECK(p->display_name && strcmp(p->display_name, display_name) == 0);
}

int main(void) {
    /* ---- wrapper argument validation (no network) --------------------- */
    wf_agent_actor_list out = {0};
    WF_CHECK(wf_agent_get_blocks_typed(NULL, 10, NULL, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_blocks_typed((wf_agent *)0x1, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_mutes_typed(NULL, 10, NULL, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_mutes_typed((wf_agent *)0x1, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_known_followers_typed(NULL, "a", 10, NULL, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_known_followers_typed((wf_agent *)0x1, NULL, 10, NULL,
                                                &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_known_followers_typed((wf_agent *)0x1, "a", 10, NULL,
                                                NULL) == WF_ERR_INVALID_ARG);

    /* ---- malformed JSON ----------------------------------------------- */
    wf_agent_actor_list bad = {0};
    WF_CHECK(wf_agent_parse_profile_views("not json{", 10, "blocks", &bad) ==
             WF_ERR_PARSE);
    wf_agent_actor_list_free(&bad);

    /* ---- valid empty object (no array) --------------------------------- */
    wf_agent_actor_list empty = {0};
    WF_CHECK(wf_agent_parse_profile_views("{}", 2, "blocks", &empty) ==
             WF_ERR_PARSE);
    wf_agent_actor_list_free(&empty);

    /* ---- parser NULL guards ------------------------------------------- */
    WF_CHECK(wf_agent_parse_profile_views(NULL, 0, "blocks", &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_profile_views("{}", 2, NULL, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_profile_views("{}", 2, "blocks", NULL) ==
             WF_ERR_INVALID_ARG);

    /* ---- blocks fixture ----------------------------------------------- */
    {
        size_t len = 0;
        char *json = load_fixture("blocks.json", &len);
        WF_CHECK(json != NULL);
        if (json) {
            wf_agent_actor_list list = {0};
            WF_CHECK(wf_agent_parse_profile_views(json, len, "blocks", &list) ==
                     WF_OK);
            WF_CHECK(list.actor_count == 2);
            WF_CHECK(list.cursor && strcmp(list.cursor, "blocks-cursor-123") == 0);
            assert_actor(&list, 0, "did:plc:blocksalice", "alice.bsky.social",
                         "Alice");
            assert_actor(&list, 1, "did:plc:blocksbob", "bob.bsky.social",
                         "Bob");
            wf_agent_actor_list_free(&list);
            free(json);
        }
    }

    /* ---- mutes fixture ------------------------------------------------- */
    {
        size_t len = 0;
        char *json = load_fixture("mutes.json", &len);
        WF_CHECK(json != NULL);
        if (json) {
            wf_agent_actor_list list = {0};
            WF_CHECK(wf_agent_parse_profile_views(json, len, "mutes", &list) ==
                     WF_OK);
            WF_CHECK(list.actor_count == 2);
            WF_CHECK(list.cursor && strcmp(list.cursor, "mutes-cursor-456") == 0);
            assert_actor(&list, 0, "did:plc:mutescarol", "carol.bsky.social",
                         "Carol");
            assert_actor(&list, 1, "did:plc:mutesdave", "dave.bsky.social",
                         "Dave");
            wf_agent_actor_list_free(&list);
            free(json);
        }
    }

    /* ---- knownFollowers fixture --------------------------------------- */
    {
        size_t len = 0;
        char *json = load_fixture("knownFollowers.json", &len);
        WF_CHECK(json != NULL);
        if (json) {
            wf_agent_actor_list list = {0};
            WF_CHECK(wf_agent_parse_profile_views(json, len, "followers",
                                                  &list) == WF_OK);
            WF_CHECK(list.actor_count == 2);
            WF_CHECK(list.cursor && strcmp(list.cursor, "kf-cursor-789") == 0);
            assert_actor(&list, 0, "did:plc:kfeva", "eve.bsky.social", "Eve");
            assert_actor(&list, 1, "did:plc:kffrank", "frank.bsky.social",
                         "Frank");
            wf_agent_actor_list_free(&list);
            free(json);
        }
    }

    WF_TEST_SUMMARY();
}
