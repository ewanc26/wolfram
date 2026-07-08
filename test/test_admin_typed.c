/*
 * test_admin_typed.c — offline unit tests for the com.atproto.admin typed
 * parsers and wrapper argument validation. No network.
 */

#include "wolfram/admin_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* getAccountInfo -> accountView object (not wrapped) */
static const char *k_account_info_json =
    "{\"did\":\"did:plc:aaa\",\"handle\":\"alice.bsky.social\","
    "\"email\":\"alice@example.com\",\"indexedAt\":\"2024-01-01T00:00:00Z\","
    "\"invitesDisabled\":false,"
    "\"deactivatedAt\":\"2024-02-01T00:00:00Z\","
    "\"emailConfirmedAt\":\"2024-01-01T00:00:01Z\"}";

/* searchAccounts -> "accounts" array of accountView + cursor */
static const char *k_search_json =
    "{\"accounts\":["
    "{\"did\":\"did:plc:aaa\",\"handle\":\"alice.bsky.social\","
    "\"email\":\"alice@example.com\"},"
    "{\"did\":\"did:plc:bbb\",\"handle\":\"bob.bsky.social\"}],"
    "\"cursor\":\"c2\"}";

/* getSubjectStatus -> {subject, takedown, deactivated} */
static const char *k_subject_status_json =
    "{\"subject\":{\"$type\":\"com.atproto.admin.defs#repoRef\","
    "\"did\":\"did:plc:aaa\"},"
    "\"takedown\":{\"applied\":true,\"ref\":\"at://did:plc:r/...\"},"
    "\"deactivated\":{\"applied\":false}}";

/* getInviteCodes -> "codes" array of inviteCode + cursor */
static const char *k_invite_codes_json =
    "{\"codes\":["
    "{\"code\":\"hxxp-xxxx-xxxx-xxxx\",\"available\":3,\"disabled\":false,"
    "\"forAccount\":\"did:plc:aaa\",\"createdBy\":\"did:plc:admin\","
    "\"createdAt\":\"2024-01-01T00:00:00Z\",\"uses\":[]},"
    "{\"code\":\"hyyy-yyyy-yyyy-yyyy\",\"available\":0,\"disabled\":true,"
    "\"forAccount\":\"did:plc:bbb\",\"createdBy\":\"did:plc:admin\","
    "\"createdAt\":\"2024-01-02T00:00:00Z\"}],"
    "\"cursor\":\"ic2\"}";

int main(void) {
    /* ---- getAccountInfo (single accountView) ---- */
    {
        wf_admin_account_view out = {0};
        wf_status s = wf_admin_parse_account_view(k_account_info_json,
                                                  strlen(k_account_info_json),
                                                  &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.did && strcmp(out.did, "did:plc:aaa") == 0);
        WF_CHECK(out.handle && strcmp(out.handle, "alice.bsky.social") == 0);
        WF_CHECK(out.email && strcmp(out.email, "alice@example.com") == 0);
        WF_CHECK(out.indexed_at &&
                 strcmp(out.indexed_at, "2024-01-01T00:00:00Z") == 0);
        WF_CHECK(out.has_invites_disabled && out.invites_disabled == false);
        WF_CHECK(out.deactivated_at &&
                 strcmp(out.deactivated_at, "2024-02-01T00:00:00Z") == 0);
        /* unknown field (emailConfirmedAt) kept in the owned extra subtree */
        WF_CHECK(out.extra != NULL);
        if (out.extra) {
            cJSON *ec =
                cJSON_GetObjectItemCaseSensitive(out.extra, "emailConfirmedAt");
            WF_CHECK(cJSON_IsString(ec) && ec->valuestring);
        }
        wf_admin_account_view_free(&out);
        wf_admin_account_view_free(&out); /* freed list must be safe */
    }

    /* ---- searchAccounts (account list + cursor) ---- */
    {
        wf_admin_account_view_list out = {0};
        wf_status s = wf_admin_parse_search_accounts(k_search_json,
                                                     strlen(k_search_json),
                                                     &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.account_count == 2);
        WF_CHECK(out.accounts != NULL);
        WF_CHECK(out.accounts[0].did &&
                 strcmp(out.accounts[0].did, "did:plc:aaa") == 0);
        WF_CHECK(out.accounts[0].email &&
                 strcmp(out.accounts[0].email, "alice@example.com") == 0);
        WF_CHECK(out.accounts[1].handle &&
                 strcmp(out.accounts[1].handle, "bob.bsky.social") == 0);
        WF_CHECK(out.cursor && strcmp(out.cursor, "c2") == 0);
        wf_admin_account_view_list_free(&out);
    }

    /* ---- getAccountInfos (infos list) ---- */
    {
        const char *infos_json =
            "{\"infos\":[{\"did\":\"did:plc:aaa\",\"handle\":\"a.bsky.social\"}],"
            "\"cursor\":\"i1\"}";
        wf_admin_account_view_list out = {0};
        wf_status s = wf_admin_parse_account_infos(infos_json,
                                                   strlen(infos_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.account_count == 1);
        WF_CHECK(out.accounts[0].did &&
                 strcmp(out.accounts[0].did, "did:plc:aaa") == 0);
        wf_admin_account_view_list_free(&out);
    }

    /* ---- getSubjectStatus ---- */
    {
        wf_admin_subject_status out = {0};
        wf_status s = wf_admin_parse_subject_status(k_subject_status_json,
                                                    strlen(k_subject_status_json),
                                                    &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.subject != NULL);
        WF_CHECK(out.did && strcmp(out.did, "did:plc:aaa") == 0);
        WF_CHECK(out.has_takedown && out.takedown_applied == true);
        WF_CHECK(out.takedown_ref &&
                 strcmp(out.takedown_ref, "at://did:plc:r/...") == 0);
        WF_CHECK(out.has_deactivated && out.deactivated_applied == false);
        wf_admin_subject_status_free(&out);
    }

    /* ---- getInviteCodes ---- */
    {
        wf_admin_invite_code_list out = {0};
        wf_status s = wf_admin_parse_invite_codes(k_invite_codes_json,
                                                  strlen(k_invite_codes_json),
                                                  &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.code_count == 2);
        WF_CHECK(out.codes[0].code &&
                 strcmp(out.codes[0].code, "hxxp-xxxx-xxxx-xxxx") == 0);
        WF_CHECK(out.codes[0].has_available && out.codes[0].available == 3);
        WF_CHECK(out.codes[0].has_disabled && out.codes[0].disabled == false);
        WF_CHECK(out.codes[1].has_disabled && out.codes[1].disabled == true);
        WF_CHECK(out.codes[1].available == 0);
        WF_CHECK(out.cursor && strcmp(out.cursor, "ic2") == 0);
        /* uses array kept in owned extra subtree */
        WF_CHECK(out.codes[0].extra != NULL);
        wf_admin_invite_code_list_free(&out);
    }

    /* ---- invalid / NULL input validation ---- */
    {
        wf_admin_account_view a = {0};
        wf_admin_account_view_list l = {0};
        wf_admin_subject_status st = {0};
        wf_admin_invite_code_list ic = {0};

        WF_CHECK(wf_admin_parse_account_view(NULL, 0, &a) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_admin_parse_account_view("", 0, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_admin_parse_account_infos(NULL, 0, &l) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_admin_parse_search_accounts(NULL, 0, &l) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_admin_parse_subject_status(NULL, 0, &st) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_admin_parse_invite_codes(NULL, 0, &ic) ==
                 WF_ERR_INVALID_ARG);

        wf_admin_account_view_free(&a);
        wf_admin_account_view_list_free(&l);
        wf_admin_subject_status_free(&st);
        wf_admin_invite_code_list_free(&ic);
    }

    /* ---- wrapper argument validation (returns before any network use) ---- */
    {
        wf_admin_account_view a = {0};
        wf_admin_account_view_list l = {0};
        wf_admin_subject_status st = {0};
        wf_admin_invite_code_list ic = {0};
        const char *dids[1] = {"did:plc:aaa"};

        WF_CHECK(wf_agent_admin_get_account_info(NULL, "did:plc:aaa", &a) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_get_account_info((wf_agent *)1, NULL, &a) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_get_account_infos(NULL, dids, 1, &l) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_get_account_infos((wf_agent *)1, NULL, 0, &l) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_search_accounts(NULL, "q", 10, NULL, &l) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_get_subject_status(NULL, "did:plc:aaa", &st) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_get_subject_status((wf_agent *)1, NULL, &st) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_get_invite_codes(NULL, NULL, &ic) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_update_subject_status(NULL, "did:plc:aaa", 1, 0,
                                                      NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_update_account_handle(NULL, "did:plc:aaa",
                                                     "a.bsky.social") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_update_account_email(NULL, "did:plc:aaa",
                                                    "a@b.com") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_update_account_password(NULL, "did:plc:aaa",
                                                       "pw") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_update_account_signing_key(
                    NULL, "did:plc:aaa", "did:key:z") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_delete_account(NULL, "did:plc:aaa") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_enable_account_invites(NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_disable_account_invites(NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_disable_invite_codes(NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_send_email(NULL, "did:plc:aaa", "hi", NULL,
                                           NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_admin_send_email((wf_agent *)1, NULL, "hi", NULL,
                                           NULL) == WF_ERR_INVALID_ARG);

        wf_admin_account_view_free(&a);
        wf_admin_account_view_list_free(&l);
        wf_admin_subject_status_free(&st);
        wf_admin_invite_code_list_free(&ic);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
