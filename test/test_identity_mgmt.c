#include "wolfram/identity.h"
#include "wolfram/server.h"
#include "wolfram/agent.h"
#include "wolfram/atproto_lex.h"
#include "test.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

static wf_xrpc_client *new_client(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);
    return client;
}

/* ---- identity: NULL / short-circuit validation ---- */

static void test_identity_invalid(void) {
    wf_identity_recommended_did_credentials creds = {0};
    wf_identity_sign_plc_operation_input sign_in = {0};
    wf_identity_sign_plc_operation_result sign_out = {0};
    wf_identity_check_handle_input check_in = {0};
    wf_identity_check_handle_result check_out = {0};
    int valid = 0;

    WF_CHECK(wf_identity_get_recommended_did_credentials(NULL, &creds) ==
             WF_ERR_INVALID_ARG);
    wf_xrpc_client *client = new_client();
    WF_CHECK(wf_identity_get_recommended_did_credentials(client, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_identity_request_plc_operation_signature(NULL, "did:plc:x") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_request_plc_operation_signature(client, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_request_plc_operation_signature(client, "") ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_identity_sign_plc_operation(NULL, &sign_in, &sign_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_sign_plc_operation(client, NULL, &sign_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_sign_plc_operation(client, &sign_in, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_identity_submit_plc_operation(NULL, "{}") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_submit_plc_operation(client, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_identity_update_handle(NULL, "h") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_update_handle(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_update_handle(client, "") == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_identity_check_handle(NULL, &check_in, &check_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_check_handle(client, NULL, &check_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_check_handle(client, &check_in, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_check_handle(client, &check_in, &check_out) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_identity_verify_handle(NULL, "h", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_verify_handle(client, NULL, &valid) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_verify_handle(client, "h", NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_verify_handle(client, "", &valid) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_identity_free_safe(void) {
    wf_identity_recommended_did_credentials creds = {0};
    wf_identity_sign_plc_operation_result res = {0};
    wf_identity_recommended_did_credentials_free(&creds);
    wf_identity_recommended_did_credentials_free(NULL);
    wf_identity_sign_plc_operation_result_free(&res);
    wf_identity_sign_plc_operation_result_free(NULL);
}

/* ---- server: NULL / short-circuit validation ---- */

static void test_server_invalid(void) {
    wf_server_create_invite_code_input cic_in = {0};
    wf_server_create_invite_code_result cic_out = {0};
    wf_server_create_invite_codes_input cics_in = {0};
    wf_server_create_invite_codes_result cics_out = {0};
    wf_server_revoke_invite_codes_input rev_in = {0};
    wf_server_deactivate_account_input deact_in = {0};
    wf_server_confirm_email_input conf_in = {0};
    wf_server_request_email_update_result req_out = {0};
    wf_server_update_email_input upd_in = {0};

    wf_xrpc_client *client = new_client();

    WF_CHECK(wf_server_create_invite_code(NULL, &cic_in, &cic_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_invite_code(client, NULL, &cic_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_invite_code(client, &cic_in, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_invite_code(client, &cic_in, &cic_out) ==
             WF_ERR_INVALID_ARG); /* use_count == 0 */

    WF_CHECK(wf_server_create_invite_codes(NULL, &cics_in, &cics_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_invite_codes(client, &cics_in, &cics_out) ==
             WF_ERR_INVALID_ARG); /* code_count == 0 */

    WF_CHECK(wf_server_revoke_invite_codes(NULL, &rev_in) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_revoke_invite_codes(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_revoke_invite_codes(client, &rev_in) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_server_activate_account(NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_deactivate_account(NULL, &deact_in) == WF_ERR_INVALID_ARG);
    /* NULL input is valid (no optional deleteAfter): must not be rejected as
     * an invalid-arg error at the wrapper layer; it proceeds to the transport. */
    WF_CHECK(wf_server_deactivate_account(client, NULL) != WF_ERR_INVALID_ARG);

    WF_CHECK(wf_server_confirm_email(NULL, &conf_in) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_confirm_email(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_confirm_email(client, &conf_in) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_server_request_email_update(NULL, &req_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_request_email_update(client, NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_server_request_email_confirmation(NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_server_update_email(NULL, &upd_in) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_update_email(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_update_email(client, &upd_in) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_server_free_safe(void) {
    wf_server_create_invite_code_result cic = {0};
    wf_server_create_invite_codes_result cics = {0};
    wf_server_create_invite_code_result_free(&cic);
    wf_server_create_invite_code_result_free(NULL);
    wf_server_create_invite_codes_result_free(&cics);
    wf_server_create_invite_codes_result_free(NULL);
}

/* ---- agent: NULL validation (no network) ---- */

static void test_agent_invalid(void) {
    wf_agent *agent = NULL;
    wf_identity_check_handle_input check_in = {0};
    wf_identity_check_handle_result check_out = {0};
    int valid = 0;
    wf_server_revoke_invite_codes_input rev_in = {0};

    WF_CHECK(wf_agent_check_handle(agent, &check_in, &check_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_check_handle(agent, NULL, &check_out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_check_handle(agent, &check_in, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_handle(agent, "h", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_handle(agent, NULL, &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_revoke_invite_codes(agent, &rev_in) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_revoke_invite_codes(agent, NULL) == WF_ERR_INVALID_ARG);
}

/* ---- request construction: encode paths via generated lex wrappers ---- */

static void test_construct_sign_plc_operation(void) {
    wf_lex_com_atproto_identity_sign_plc_operation_main_input in = {0};
    const char *rotation[] = {"did:key:zAlice"};
    const char *aka[] = {"at://alice.example"};
    char *json = NULL;

    in.has_token = 1;
    in.token = "tok123";
    in.has_rotation_keys = 1;
    in.rotation_keys.items = rotation;
    in.rotation_keys.count = 1;
    in.has_also_known_as = 1;
    in.also_known_as.items = aka;
    in.also_known_as.count = 1;
    in.has_verification_methods = 1;
    in.verification_methods.data = "{\"atproto\":\"did:key:zAlice\"}";
    in.verification_methods.length = strlen(in.verification_methods.data);
    in.has_services = 1;
    in.services.data = "{\"atproto_pds\":\"https://pds.example\"}";
    in.services.length = strlen(in.services.data);

    WF_CHECK(wf_lex_com_atproto_identity_sign_plc_operation_main_input_encode_json(
                 &in, &json) == WF_OK);
    WF_CHECK(json != NULL);

    cJSON *root = cJSON_Parse(json);
    WF_CHECK(root != NULL);
    if (root) {
        cJSON *tok = cJSON_GetObjectItemCaseSensitive(root, "token");
        cJSON *rkeys = cJSON_GetObjectItemCaseSensitive(root, "rotationKeys");
        cJSON *aka_j = cJSON_GetObjectItemCaseSensitive(root, "alsoKnownAs");
        cJSON *vm = cJSON_GetObjectItemCaseSensitive(root, "verificationMethods");
        cJSON *svc = cJSON_GetObjectItemCaseSensitive(root, "services");
        WF_CHECK(tok && cJSON_IsString(tok) &&
                 strcmp(tok->valuestring, "tok123") == 0);
        WF_CHECK(rkeys && cJSON_IsArray(rkeys) &&
                 cJSON_GetArraySize(rkeys) == 1);
        WF_CHECK(aka_j && cJSON_IsArray(aka_j) &&
                 cJSON_GetArraySize(aka_j) == 1);
        WF_CHECK(vm && cJSON_IsObject(vm));
        WF_CHECK(svc && cJSON_IsObject(svc));
        cJSON_Delete(root);
    }
    wf_lex_com_atproto_identity_sign_plc_operation_main_json_free(json);
}

static void test_construct_create_invite_code(void) {
    wf_lex_com_atproto_server_create_invite_code_main_input in = {0};
    char *json = NULL;

    in.use_count = 5;
    in.has_for_account = 1;
    in.for_account = "did:plc:abc";

    WF_CHECK(wf_lex_com_atproto_server_create_invite_code_main_input_encode_json(
                 &in, &json) == WF_OK);
    WF_CHECK(json != NULL);

    cJSON *root = cJSON_Parse(json);
    WF_CHECK(root != NULL);
    if (root) {
        cJSON *uc = cJSON_GetObjectItemCaseSensitive(root, "useCount");
        cJSON *fa = cJSON_GetObjectItemCaseSensitive(root, "forAccount");
        WF_CHECK(uc && cJSON_IsNumber(uc) && uc->valueint == 5);
        WF_CHECK(fa && cJSON_IsString(fa) &&
                 strcmp(fa->valuestring, "did:plc:abc") == 0);
        cJSON_Delete(root);
    }
    wf_lex_com_atproto_server_create_invite_code_main_json_free(json);
}

static void test_construct_update_email(void) {
    wf_lex_com_atproto_server_update_email_main_input in = {0};
    char *json = NULL;

    in.email = "new@example.com";
    in.has_token = 1;
    in.token = "tok";
    in.has_email_auth_factor = 1;
    in.email_auth_factor = 1;

    WF_CHECK(wf_lex_com_atproto_server_update_email_main_input_encode_json(
                 &in, &json) == WF_OK);
    WF_CHECK(json != NULL);

    cJSON *root = cJSON_Parse(json);
    WF_CHECK(root != NULL);
    if (root) {
        cJSON *e = cJSON_GetObjectItemCaseSensitive(root, "email");
        cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "token");
        cJSON *af = cJSON_GetObjectItemCaseSensitive(root, "emailAuthFactor");
        WF_CHECK(e && cJSON_IsString(e) &&
                 strcmp(e->valuestring, "new@example.com") == 0);
        WF_CHECK(t && cJSON_IsString(t) && strcmp(t->valuestring, "tok") == 0);
        WF_CHECK(af && cJSON_IsBool(af) && cJSON_IsTrue(af));
        cJSON_Delete(root);
    }
    wf_lex_com_atproto_server_update_email_main_json_free(json);
}

/* ---- JSON parsing: decode representative responses ---- */

static void test_parse_get_recommended_did_credentials(void) {
    const char *json =
        "{\"rotationKeys\":[\"did:key:zA\",\"did:key:zB\"],"
         "\"alsoKnownAs\":[\"at://a.example\",\"at://b.example\"],"
         "\"verificationMethods\":{\"atproto\":\"did:key:zA\"},"
         "\"services\":{\"atproto_pds\":\"https://pds.example\"}}";
    wf_lex_com_atproto_identity_get_recommended_did_credentials_main_output *out =
        NULL;

    WF_CHECK(
        wf_lex_com_atproto_identity_get_recommended_did_credentials_main_output_decode_json(
            json, strlen(json), &out) == WF_OK);
    WF_CHECK(out != NULL);
    if (out) {
        WF_CHECK(out->has_rotation_keys &&
                 out->rotation_keys.count == 2);
        WF_CHECK(out->has_also_known_as &&
                 out->also_known_as.count == 2);
        WF_CHECK(out->has_verification_methods &&
                 out->verification_methods.length > 0);
        WF_CHECK(out->has_services && out->services.length > 0);
        wf_lex_com_atproto_identity_get_recommended_did_credentials_main_output_free(
            out);
    }
}

static void test_parse_create_invite_codes(void) {
    const char *json =
        "{\"codes\":[{\"account\":\"did:plc:abc\","
         "\"codes\":[\"a1b2c3\",\"d4e5f6\"]}]}";
    wf_lex_com_atproto_server_create_invite_codes_main_output *out = NULL;

    WF_CHECK(
        wf_lex_com_atproto_server_create_invite_codes_main_output_decode_json(
            json, strlen(json), &out) == WF_OK);
    WF_CHECK(out != NULL);
    if (out) {
        WF_CHECK(out->codes.count == 1);
        if (out->codes.count == 1) {
            const wf_lex_com_atproto_server_create_invite_codes_account_codes *ac =
                out->codes.items[0];
            WF_CHECK(ac->account &&
                     strcmp(ac->account, "did:plc:abc") == 0);
            WF_CHECK(ac->codes.count == 2);
        }
        wf_lex_com_atproto_server_create_invite_codes_main_output_free(out);
    }
}

static void test_parse_request_email_update(void) {
    const char *json = "{\"tokenRequired\":true}";
    wf_lex_com_atproto_server_request_email_update_main_output *out = NULL;

    WF_CHECK(
        wf_lex_com_atproto_server_request_email_update_main_output_decode_json(
            json, strlen(json), &out) == WF_OK);
    WF_CHECK(out != NULL);
    if (out) {
        WF_CHECK(out->token_required == 1);
        wf_lex_com_atproto_server_request_email_update_main_output_free(out);
    }
}

static void test_parse_sign_plc_operation(void) {
    const char *json =
        "{\"operation\":{\"type\":\"plc_operation\",\"sig\":{}}}";
    wf_lex_com_atproto_identity_sign_plc_operation_main_output *out = NULL;

    WF_CHECK(
        wf_lex_com_atproto_identity_sign_plc_operation_main_output_decode_json(
            json, strlen(json), &out) == WF_OK);
    WF_CHECK(out != NULL);
    if (out) {
        WF_CHECK(out->operation.length > 0);
        wf_lex_com_atproto_identity_sign_plc_operation_main_output_free(out);
    }
}

int main(void) {
    test_identity_invalid();
    test_identity_free_safe();
    test_server_invalid();
    test_server_free_safe();
    test_agent_invalid();
    test_construct_sign_plc_operation();
    test_construct_create_invite_code();
    test_construct_update_email();
    test_parse_get_recommended_did_credentials();
    test_parse_create_invite_codes();
    test_parse_request_email_update();
    test_parse_sign_plc_operation();

    WF_TEST_SUMMARY();
}
