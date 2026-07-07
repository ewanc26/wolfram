/*
 * test_graph_lists.c — validation coverage for graph list helpers.
 *
 * Exercises AT URI parsing plus NULL / malformed argument handling for the
 * new list CRUD and moderation-list wrappers. No network calls are made.
 */

#include "wolfram/agent.h"
#include "wolfram/syntax.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static void test_aturi_parse_valid(void) {
    wf_syntax_aturi parsed = {0};
    WF_CHECK(wf_syntax_aturi_parse(
        "at://did:plc:abc123/app.bsky.graph.list/3abcdefghijkl", &parsed));
    WF_CHECK(parsed.authority && strcmp(parsed.authority, "did:plc:abc123") == 0);
    WF_CHECK(parsed.collection && strcmp(parsed.collection,
                                         "app.bsky.graph.list") == 0);
    WF_CHECK(parsed.record_key && strcmp(parsed.record_key, "3abcdefghijkl") == 0);
    wf_syntax_aturi_free(&parsed);
}

static void test_aturi_parse_invalid(void) {
    wf_syntax_aturi parsed = {0};

    WF_CHECK(!wf_syntax_aturi_parse(NULL, &parsed));
    wf_syntax_aturi_free(&parsed);

    WF_CHECK(!wf_syntax_aturi_parse("not-an-at-uri", &parsed));
    wf_syntax_aturi_free(&parsed);

    WF_CHECK(!wf_syntax_aturi_parse(
        "at://did:plc:abc123/app.bsky.graph.list/3abcdefghijkl/extra", &parsed));
    wf_syntax_aturi_free(&parsed);
}

static void test_create_list_validation(void) {
    wf_agent *agent = wf_agent_new("https://example.com");
    wf_agent_post_result out = {0};
    wf_agent_create_list_params params = {
        .purpose = "app.bsky.graph.defs#modlist",
        .name = "curation list",
    };

    WF_CHECK(agent != NULL);
    WF_CHECK(wf_agent_create_list(NULL, &params, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_create_list(agent, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_create_list(agent, &params, NULL) == WF_ERR_INVALID_ARG);

    params.name = NULL;
    WF_CHECK(wf_agent_create_list(agent, &params, &out) == WF_ERR_INVALID_ARG);
    params.name = "curation list";

    params.purpose = "not-a-purpose";
    WF_CHECK(wf_agent_create_list(agent, &params, &out) == WF_ERR_INVALID_ARG);
    params.purpose = "app.bsky.graph.defs#modlist";

    params.description_facets_json = "not json";
    WF_CHECK(wf_agent_create_list(agent, &params, &out) == WF_ERR_PARSE);

    wf_agent_free(agent);
}

static void test_update_and_delete_list_validation(void) {
    wf_agent *agent = wf_agent_new("https://example.com");
    wf_agent_update_list_params params = {
        .list_uri = "at://did:plc:abc123/app.bsky.graph.list/3abcdefghijkl",
        .name = "updated name",
    };

    WF_CHECK(agent != NULL);
    WF_CHECK(wf_agent_update_list(NULL, &params) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_update_list(agent, NULL) == WF_ERR_INVALID_ARG);

    params.list_uri = NULL;
    WF_CHECK(wf_agent_update_list(agent, &params) == WF_ERR_INVALID_ARG);
    params.list_uri = "at://did:plc:abc123/app.bsky.graph.list/3abcdefghijkl";

    params.name = NULL;
    WF_CHECK(wf_agent_update_list(agent, &params) == WF_ERR_INVALID_ARG);
    params.name = "updated name";

    params.list_uri = "bad-uri";
    WF_CHECK(wf_agent_update_list(agent, &params) == WF_ERR_PARSE);

    WF_CHECK(wf_agent_delete_list(NULL, params.list_uri) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_delete_list(agent, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_delete_list(agent, "bad-uri") == WF_ERR_PARSE);

    wf_agent_free(agent);
}

static void test_list_item_validation(void) {
    wf_agent *agent = wf_agent_new("https://example.com");
    wf_agent_post_result out = {0};
    const char *list_uri = "at://did:plc:abc123/app.bsky.graph.list/3abcdefghijkl";
    const char *subject_did = "did:plc:subject123";

    WF_CHECK(agent != NULL);
    WF_CHECK(wf_agent_add_list_item(NULL, list_uri, subject_did, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_add_list_item(agent, NULL, subject_did, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_add_list_item(agent, list_uri, NULL, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_add_list_item(agent, list_uri, subject_did, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_add_list_item(agent, list_uri, "not-a-did", &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_add_list_item(agent, "bad-uri", subject_did, &out) ==
             WF_ERR_PARSE);

    WF_CHECK(wf_agent_remove_list_item(NULL, list_uri) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_remove_list_item(agent, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_remove_list_item(agent, "bad-uri") == WF_ERR_PARSE);

    wf_agent_free(agent);
}

static void test_mod_list_action_validation(void) {
    wf_agent *agent = wf_agent_new("https://example.com");
    wf_agent_post_result out = {0};
    const char *list_uri = "at://did:plc:abc123/app.bsky.graph.list/3abcdefghijkl";

    WF_CHECK(agent != NULL);
    WF_CHECK(wf_agent_mute_mod_list(NULL, list_uri) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_mute_mod_list(agent, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_mute_mod_list(agent, "bad-uri") == WF_ERR_PARSE);

    WF_CHECK(wf_agent_unmute_mod_list(NULL, list_uri) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unmute_mod_list(agent, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unmute_mod_list(agent, "bad-uri") == WF_ERR_PARSE);

    WF_CHECK(wf_agent_block_mod_list(NULL, list_uri, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_block_mod_list(agent, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_block_mod_list(agent, list_uri, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_block_mod_list(agent, "bad-uri", &out) == WF_ERR_PARSE);

    WF_CHECK(wf_agent_unblock_mod_list(NULL, list_uri) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unblock_mod_list(agent, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unblock_mod_list(agent, "bad-uri") == WF_ERR_PARSE);

    wf_agent_free(agent);
}

int main(void) {
    test_aturi_parse_valid();
    test_aturi_parse_invalid();
    test_create_list_validation();
    test_update_and_delete_list_validation();
    test_list_item_validation();
    test_mod_list_action_validation();
    WF_TEST_SUMMARY();
}
