/*
 * test_graph_social_typed.c — offline unit tests for the typed mutes / blocks
 * / lists / starter-packs parsers and agent wrappers. No network: arg
 * validation is exercised (returns before any transport) and parsers run
 * against locally-built JSON fixtures.
 */

#include "wolfram/graph_social_typed.h"
#include "wolfram/actor_typed.h"
#include "wolfram/list_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

/* Render a cJSON tree to a freshly-allocated NUL-terminated string. */
static char *json_string(cJSON *root, size_t *len_out) {
    char *s = cJSON_PrintUnformatted(root);
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    if (len_out) {
        *len_out = len;
    }
    return s;
}

static cJSON *make_profile_view(const char *did, const char *handle,
                                const char *name, const char *avatar) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "did", did);
    cJSON_AddStringToObject(p, "handle", handle);
    if (name) {
        cJSON_AddStringToObject(p, "displayName", name);
    }
    if (avatar) {
        cJSON_AddStringToObject(p, "avatar", avatar);
    }
    return p;
}

static cJSON *make_list_view(const char *uri, const char *name,
                             const char *purpose) {
    cJSON *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "uri", uri);
    cJSON_AddStringToObject(v, "cid", "bafyreindexedcid");
    cJSON_AddItemToObject(v, "creator",
                          make_profile_view("did:plc:creator",
                                            "creator.example.com", "Creator",
                                            "https://cdn/creator.jpg"));
    cJSON_AddStringToObject(v, "name", name);
    cJSON_AddStringToObject(v, "purpose", purpose);
    cJSON_AddStringToObject(v, "description", "a list");
    cJSON_AddStringToObject(v, "indexedAt", "2026-07-09T00:00:00Z");
    cJSON_AddNumberToObject(v, "listItemCount", 3);
    return v;
}

static cJSON *make_starter_pack_view(const char *uri, const char *name) {
    cJSON *sp = cJSON_CreateObject();
    cJSON_AddStringToObject(sp, "uri", uri);
    cJSON_AddStringToObject(sp, "cid", "bafyrestartercid");
    cJSON *record = cJSON_CreateObject();
    cJSON_AddStringToObject(record, "name", name);
    cJSON_AddStringToObject(record, "description", "a starter pack");
    cJSON_AddStringToObject(record, "createdAt", "2026-07-08T12:00:00Z");
    cJSON_AddItemToObject(sp, "record", record);
    cJSON_AddItemToObject(sp, "creator",
                          make_profile_view("did:plc:maker", "maker.example.com",
                                            "Maker", "https://cdn/maker.jpg"));
    cJSON_AddNumberToObject(sp, "listItemCount", 5);
    cJSON_AddNumberToObject(sp, "joinedAllTimeCount", 42);
    cJSON_AddStringToObject(sp, "indexedAt", "2026-07-09T01:00:00Z");
    return sp;
}

static cJSON *make_list_item_view(const char *uri, const char *subject_did,
                                  const char *subject_handle) {
    cJSON *li = cJSON_CreateObject();
    cJSON_AddStringToObject(li, "uri", uri);
    cJSON_AddItemToObject(
        li, "subject",
        make_profile_view(subject_did, subject_handle, "Member", NULL));
    return li;
}

int main(void) {
    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);

    /* ---- getLists (list_typed: wf_agent_list_view_list) ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *lists = cJSON_AddArrayToObject(root, "lists");
        cJSON_AddItemToArray(lists, make_list_view("at://x/list/1", "List One",
                                                   "modlist"));
        cJSON_AddItemToArray(lists, make_list_view("at://x/list/2", "List Two",
                                                   "curatelist"));
        cJSON_AddStringToObject(root, "cursor", "lists-cursor-1");
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_agent_list_view_list lvl = {0};
        WF_CHECK(wf_agent_parse_lists(json, len, &lvl) == WF_OK);
        WF_CHECK(lvl.list_count == 2);
        WF_CHECK(lvl.cursor && strcmp(lvl.cursor, "lists-cursor-1") == 0);
        if (lvl.list_count == 2) {
            WF_CHECK(lvl.lists[0].uri &&
                     strcmp(lvl.lists[0].uri, "at://x/list/1") == 0);
            WF_CHECK(lvl.lists[0].name &&
                     strcmp(lvl.lists[0].name, "List One") == 0);
            WF_CHECK(lvl.lists[1].name &&
                     strcmp(lvl.lists[1].name, "List Two") == 0);
        }
        wf_agent_list_view_list_free(&lvl);
        free(json);
    }

    /* ---- getListMutes / getListBlocks (wf_graph_list_view_list) ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *lists = cJSON_AddArrayToObject(root, "lists");
        cJSON_AddItemToArray(lists, make_list_view("at://x/mute/1", "Muted Mods",
                                                   "modlist"));
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_graph_list_view_list gv = {0};
        WF_CHECK(wf_graph_parse_list_views(json, len, &gv) == WF_OK);
        WF_CHECK(gv.list_count == 1);
        if (gv.list_count == 1) {
            WF_CHECK(gv.lists[0].uri &&
                     strcmp(gv.lists[0].uri, "at://x/mute/1") == 0);
            WF_CHECK(gv.lists[0].name &&
                     strcmp(gv.lists[0].name, "Muted Mods") == 0);
            WF_CHECK(gv.lists[0].creator.did &&
                     strcmp(gv.lists[0].creator.did, "did:plc:creator") == 0);
            WF_CHECK(gv.lists[0].has_list_item_count &&
                     gv.lists[0].list_item_count == 3);
            WF_CHECK(gv.lists[0].purpose &&
                     strcmp(gv.lists[0].purpose, "modlist") == 0);
        }
        wf_graph_list_view_list_free(&gv);
        free(json);
    }

    /* ---- getMutes (wf_agent_actor_list) ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *mutes = cJSON_AddArrayToObject(root, "mutes");
        cJSON_AddItemToArray(mutes, make_profile_view("did:plc:m1",
                                                      "muted1.example.com",
                                                      "Muted One", NULL));
        cJSON_AddItemToArray(mutes, make_profile_view("did:plc:m2",
                                                      "muted2.example.com",
                                                      "Muted Two",
                                                      "https://cdn/m2.jpg"));
        cJSON_AddStringToObject(root, "cursor", "mutes-cursor-9");
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_agent_actor_list al = {0};
        WF_CHECK(wf_agent_parse_profile_views(json, len, "mutes", &al) == WF_OK);
        WF_CHECK(al.actor_count == 2);
        WF_CHECK(al.cursor && strcmp(al.cursor, "mutes-cursor-9") == 0);
        if (al.actor_count == 2) {
            WF_CHECK(al.actors[0].did &&
                     strcmp(al.actors[0].did, "did:plc:m1") == 0);
            WF_CHECK(al.actors[1].avatar &&
                     strstr(al.actors[1].avatar, "m2.jpg") != NULL);
        }
        wf_agent_actor_list_free(&al);
        free(json);
    }

    /* ---- getBlocks (wf_agent_actor_list) ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *blocks = cJSON_AddArrayToObject(root, "blocks");
        cJSON_AddItemToArray(blocks, make_profile_view("did:plc:b1",
                                                       "blocked1.example.com",
                                                       "Blocked One", NULL));
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_agent_actor_list al = {0};
        WF_CHECK(wf_agent_parse_profile_views(json, len, "blocks", &al) == WF_OK);
        WF_CHECK(al.actor_count == 1);
        if (al.actor_count == 1) {
            WF_CHECK(al.actors[0].did &&
                     strcmp(al.actors[0].did, "did:plc:b1") == 0);
        }
        wf_agent_actor_list_free(&al);
        free(json);
    }

    /* ---- getSuggestedFollowsByActor (wf_agent_actor_list "suggestions") ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *sugg = cJSON_AddArrayToObject(root, "suggestions");
        cJSON_AddItemToArray(sugg, make_profile_view("did:plc:s1",
                                                     "suggest1.example.com",
                                                     "Suggested", NULL));
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_agent_actor_list al = {0};
        WF_CHECK(wf_agent_parse_profile_views(json, len, "suggestions",
                                              &al) == WF_OK);
        WF_CHECK(al.actor_count == 1);
        if (al.actor_count == 1) {
            WF_CHECK(al.actors[0].did &&
                     strcmp(al.actors[0].did, "did:plc:s1") == 0);
        }
        wf_agent_actor_list_free(&al);
        free(json);
    }

    /* ---- getStarterPacks (wf_graph_starter_pack_view_list) ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *packs = cJSON_AddArrayToObject(root, "starterPacks");
        cJSON_AddItemToArray(packs, make_starter_pack_view("at://x/sp/1",
                                                           "Starter Alpha"));
        cJSON_AddItemToArray(packs, make_starter_pack_view("at://x/sp/2",
                                                           "Starter Beta"));
        cJSON_AddStringToObject(root, "cursor", "sp-cursor-3");
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_graph_starter_pack_view_list spl = {0};
        WF_CHECK(wf_graph_parse_starter_pack_views(json, len, &spl) == WF_OK);
        WF_CHECK(spl.pack_count == 2);
        WF_CHECK(spl.s_cursor && strcmp(spl.s_cursor, "sp-cursor-3") == 0);
        if (spl.pack_count == 2) {
            WF_CHECK(spl.packs[0].uri &&
                     strcmp(spl.packs[0].uri, "at://x/sp/1") == 0);
            WF_CHECK(spl.packs[0].name &&
                     strcmp(spl.packs[0].name, "Starter Alpha") == 0);
            WF_CHECK(spl.packs[0].creator.did &&
                     strcmp(spl.packs[0].creator.did, "did:plc:maker") == 0);
            WF_CHECK(spl.packs[0].has_list_item_count &&
                     spl.packs[0].list_item_count == 5);
            WF_CHECK(spl.packs[0].has_joined_all_time_count &&
                     spl.packs[0].joined_all_time_count == 42);
        }
        wf_graph_starter_pack_view_list_free(&spl);
        free(json);
    }

    /* ---- getListsWithMembership (wf_graph_list_membership_list) ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *arr = cJSON_AddArrayToObject(root, "listsWithMembership");
        cJSON *e0 = cJSON_CreateObject();
        cJSON_AddItemToObject(e0, "list",
                              make_list_view("at://x/lwm/1", "Joined List",
                                             "curatelist"));
        cJSON_AddItemToObject(
            e0, "listItem",
            make_list_item_view("at://x/lwm/1/item/9", "did:plc:me",
                                 "me.example.com"));
        cJSON_AddItemToArray(arr, e0);
        cJSON *e1 = cJSON_CreateObject();
        cJSON_AddItemToObject(e1, "list",
                              make_list_view("at://x/lwm/2", "Other List",
                                             "modlist"));
        cJSON_AddItemToArray(arr, e1);
        cJSON_AddStringToObject(root, "cursor", "lwm-cursor-7");
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_graph_list_membership_list lm = {0};
        WF_CHECK(wf_graph_parse_list_memberships(json, len, &lm) == WF_OK);
        WF_CHECK(lm.membership_count == 2);
        WF_CHECK(lm.cursor && strcmp(lm.cursor, "lwm-cursor-7") == 0);
        if (lm.membership_count == 2) {
            WF_CHECK(lm.memberships[0].list.uri &&
                     strcmp(lm.memberships[0].list.uri,
                            "at://x/lwm/1") == 0);
            WF_CHECK(lm.memberships[0].has_list_item &&
                     lm.memberships[0].list_item.uri &&
                     strcmp(lm.memberships[0].list_item.uri,
                            "at://x/lwm/1/item/9") == 0);
            WF_CHECK(lm.memberships[0].list_item.subject.did &&
                     strcmp(lm.memberships[0].list_item.subject.did,
                            "did:plc:me") == 0);
            WF_CHECK(!lm.memberships[1].has_list_item);
            WF_CHECK(lm.memberships[1].list.name &&
                     strcmp(lm.memberships[1].list.name, "Other List") == 0);
        }
        wf_graph_list_membership_list_free(&lm);
        free(json);
    }

    /* ---- getStarterPacksWithMembership
     * (wf_graph_starter_pack_membership_list) ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *arr = cJSON_AddArrayToObject(root, "starterPacksWithMembership");
        cJSON *e0 = cJSON_CreateObject();
        cJSON_AddItemToObject(
            e0, "starterPack",
            make_starter_pack_view("at://x/spwm/1", "Pack Alpha"));
        cJSON_AddItemToObject(
            e0, "listItem",
            make_list_item_view("at://x/spwm/1/item/3", "did:plc:mem",
                                 "mem.example.com"));
        cJSON_AddItemToArray(arr, e0);
        size_t len = 0;
        char *json = json_string(root, &len);
        cJSON_Delete(root);
        WF_CHECK(json != NULL);

        wf_graph_starter_pack_membership_list sm = {0};
        WF_CHECK(wf_graph_parse_starter_pack_memberships(json, len, &sm) ==
                 WF_OK);
        WF_CHECK(sm.membership_count == 1);
        if (sm.membership_count == 1) {
            WF_CHECK(sm.memberships[0].starter_pack.uri &&
                     strcmp(sm.memberships[0].starter_pack.uri,
                            "at://x/spwm/1") == 0);
            WF_CHECK(sm.memberships[0].starter_pack.name &&
                     strcmp(sm.memberships[0].starter_pack.name,
                            "Pack Alpha") == 0);
            WF_CHECK(sm.memberships[0].has_list_item &&
                     sm.memberships[0].list_item.subject.did &&
                     strcmp(sm.memberships[0].list_item.subject.did,
                            "did:plc:mem") == 0);
        }
        wf_graph_starter_pack_membership_list_free(&sm);
        free(json);
    }

    /* ---- parser arg validation ---- */
    {
        wf_graph_list_view_list gv = {0};
        wf_graph_starter_pack_view_list spl = {0};
        WF_CHECK(wf_graph_parse_list_views(NULL, 0, &gv) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_graph_parse_list_views("{}", 2, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_graph_parse_list_views("not json", 8, &gv) == WF_ERR_PARSE);
        WF_CHECK(wf_graph_parse_starter_pack_views(NULL, 0, &spl) ==
                 WF_ERR_INVALID_ARG);
        wf_graph_list_view_list_free(&gv);
        wf_graph_starter_pack_view_list_free(&spl);
    }

    /* ---- agent wrapper arg validation (NULL agent / missing required) ---- */
    {
        wf_agent_actor_list al = {0};
        wf_graph_list_view_list gv = {0};
        wf_graph_starter_pack_view_list spl = {0};
        wf_graph_starter_pack_view spv = {0};
        wf_graph_list_membership_list lml = {0};
        wf_graph_starter_pack_membership_list spml = {0};

        WF_CHECK(wf_graph_parse_list_memberships(NULL, 0, &lml) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_graph_parse_starter_pack_memberships(NULL, 0, &spml) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_get_lists_with_membership_typed(NULL, "did:plc:x", 10,
                                                         NULL, &lml) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_lists_with_membership_typed(agent, NULL, 10, NULL,
                                                         &lml) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_lists_with_membership_typed(agent, "not a did",
                                                         10, NULL, &lml) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_lists_with_membership_typed(agent, "did:plc:x", 10,
                                                         NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_starter_packs_with_membership_typed(
                     NULL, "did:plc:x", 10, NULL, &spml) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_starter_packs_with_membership_typed(
                     agent, NULL, 10, NULL, &spml) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_starter_packs_with_membership_typed(
                     agent, "bad", 10, NULL, &spml) == WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_get_list_mutes_typed(NULL, 10, NULL, &gv) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_list_blocks_typed(agent, 10, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_actor_lists_typed(agent, "did:plc:x", 10, NULL,
                                                &gv) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_actor_lists_typed(agent, NULL, 10, NULL, &gv) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_starter_packs_typed(agent, NULL, 0, &spl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_starter_packs_typed(NULL, (const char *const *)"x",
                                                 1, &spl) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_actor_starter_packs_typed(agent, NULL, 10, NULL,
                                                        &spl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_starter_pack_typed(agent, NULL, &spv) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_starter_pack_typed(NULL, "at://x/sp", &spv) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_suggested_follows_by_actor_typed(agent, NULL,
                                                               &al) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_mute_actor_typed(NULL, "did:plc:x") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_mute_actor_typed(agent, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor_typed(agent, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_mute_actor_list_typed(agent, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor_list_typed(NULL, "at://x/list") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_block_actor_typed(agent, "did:plc:x") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unblock_actor_typed(agent, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_block_actor_list_typed(agent, "at://x/list") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unblock_actor_list_typed(NULL, "at://x/list") ==
                 WF_ERR_INVALID_ARG);

        wf_agent_actor_list_free(&al);
        wf_graph_list_view_list_free(&gv);
        wf_graph_starter_pack_view_list_free(&spl);
        wf_graph_starter_pack_view_free(&spv);
        wf_graph_list_membership_list_free(&lml);
        wf_graph_starter_pack_membership_list_free(&spml);
    }

    wf_agent_free(agent);
    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
