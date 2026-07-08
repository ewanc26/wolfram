/*
 * identity_rotate.c — demonstrate a DID PLC handle rotation end to end.
 *
 * Exercises the generated com.atproto.identity PLC operation wrappers plus
 * the plc.h build helper:
 *   1. Log in (session.h) to obtain an authenticated XRPC client.
 *   2. Fetch the account's recommended DID credentials.
 *   3. Build the operation diff (new alsoKnownAs) with wf_plc_operation_build.
 *   4. Request a PLC operation signature (requestPlcOperationSignature).
 *   5. Sign the operation server-side (signPlcOperation) when a token is given.
 *   6. Submit the signed operation (submitPlcOperation) when a token is given.
 *
 * A signing token is emailed by the PLC directory during step 4; supply it as
 * the optional final argument to run the full sign + submit flow.
 *
 * Usage:
 *   identity_rotate <service-url> <handle> <password> <new-handle> [token]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/atproto_lex.h"
#include "wolfram/identity.h"
#include "wolfram/plc.h"
#include "wolfram/session.h"
#include "wolfram/xrpc.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> <password> <new-handle> [token]\n",
                argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];
    const char *new_handle = argv[4];
    const char *token = (argc > 5) ? argv[5] : NULL;

    wf_session *session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "failed to create session\n");
        return 1;
    }

    wf_status status = wf_session_login(session, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    printf("Logged in as %s (%s)\n",
           session->data.handle ? session->data.handle : "",
           session->data.did ? session->data.did : "");

    wf_identity_recommended_did_credentials creds = {0};
    status = wf_identity_get_recommended_did_credentials(session->client, &creds);
    if (status != WF_OK) {
        fprintf(stderr, "getRecommendedDidCredentials failed: %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    char *aka_buf = malloc(strlen(new_handle) + 6);
    if (!aka_buf) {
        fprintf(stderr, "alloc failed\n");
        wf_identity_recommended_did_credentials_free(&creds);
        wf_session_free(session);
        return 1;
    }
    snprintf(aka_buf, strlen(new_handle) + 6, "at://%s", new_handle);
    const char *aka[1] = { aka_buf };

    wf_plc_operation_update update = {0};
    update.rotation_keys = (const char *const *)creds.rotation_keys;
    update.rotation_keys_count = creds.rotation_keys_count;
    update.also_known_as = aka;
    update.also_known_as_count = 1;
    update.verification_methods_json = creds.verification_methods;
    update.services_json = creds.services;

    char *op_json = NULL;
    status = wf_plc_operation_build(&update, &op_json);
    wf_identity_recommended_did_credentials_free(&creds);
    if (status != WF_OK || !op_json) {
        fprintf(stderr, "plc_operation_build failed: %d\n", (int)status);
        free(aka_buf);
        wf_session_free(session);
        return 1;
    }
    printf("Built operation:\n%s\n", op_json);

    status = wf_identity_request_plc_operation_signature(session->client,
                                                        session->data.did);
    if (status != WF_OK) {
        fprintf(stderr, "requestPlcOperationSignature failed: %d\n", (int)status);
        wf_plc_operation_free(op_json);
        free(aka_buf);
        wf_session_free(session);
        return 1;
    }
    printf("requestPlcOperationSignature: OK (check email for token)\n");

    if (!token) {
        printf("No token supplied; skipping sign + submit. Re-run with the "
               "emailed token as the final argument.\n");
        wf_plc_operation_free(op_json);
        free(aka_buf);
        wf_session_free(session);
        return 0;
    }

    wf_lex_com_atproto_identity_sign_plc_operation_main_input sign_input = {0};
    sign_input.has_token = true;
    sign_input.token = token;
    sign_input.has_rotation_keys = true;
    sign_input.rotation_keys.items = (const char *const *)aka;
    sign_input.rotation_keys.count = 1;
    sign_input.has_also_known_as = true;
    sign_input.also_known_as.items = aka;
    sign_input.also_known_as.count = 1;

    wf_response sign_res = {0};
    status = wf_lex_com_atproto_identity_sign_plc_operation_main_call(
        session->client, &sign_input, &sign_res);
    if (status != WF_OK) {
        fprintf(stderr, "signPlcOperation failed: %d\n", (int)status);
        wf_response_free(&sign_res);
        wf_plc_operation_free(op_json);
        free(aka_buf);
        wf_session_free(session);
        return 1;
    }

    wf_lex_com_atproto_identity_sign_plc_operation_main_output *signed_op = NULL;
    status = wf_lex_com_atproto_identity_sign_plc_operation_main_output_decode_json(
        sign_res.body, sign_res.body_len, &signed_op);
    wf_response_free(&sign_res);
    if (status != WF_OK || !signed_op || !signed_op->operation.data) {
        fprintf(stderr, "failed to decode signPlcOperation response: %d\n", (int)status);
        wf_lex_com_atproto_identity_sign_plc_operation_main_output_free(signed_op);
        wf_plc_operation_free(op_json);
        free(aka_buf);
        wf_session_free(session);
        return 1;
    }
    printf("Signed operation:\n%.*s\n",
           (int)signed_op->operation.length, signed_op->operation.data);

    wf_lex_com_atproto_identity_submit_plc_operation_main_input submit_input = {0};
    submit_input.operation.data = signed_op->operation.data;
    submit_input.operation.length = signed_op->operation.length;

    wf_response submit_res = {0};
    status = wf_lex_com_atproto_identity_submit_plc_operation_main_call(
        session->client, &submit_input, &submit_res);
    if (status == WF_OK) {
        printf("submitPlcOperation: OK\n");
    } else {
        fprintf(stderr, "submitPlcOperation failed: %d\n", (int)status);
    }
    wf_response_free(&submit_res);

    wf_lex_com_atproto_identity_sign_plc_operation_main_output_free(signed_op);
    wf_plc_operation_free(op_json);
    free(aka_buf);
    wf_session_free(session);
    return status == WF_OK ? 0 : 1;
}
