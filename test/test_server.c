#include "wolfram/server.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static wf_xrpc_client *new_client(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("http://127.0.0.1");
    WF_CHECK(client != NULL);
    return client;
}

static void test_describe_invalid(void) {
    wf_server_describe out = {0};

    WF_CHECK(wf_server_describe(NULL, &out) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();
    WF_CHECK(wf_server_describe(client, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_describe_free_safe(void) {
    wf_server_describe out = {0};

    wf_server_describe_free(&out);
    wf_server_describe_free(NULL);
}

static void test_create_account_invalid(void) {
    wf_server_create_account_result out = {0};
    wf_server_create_account_input input = {0};

    WF_CHECK(wf_server_create_account(NULL, &input, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_account(NULL, NULL, &out) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();

    WF_CHECK(wf_server_create_account(client, NULL, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_account(client, &input, NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_server_create_account(client, &input, &out) == WF_ERR_INVALID_ARG);

    input.handle = "";
    WF_CHECK(wf_server_create_account(client, &input, &out) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_create_account_free_safe(void) {
    wf_server_create_account_result out = {0};

    wf_server_create_account_result_free(&out);
    wf_server_create_account_result_free(NULL);
}

static void test_create_app_password_invalid(void) {
    wf_server_app_password out = {0};
    wf_server_create_app_password_input input = {0};

    WF_CHECK(wf_server_create_app_password(NULL, &input, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_app_password(NULL, NULL, &out) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();

    WF_CHECK(wf_server_create_app_password(client, NULL, &out) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_app_password(client, &input, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_create_app_password(client, &input, &out) ==
             WF_ERR_INVALID_ARG);

    input.name = "";
    WF_CHECK(wf_server_create_app_password(client, &input, &out) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_app_password_free_safe(void) {
    wf_server_app_password out = {0};

    wf_server_app_password_free(&out);
    wf_server_app_password_free(NULL);
}

static void test_list_app_passwords_invalid(void) {
    wf_server_app_password_list out = {0};

    WF_CHECK(wf_server_list_app_passwords(NULL, &out) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();
    WF_CHECK(wf_server_list_app_passwords(client, NULL) == WF_ERR_INVALID_ARG);
    wf_xrpc_client_free(client);
}

static void test_app_password_list_free_safe(void) {
    wf_server_app_password_list out = {0};

    wf_server_app_password_list_free(&out);
    wf_server_app_password_list_free(NULL);
}

static void test_revoke_app_password_invalid(void) {
    wf_server_revoke_app_password_input input = {0};

    WF_CHECK(wf_server_revoke_app_password(NULL, &input) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_revoke_app_password(NULL, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();

    WF_CHECK(wf_server_revoke_app_password(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_revoke_app_password(client, &input) == WF_ERR_INVALID_ARG);

    input.name = "";
    WF_CHECK(wf_server_revoke_app_password(client, &input) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_delete_account_invalid(void) {
    wf_server_delete_account_input input = {0};

    WF_CHECK(wf_server_delete_account(NULL, &input) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_delete_account(NULL, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();

    WF_CHECK(wf_server_delete_account(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_delete_account(client, &input) == WF_ERR_INVALID_ARG);

    input.did = "did:plc:test";
    WF_CHECK(wf_server_delete_account(client, &input) == WF_ERR_INVALID_ARG);

    input.password = "";
    WF_CHECK(wf_server_delete_account(client, &input) == WF_ERR_INVALID_ARG);

    input.password = "secret";
    input.token = "";
    WF_CHECK(wf_server_delete_account(client, &input) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_request_password_reset_invalid(void) {
    wf_server_request_password_reset_input input = {0};

    WF_CHECK(wf_server_request_password_reset(NULL, &input) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_request_password_reset(NULL, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();

    WF_CHECK(wf_server_request_password_reset(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_request_password_reset(client, &input) ==
             WF_ERR_INVALID_ARG);

    input.email = "";
    WF_CHECK(wf_server_request_password_reset(client, &input) ==
             WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

static void test_reset_password_invalid(void) {
    wf_server_reset_password_input input = {0};

    WF_CHECK(wf_server_reset_password(NULL, &input) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_reset_password(NULL, NULL) == WF_ERR_INVALID_ARG);

    wf_xrpc_client *client = new_client();

    WF_CHECK(wf_server_reset_password(client, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_server_reset_password(client, &input) == WF_ERR_INVALID_ARG);

    input.reset_token = "tok";
    WF_CHECK(wf_server_reset_password(client, &input) == WF_ERR_INVALID_ARG);

    input.reset_token = "";
    input.new_password = "new";
    WF_CHECK(wf_server_reset_password(client, &input) == WF_ERR_INVALID_ARG);

    input.reset_token = "tok";
    input.new_password = "";
    WF_CHECK(wf_server_reset_password(client, &input) == WF_ERR_INVALID_ARG);

    wf_xrpc_client_free(client);
}

int main(void) {
    test_describe_invalid();
    test_describe_free_safe();
    test_create_account_invalid();
    test_create_account_free_safe();
    test_create_app_password_invalid();
    test_app_password_free_safe();
    test_list_app_passwords_invalid();
    test_app_password_list_free_safe();
    test_revoke_app_password_invalid();
    test_delete_account_invalid();
    test_request_password_reset_invalid();
    test_reset_password_invalid();

    WF_TEST_SUMMARY();
}
