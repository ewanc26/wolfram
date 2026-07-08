/*
 * test_chat_write.c — offline tests for the chat.bsky.* write/query wrappers.
 *
 * No network: we only verify (a) the NULL-arg guards fire and (b) the
 * request bodies built for representative wrappers exactly match the lexicon
 * input schemas.
 */

#include "wolfram/chat_typed.h"
#include "wolfram/agent.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Small helpers to satisfy non-NULL pointer arguments without touching
 * real agent state. Every wrapper guards `!agent` first, so passing a NULL
 * agent must return WF_ERR_INVALID_ARG before any dereference. */

#define RNULL ((wf_agent *)NULL)

static const char *DUMMY_DID = "did:plc:abc";
static const char *DUMMY_DIDS[] = { "did:plc:abc", "did:plc:def" };

static void test_guard_all_wrappers(void) {
    wf_response r = {0};
    wf_chat_convo_list cl = {0};
    wf_chat_convo co = {0};
    wf_chat_message_list ml = {0};
    wf_chat_message msg = {0};

    WF_CHECK(wf_agent_chat_list_convos(RNULL, 0, "c", &cl) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo(RNULL, "c", &co) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_messages(RNULL, "c", 0, "cur", &ml) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_send_message(RNULL, "c", "t", "f", &msg) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_accept_convo(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_leave_convo(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mute_convo(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_unmute_convo(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_delete_message_for_self(RNULL, "c", "m") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_add_reaction(RNULL, "c", "m", "v") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_remove_reaction(RNULL, "c", "m", "v") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_update_read(RNULL, "c", "m") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_update_all_read(RNULL, "accepted") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo_availability(RNULL, DUMMY_DIDS, 2, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo_for_members(RNULL, DUMMY_DIDS, 2, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo_members(RNULL, "c", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_unread_counts(RNULL, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_list_convo_requests(RNULL, 0, "c", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_send_message_batch(RNULL, "[]", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_lock_convo(RNULL, "c", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_unlock_convo(RNULL, "c", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_log(RNULL, 0, "c", &r) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_chat_create_group(RNULL, DUMMY_DIDS, 2, "n", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_edit_group(RNULL, "c", "n", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_add_members(RNULL, "c", DUMMY_DIDS, 2, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_remove_members(RNULL, "c", DUMMY_DIDS, 2, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_request_join(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_withdraw_join_request(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_approve_join_request(RNULL, "c", DUMMY_DID, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_reject_join_request(RNULL, "c", DUMMY_DID, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_list_join_requests(RNULL, "c", 0, "cur", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_update_join_requests_read(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_create_join_link(RNULL, "c", 0, "anyone", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_edit_join_link(RNULL, "c", "code", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_disable_join_link(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_enable_join_link(RNULL, "c") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_join_link_previews(RNULL, DUMMY_DIDS, 2, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_list_mutual_groups(RNULL, "c", 0, "cur", &r) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_chat_get_status(RNULL, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_delete_account(RNULL, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_export_account_data(RNULL, &r) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_chat_mod_get_actor_metadata(RNULL, DUMMY_DID, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mod_get_message_context(RNULL, "m", "c", 0, 0, 0, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mod_get_convo(RNULL, "c", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mod_get_convos(RNULL, DUMMY_DIDS, 2, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mod_get_convo_members(RNULL, "c", 0, "cur", &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mod_update_actor_access(RNULL, DUMMY_DID, 1, "ref", &r) == WF_ERR_INVALID_ARG);
}

static void test_build_accept_convo_body(void) {
    char *json = NULL;
    WF_CHECK(wf_chat_build_accept_convo_body(NULL, &json) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_chat_build_accept_convo_body("c1", NULL) == WF_ERR_INVALID_ARG);

    wf_status s = wf_chat_build_accept_convo_body("c1", &json);
    WF_CHECK(s == WF_OK);
    WF_CHECK(json != NULL);

    cJSON *root = cJSON_Parse(json);
    WF_CHECK(root != NULL);
    WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "convoId") &&
             strcmp(cJSON_GetObjectItemCaseSensitive(root, "convoId")->valuestring,
                    "c1") == 0);
    WF_CHECK(cJSON_GetArraySize(root) == 1);

    cJSON_Delete(root);
    free(json);
}

static void test_build_add_reaction_body(void) {
    char *json = NULL;
    wf_status s = wf_chat_build_add_reaction_body("c1", "m1", "\xe2\x9d\xa4", &json);
    WF_CHECK(s == WF_OK);
    WF_CHECK(json != NULL);

    cJSON *root = cJSON_Parse(json);
    WF_CHECK(root != NULL);
    WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(root, "convoId")->valuestring, "c1") == 0);
    WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(root, "messageId")->valuestring, "m1") == 0);
    WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(root, "value")->valuestring, "\xe2\x9d\xa4") == 0);
    WF_CHECK(cJSON_GetArraySize(root) == 3);

    WF_CHECK(wf_chat_build_add_reaction_body(NULL, "m", "v", &json) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_chat_build_add_reaction_body("c", NULL, "v", &json) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_chat_build_add_reaction_body("c", "m", NULL, &json) == WF_ERR_INVALID_ARG);

    cJSON_Delete(root);
    free(json);
}

static void test_build_create_group_body(void) {
    char *json = NULL;
    const char *members[] = { "did:plc:abc", "did:plc:def" };
    wf_status s = wf_chat_build_create_group_body(members, 2, "Team", &json);
    WF_CHECK(s == WF_OK);
    WF_CHECK(json != NULL);

    cJSON *root = cJSON_Parse(json);
    WF_CHECK(root != NULL);
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "members");
    WF_CHECK(arr && cJSON_IsArray(arr) && cJSON_GetArraySize(arr) == 2);
    WF_CHECK(strcmp(cJSON_GetArrayItem(arr, 0)->valuestring, "did:plc:abc") == 0);
    WF_CHECK(strcmp(cJSON_GetArrayItem(arr, 1)->valuestring, "did:plc:def") == 0);
    WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(root, "name")->valuestring, "Team") == 0);
    WF_CHECK(cJSON_GetArraySize(root) == 2);

    WF_CHECK(wf_chat_build_create_group_body(NULL, 2, "n", &json) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_chat_build_create_group_body(members, 0, "n", &json) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_chat_build_create_group_body(members, 2, NULL, &json) == WF_ERR_INVALID_ARG);

    cJSON_Delete(root);
    free(json);
}

int main(void) {
    test_guard_all_wrappers();
    test_build_accept_convo_body();
    test_build_add_reaction_body();
    test_build_create_group_body();
    WF_TEST_SUMMARY();
}
