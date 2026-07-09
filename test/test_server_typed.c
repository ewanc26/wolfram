/*
 * test_server_typed.c — offline unit tests for the com.atproto.server typed
 * parsers and wrapper argument validation. No network.
 */

#include "wolfram/server_typed.h"
#include "wolfram/server.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* getSession sample */
static const char *k_get_session_json =
    "{\"did\":\"did:plc:abc\",\"handle\":\"alice.bsky.social\","
    "\"email\":\"alice@example.com\",\"emailConfirmed\":true,"
    "\"active\":true,\"status\":\"active\","
    "\"didDoc\":{\"@context\":[\"https://www.w3.org/ns/did/v1\"]}}";

/* checkAccountStatus sample */
static const char *k_check_status_json =
    "{\"activated\":true,\"validDid\":true,\"repoCommit\":\"bafy:ccc\","
    "\"repoRev\":\"3\",\"repoBlocks\":1024,\"indexedRecords\":42,"
    "\"privateStateValues\":0,\"expectedBlobs\":3,\"importedBlobs\":1}";

/* getAccountInviteCodes sample */
static const char *k_invite_codes_json =
    "{\"codes\":["
    "{\"code\":\"hxxp-xxxx-xxxx-xxxx\",\"available\":3,\"disabled\":false,"
    "\"forAccount\":\"did:plc:aaa\",\"createdBy\":\"did:plc:admin\","
    "\"createdAt\":\"2024-01-01T00:00:00Z\",\"uses\":[]},"
    "{\"code\":\"hyyy-yyyy-yyyy-yyyy\",\"available\":0,\"disabled\":true,"
    "\"forAccount\":\"did:plc:bbb\",\"createdBy\":\"did:plc:admin\","
    "\"createdAt\":\"2024-01-02T00:00:00Z\"}]}";

/* createSession sample */
static const char *k_create_session_json =
    "{\"accessJwt\":\"acc.jwt\",\"refreshJwt\":\"ref.jwt\","
    "\"handle\":\"alice.bsky.social\",\"did\":\"did:plc:abc\","
    "\"email\":\"alice@example.com\",\"emailConfirmed\":true,"
    "\"active\":true,\"status\":\"active\","
    "\"didDoc\":{\"id\":\"did:plc:abc\"}}";

/* requestEmailUpdate sample */
static const char *k_email_update_json = "{\"tokenRequired\":true}";

/* getServiceAuth / reserveSigningKey sample */
static const char *k_auth_token_json = "{\"token\":\"srv.jwt\"}";

int main(void) {
    /* ---- getSession ---- */
    {
        wf_server_session_info out = {0};
        wf_status s = wf_server_parse_session_info(
            k_get_session_json, strlen(k_get_session_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.did && strcmp(out.did, "did:plc:abc") == 0);
        WF_CHECK(out.handle && strcmp(out.handle, "alice.bsky.social") == 0);
        WF_CHECK(out.has_email && out.email &&
                 strcmp(out.email, "alice@example.com") == 0);
        WF_CHECK(out.has_email_confirmed && out.email_confirmed == true);
        WF_CHECK(out.has_active && out.active == true);
        WF_CHECK(out.has_status && out.status &&
                 strcmp(out.status, "active") == 0);
        WF_CHECK(out.did_doc != NULL);
        if (out.did_doc) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(out.did_doc, "@context");
            WF_CHECK(cJSON_IsArray(id));
        }
        WF_CHECK(out.extra != NULL);
        wf_server_session_info_free(&out);
        wf_server_session_info_free(&out); /* idempotent */
    }

    /* ---- checkAccountStatus ---- */
    {
        wf_server_account_status out = {0};
        wf_status s = wf_server_parse_account_status(
            k_check_status_json, strlen(k_check_status_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.has_activated && out.activated == true);
        WF_CHECK(out.has_valid_did && out.valid_did == true);
        WF_CHECK(out.repo_commit && strcmp(out.repo_commit, "bafy:ccc") == 0);
        WF_CHECK(out.repo_rev && strcmp(out.repo_rev, "3") == 0);
        WF_CHECK(out.has_repo_blocks && out.repo_blocks == 1024);
        WF_CHECK(out.has_indexed_records && out.indexed_records == 42);
        WF_CHECK(out.has_private_state_values &&
                 out.private_state_values == 0);
        WF_CHECK(out.has_expected_blobs && out.expected_blobs == 3);
        WF_CHECK(out.has_imported_blobs && out.imported_blobs == 1);
        wf_server_account_status_free(&out);
    }

    /* ---- getAccountInviteCodes ---- */
    {
        wf_server_invite_code_list out = {0};
        wf_status s = wf_server_parse_invite_codes(
            k_invite_codes_json, strlen(k_invite_codes_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.code_count == 2);
        WF_CHECK(out.codes[0].code &&
                 strcmp(out.codes[0].code, "hxxp-xxxx-xxxx-xxxx") == 0);
        WF_CHECK(out.codes[0].has_available && out.codes[0].available == 3);
        WF_CHECK(out.codes[0].has_disabled && out.codes[0].disabled == false);
        WF_CHECK(out.codes[1].has_disabled && out.codes[1].disabled == true);
        WF_CHECK(out.codes[1].has_available && out.codes[1].available == 0);
        WF_CHECK(out.codes[0].for_account &&
                 strcmp(out.codes[0].for_account, "did:plc:aaa") == 0);
        WF_CHECK(out.codes[0].created_at &&
                 strcmp(out.codes[0].created_at, "2024-01-01T00:00:00Z") == 0);
        WF_CHECK(out.codes[0].extra != NULL);
        wf_server_invite_code_list_free(&out);
    }

    /* ---- createSession ---- */
    {
        wf_server_session_tokens out = {0};
        wf_status s = wf_server_parse_session_tokens(
            k_create_session_json, strlen(k_create_session_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.access_jwt && strcmp(out.access_jwt, "acc.jwt") == 0);
        WF_CHECK(out.refresh_jwt && strcmp(out.refresh_jwt, "ref.jwt") == 0);
        WF_CHECK(out.handle && strcmp(out.handle, "alice.bsky.social") == 0);
        WF_CHECK(out.did && strcmp(out.did, "did:plc:abc") == 0);
        WF_CHECK(out.has_email && out.email &&
                 strcmp(out.email, "alice@example.com") == 0);
        WF_CHECK(out.has_email_confirmed && out.email_confirmed == true);
        WF_CHECK(out.has_active && out.active == true);
        WF_CHECK(out.has_status && out.status &&
                 strcmp(out.status, "active") == 0);
        WF_CHECK(out.did_doc != NULL);
        WF_CHECK(out.extra != NULL);
        wf_server_session_tokens_free(&out);
    }

    /* ---- requestEmailUpdate ---- */
    {
        wf_server_email_update_request out = {0};
        wf_status s = wf_server_parse_email_update(
            k_email_update_json, strlen(k_email_update_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.token_required == true);
        wf_server_email_update_request_free(&out);
    }

    /* ---- auth token (getServiceAuth) ---- */
    {
        wf_server_auth_token out = {0};
        wf_status s = wf_server_parse_auth_token(
            k_auth_token_json, strlen(k_auth_token_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.token && strcmp(out.token, "srv.jwt") == 0);
        wf_server_auth_token_free(&out);
    }

    /* ---- build auth token via cJSON round-trip ---- */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "signingKey", "did:key:z6Mk");
        char *js = cJSON_PrintUnformatted(root);
        wf_server_auth_token out = {0};
        wf_status s = wf_server_parse_auth_token(js, strlen(js), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.token && strcmp(out.token, "did:key:z6Mk") == 0);
        wf_server_auth_token_free(&out);
        free(js);
        cJSON_Delete(root);
    }

    /* ---- invalid / NULL input validation ---- */
    {
        wf_server_session_info si = {0};
        wf_server_account_status as = {0};
        wf_server_invite_code_list ic = {0};
        wf_server_auth_token at = {0};
        wf_server_session_tokens st = {0};
        wf_server_email_update_request eu = {0};

        WF_CHECK(wf_server_parse_session_info(NULL, 0, &si) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_parse_session_info("", 0, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_parse_account_status(NULL, 0, &as) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_parse_invite_codes(NULL, 0, &ic) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_parse_auth_token(NULL, 0, &at) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_parse_session_tokens(NULL, 0, &st) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_parse_email_update(NULL, 0, &eu) ==
                 WF_ERR_INVALID_ARG);

        wf_server_session_info_free(&si);
        wf_server_account_status_free(&as);
        wf_server_invite_code_list_free(&ic);
        wf_server_auth_token_free(&at);
        wf_server_session_tokens_free(&st);
        wf_server_email_update_request_free(&eu);
    }

    /* ---- wrapper argument validation (returns before any network use) ---- */
    {
        wf_server_session_info si = {0};
        wf_server_account_status as = {0};
        wf_server_invite_code_list ic = {0};
        wf_server_auth_token at = {0};
        wf_server_session_tokens st = {0};
        wf_server_email_update_request eu = {0};

        WF_CHECK(wf_agent_get_session_typed(NULL, &si) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_session_typed((wf_agent *)1, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_service_auth_typed(NULL, "aud", 0, NULL, &at) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_service_auth_typed((wf_agent *)1, NULL, 0, NULL,
                                                 &at) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_service_auth_typed((wf_agent *)1, "aud", 0, NULL,
                                                 NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_check_account_status_typed(NULL, &as) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_check_account_status_typed((wf_agent *)1, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_account_invite_codes_typed(NULL, 0, 0, &ic) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_account_invite_codes_typed((wf_agent *)1, 0, 0,
                                                         NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_reserve_signing_key_typed(NULL, NULL, &at) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_reserve_signing_key_typed((wf_agent *)1, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_account_delete_typed(NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_email_update_typed(NULL, &eu) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_email_update_typed((wf_agent *)1, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_email_confirmation_typed(NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_password_reset_typed(NULL, "a@b.com") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_password_reset_typed((wf_agent *)1, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_session_typed(NULL, "id", "pw", NULL, &st) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_session_typed((wf_agent *)1, NULL, "pw", NULL,
                                               &st) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_session_typed((wf_agent *)1, "id", NULL, NULL,
                                               &st) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_session_typed((wf_agent *)1, "id", "pw", NULL,
                                               NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_refresh_session_typed(NULL, &st) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_refresh_session_typed((wf_agent *)1, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_server_session_info_free(&si);
        wf_server_account_status_free(&as);
        wf_server_invite_code_list_free(&ic);
        wf_server_auth_token_free(&at);
        wf_server_session_tokens_free(&st);
        wf_server_email_update_request_free(&eu);
    }

    /* ---- new agent wrapper NULL validation (NULL agent, so the wrappers
       return before touching a client). ---- */
    {
        wf_server_description desc = {0};
        wf_server_create_account_result car = {0};
        wf_server_create_account_input cain = {0};
        wf_server_app_password ap = {0};
        wf_server_app_password_list apl = {0};
        wf_server_create_invite_code_result cic = {0};
        wf_server_create_invite_codes_result cics = {0};

        WF_CHECK(wf_agent_describe_server_typed(NULL, &desc) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_describe_server_typed(NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_create_account_typed(NULL, &cain, &car) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_account_typed(NULL, NULL, &car) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_account_typed(NULL, &cain, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_create_app_password_typed(NULL, "n", 0, &ap) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_app_password_typed(NULL, NULL, 0, &ap) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_app_password_typed(NULL, "n", 0, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_list_app_passwords_typed(NULL, &apl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_app_passwords_typed(NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_revoke_app_password_typed(NULL, "n") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_revoke_app_password_typed(NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_delete_session_typed(NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_activate_account_typed(NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_deactivate_account_typed(NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_confirm_email_typed(NULL, "e", "t") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_confirm_email_typed(NULL, NULL, "t") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_confirm_email_typed(NULL, "e", NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_reset_password_typed(NULL, "t", "p") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_reset_password_typed(NULL, NULL, "p") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_reset_password_typed(NULL, "t", NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_update_email_typed(NULL, "e", "t", 0) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_update_email_typed(NULL, NULL, "t", 0) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_create_invite_code_typed(NULL, 1, NULL, &cic) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_invite_code_typed(NULL, 1, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_create_invite_codes_typed(NULL, 1, 1, NULL, 0,
                                                   &cics) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_create_invite_codes_typed(NULL, 1, 1, NULL, 0,
                                                     NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_server_describe_free(&desc);
        wf_server_create_account_result_free(&car);
        wf_server_app_password_free(&ap);
        wf_server_app_password_list_free(&apl);
        wf_server_create_invite_code_result_free(&cic);
        wf_server_create_invite_codes_result_free(&cics);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
