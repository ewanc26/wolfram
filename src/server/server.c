#include "wolfram/server.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *wf_server_strdup(const char *value) {
    size_t len;
    char *copy;

    if (!value) {
        return NULL;
    }

    len = strlen(value);
    copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, len + 1);
    return copy;
}

static void wf_server_free_strings(char **items, size_t count) {
    size_t i;

    if (!items) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

static wf_status wf_server_json_string(const cJSON *object, const char *name,
                                       int required, char **out) {
    const cJSON *item;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;

    item = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (!item) {
        return required ? WF_ERR_PARSE : WF_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
        return required ? WF_ERR_PARSE : WF_OK;
    }

    *out = wf_server_strdup(item->valuestring);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status wf_server_json_string_array(const cJSON *object, const char *name,
                                             char ***items_out, size_t *count_out) {
    const cJSON *array;
    const cJSON *item;
    char **items = NULL;
    size_t count;
    size_t index = 0;

    if (!items_out || !count_out) {
        return WF_ERR_INVALID_ARG;
    }
    *items_out = NULL;
    *count_out = 0;

    array = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (!array || !cJSON_IsArray(array)) {
        return WF_OK;
    }

    count = (size_t)cJSON_GetArraySize(array);
    if (count == 0) {
        return WF_OK;
    }

    items = calloc(count, sizeof(*items));
    if (!items) {
        return WF_ERR_ALLOC;
    }

    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
            wf_server_free_strings(items, index);
            return WF_ERR_PARSE;
        }
        items[index] = wf_server_strdup(item->valuestring);
        if (!items[index]) {
            wf_server_free_strings(items, index);
            return WF_ERR_ALLOC;
        }
        index++;
    }

    *items_out = items;
    *count_out = count;
    return WF_OK;
}

static wf_status wf_server_json_bool(const cJSON *object, const char *name, int *out) {
    const cJSON *item;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = -1;

    item = object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (!item) {
        return WF_OK;
    }
    if (!cJSON_IsBool(item)) {
        return WF_ERR_PARSE;
    }
    *out = cJSON_IsTrue(item) ? 1 : 0;
    return WF_OK;
}

void wf_server_describe_free(wf_server_description *desc) {
    if (!desc) {
        return;
    }

    free(desc->did);
    wf_server_free_strings(desc->available_user_domains,
                           desc->available_user_domains_count);
    free(desc->links_privacy_policy);
    free(desc->links_terms_of_service);
    free(desc->contact_email);

    memset(desc, 0, sizeof(*desc));
    desc->invite_code_required = -1;
    desc->phone_verification_required = -1;
}

wf_status wf_server_describe(wf_xrpc_client *client, wf_server_description *out) {
    wf_response response = {0};
    cJSON *root = NULL;
    cJSON *links = NULL;
    cJSON *contact = NULL;
    wf_status status;

    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->invite_code_required = -1;
    out->phone_verification_required = -1;

    status = wf_xrpc_query(client, "com.atproto.server.describeServer", NULL,
                           &response);
    if (status != WF_OK) {
        return status;
    }

    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    status = wf_server_json_string(root, "did", 0, &out->did);
    if (status == WF_OK) {
        status = wf_server_json_bool(root, "inviteCodeRequired",
                                     &out->invite_code_required);
    }
    if (status == WF_OK) {
        status = wf_server_json_bool(root, "phoneVerificationRequired",
                                     &out->phone_verification_required);
    }
    if (status == WF_OK) {
        status = wf_server_json_string_array(root, "availableUserDomains",
                                             &out->available_user_domains,
                                             &out->available_user_domains_count);
    }
    if (status == WF_OK) {
        links = cJSON_GetObjectItemCaseSensitive(root, "links");
        if (links && cJSON_IsObject(links)) {
            status = wf_server_json_string(links, "privacyPolicy", 0,
                                           &out->links_privacy_policy);
        }
    }
    if (status == WF_OK && links && cJSON_IsObject(links)) {
        status = wf_server_json_string(links, "termsOfService", 0,
                                       &out->links_terms_of_service);
    }
    if (status == WF_OK) {
        contact = cJSON_GetObjectItemCaseSensitive(root, "contact");
        if (contact && cJSON_IsObject(contact)) {
            status = wf_server_json_string(contact, "email", 0,
                                           &out->contact_email);
        }
    }

    cJSON_Delete(root);
    wf_response_free(&response);

    if (status != WF_OK) {
        wf_server_describe_free(out);
    }
    return status;
}

void wf_server_create_account_result_free(wf_server_create_account_result *result) {
    if (!result) {
        return;
    }

    free(result->access_jwt);
    free(result->refresh_jwt);
    free(result->handle);
    free(result->did);
    free(result->did_doc);

    memset(result, 0, sizeof(*result));
}

wf_status wf_server_create_account(wf_xrpc_client *client,
                                   const wf_server_create_account_input *input,
                                   wf_server_create_account_result *out) {
    wf_response response = {0};
    cJSON *body = NULL;
    cJSON *root = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->handle || input->handle[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    body = cJSON_CreateObject();
    if (!body) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "handle", input->handle)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    if (input->email && input->email[0] != '\0') {
        if (!cJSON_AddStringToObject(body, "email", input->email)) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }
    if (input->password && input->password[0] != '\0') {
        if (!cJSON_AddStringToObject(body, "password", input->password)) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }
    if (input->invite_code && input->invite_code[0] != '\0') {
        if (!cJSON_AddStringToObject(body, "inviteCode", input->invite_code)) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }
    if (input->recovery_key && input->recovery_key[0] != '\0') {
        if (!cJSON_AddStringToObject(body, "recoveryKey", input->recovery_key)) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    status = wf_xrpc_procedure(client, "com.atproto.server.createAccount", json,
                               &response);
    free(json);
    if (status != WF_OK) {
        return status;
    }

    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    status = wf_server_json_string(root, "accessJwt", 1, &out->access_jwt);
    if (status == WF_OK) {
        status = wf_server_json_string(root, "refreshJwt", 1, &out->refresh_jwt);
    }
    if (status == WF_OK) {
        status = wf_server_json_string(root, "handle", 1, &out->handle);
    }
    if (status == WF_OK) {
        status = wf_server_json_string(root, "did", 1, &out->did);
    }
    if (status == WF_OK) {
        status = wf_server_json_string(root, "didDoc", 0, &out->did_doc);
    }

    cJSON_Delete(root);
    wf_response_free(&response);

    if (status != WF_OK) {
        wf_server_create_account_result_free(out);
    }
    return status;
}

void wf_server_app_password_free(wf_server_app_password *pwd) {
    if (!pwd) {
        return;
    }

    free(pwd->name);
    free(pwd->password);
    free(pwd->created_at);

    memset(pwd, 0, sizeof(*pwd));
    pwd->privileged = -1;
}

wf_status wf_server_create_app_password(wf_xrpc_client *client,
                                        const wf_server_create_app_password_input *input,
                                        wf_server_app_password *out) {
    wf_response response = {0};
    cJSON *body = NULL;
    cJSON *root = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->name || input->name[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->privileged = -1;

    body = cJSON_CreateObject();
    if (!body) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "name", input->name)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    if (input->privileged) {
        if (!cJSON_AddBoolToObject(body, "privileged", 1)) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    status = wf_xrpc_procedure(client, "com.atproto.server.createAppPassword", json,
                               &response);
    free(json);
    if (status != WF_OK) {
        return status;
    }

    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    status = wf_server_json_string(root, "name", 1, &out->name);
    if (status == WF_OK) {
        status = wf_server_json_string(root, "password", 1, &out->password);
    }
    if (status == WF_OK) {
        status = wf_server_json_string(root, "createdAt", 1, &out->created_at);
    }
    if (status == WF_OK) {
        status = wf_server_json_bool(root, "privileged", &out->privileged);
    }

    cJSON_Delete(root);
    wf_response_free(&response);

    if (status != WF_OK) {
        wf_server_app_password_free(out);
    }
    return status;
}

void wf_server_app_password_list_free(wf_server_app_password_list *list) {
    size_t i;

    if (!list) {
        return;
    }

    for (i = 0; i < list->password_count; i++) {
        wf_server_app_password_free(&list->passwords[i]);
    }
    free(list->passwords);

    memset(list, 0, sizeof(*list));
}

static wf_status wf_server_app_password_parse(const cJSON *item,
                                              wf_server_app_password *out) {
    wf_status status;

    memset(out, 0, sizeof(*out));
    out->privileged = -1;

    if (!cJSON_IsObject(item)) {
        return WF_ERR_PARSE;
    }

    status = wf_server_json_string(item, "name", 1, &out->name);
    if (status == WF_OK) {
        status = wf_server_json_string(item, "createdAt", 1, &out->created_at);
    }
    if (status == WF_OK) {
        status = wf_server_json_bool(item, "privileged", &out->privileged);
    }
    return status;
}

wf_status wf_server_list_app_passwords(wf_xrpc_client *client,
                                       wf_server_app_password_list *out) {
    wf_response response = {0};
    cJSON *root = NULL;
    const cJSON *array;
    const cJSON *item;
    wf_server_app_password *passwords = NULL;
    size_t count;
    size_t index = 0;
    wf_status status;

    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    status = wf_xrpc_query(client, "com.atproto.server.listAppPasswords", NULL,
                           &response);
    if (status != WF_OK) {
        return status;
    }

    root = cJSON_ParseWithLength(response.body, response.body_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    array = cJSON_GetObjectItemCaseSensitive(root, "passwords");
    if (!array || !cJSON_IsArray(array)) {
        cJSON_Delete(root);
        wf_response_free(&response);
        return WF_ERR_PARSE;
    }

    count = (size_t)cJSON_GetArraySize(array);
    if (count > 0) {
        passwords = calloc(count, sizeof(*passwords));
        if (!passwords) {
            cJSON_Delete(root);
            wf_response_free(&response);
            return WF_ERR_ALLOC;
        }
    }

    cJSON_ArrayForEach(item, array) {
        status = wf_server_app_password_parse(item, &passwords[index]);
        if (status != WF_OK) {
            for (index = 0; index < count; index++) {
                wf_server_app_password_free(&passwords[index]);
            }
            free(passwords);
            cJSON_Delete(root);
            wf_response_free(&response);
            return status;
        }
        index++;
    }

    out->passwords = passwords;
    out->password_count = count;

    cJSON_Delete(root);
    wf_response_free(&response);
    return WF_OK;
}

wf_status wf_server_revoke_app_password(wf_xrpc_client *client,
                                        const wf_server_revoke_app_password_input *input) {
    wf_response response = {0};
    cJSON *body = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !input) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->name || input->name[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    body = cJSON_CreateObject();
    if (!body) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "name", input->name)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    status = wf_xrpc_procedure(client, "com.atproto.server.revokeAppPassword", json,
                               &response);
    free(json);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_delete_account(wf_xrpc_client *client,
                                   const wf_server_delete_account_input *input) {
    wf_response response = {0};
    cJSON *body = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !input) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->did || input->did[0] == '\0' || !input->password ||
        input->password[0] == '\0' || !input->token || input->token[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    body = cJSON_CreateObject();
    if (!body) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "did", input->did)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "password", input->password)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "token", input->token)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    status = wf_xrpc_procedure(client, "com.atproto.server.deleteAccount", json,
                               &response);
    free(json);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_request_password_reset(wf_xrpc_client *client,
                                           const wf_server_request_password_reset_input *input) {
    wf_response response = {0};
    cJSON *body = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !input) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->email || input->email[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    body = cJSON_CreateObject();
    if (!body) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "email", input->email)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    status = wf_xrpc_procedure(client, "com.atproto.server.requestPasswordReset",
                               json, &response);
    free(json);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_reset_password(wf_xrpc_client *client,
                                   const wf_server_reset_password_input *input) {
    wf_response response = {0};
    cJSON *body = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !input) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->reset_token || input->reset_token[0] == '\0' ||
        !input->new_password || input->new_password[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    body = cJSON_CreateObject();
    if (!body) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "resetToken", input->reset_token)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(body, "newPassword", input->new_password)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    status = wf_xrpc_procedure(client, "com.atproto.server.resetPassword", json,
                                &response);
    free(json);
    wf_response_free(&response);
    return status;
}

/* ------------------------------------------------------------------ */
/* Additional com.atproto.server account-management operations.         */
/* ------------------------------------------------------------------ */

void wf_server_create_invite_code_result_free(
    wf_server_create_invite_code_result *result) {
    if (!result) {
        return;
    }
    free(result->code);
    memset(result, 0, sizeof(*result));
}

wf_status wf_server_create_invite_code(
    wf_xrpc_client *client, const wf_server_create_invite_code_input *input,
    wf_server_create_invite_code_result *out) {
    wf_response response = {0};
    wf_lex_com_atproto_server_create_invite_code_main_input lex_in = {0};
    wf_lex_com_atproto_server_create_invite_code_main_output *lex_out = NULL;
    wf_status status;

    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (input->use_count <= 0) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    lex_in.use_count = input->use_count;
    if (input->for_account && input->for_account[0] != '\0') {
        lex_in.has_for_account = 1;
        lex_in.for_account = input->for_account;
    }

    status = wf_lex_com_atproto_server_create_invite_code_main_call(client,
                                                                    &lex_in, &response);
    if (status != WF_OK) {
        wf_response_free(&response);
        return status;
    }

    status = wf_lex_com_atproto_server_create_invite_code_main_output_decode_json(
        response.body, response.body_len, &lex_out);
    wf_response_free(&response);
    if (status != WF_OK || !lex_out) {
        return status != WF_OK ? status : WF_ERR_PARSE;
    }

    if (lex_out->code) {
        out->code = wf_server_strdup(lex_out->code);
        if (!out->code) {
            status = WF_ERR_ALLOC;
        }
    }
    wf_lex_com_atproto_server_create_invite_code_main_output_free(lex_out);
    if (status != WF_OK) {
        wf_server_create_invite_code_result_free(out);
    }
    return status;
}

static void wf_server_invite_codes_for_account_free(
    wf_server_invite_codes_for_account *entry) {
    if (!entry) {
        return;
    }
    if (entry->codes) {
        for (size_t i = 0; i < entry->code_count; i++) {
            free(entry->codes[i]);
        }
        free(entry->codes);
    }
    free(entry->account);
    memset(entry, 0, sizeof(*entry));
}

void wf_server_create_invite_codes_result_free(
    wf_server_create_invite_codes_result *result) {
    if (!result) {
        return;
    }
    if (result->accounts) {
        for (size_t i = 0; i < result->account_count; i++) {
            wf_server_invite_codes_for_account_free(&result->accounts[i]);
        }
        free(result->accounts);
    }
    memset(result, 0, sizeof(*result));
}

wf_status wf_server_create_invite_codes(
    wf_xrpc_client *client, const wf_server_create_invite_codes_input *input,
    wf_server_create_invite_codes_result *out) {
    wf_response response = {0};
    wf_lex_com_atproto_server_create_invite_codes_main_input lex_in = {0};
    wf_lex_com_atproto_server_create_invite_codes_main_output *lex_out = NULL;
    wf_status status;

    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (input->code_count <= 0 || input->use_count <= 0) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    lex_in.code_count = input->code_count;
    lex_in.use_count = input->use_count;
    if (input->for_accounts_count > 0 && input->for_accounts) {
        lex_in.has_for_accounts = 1;
        lex_in.for_accounts.items = input->for_accounts;
        lex_in.for_accounts.count = input->for_accounts_count;
    }

    status = wf_lex_com_atproto_server_create_invite_codes_main_call(
        client, &lex_in, &response);
    if (status != WF_OK) {
        wf_response_free(&response);
        return status;
    }

    status = wf_lex_com_atproto_server_create_invite_codes_main_output_decode_json(
        response.body, response.body_len, &lex_out);
    wf_response_free(&response);
    if (status != WF_OK || !lex_out) {
        return status != WF_OK ? status : WF_ERR_PARSE;
    }

    if (lex_out->codes.count > 0) {
        out->accounts = calloc(lex_out->codes.count, sizeof(*out->accounts));
        if (!out->accounts) {
            status = WF_ERR_ALLOC;
        }
        for (size_t i = 0; status == WF_OK && i < lex_out->codes.count; i++) {
            const wf_lex_com_atproto_server_create_invite_codes_account_codes *src =
                lex_out->codes.items[i];
            wf_server_invite_codes_for_account *dst = &out->accounts[i];
            if (src->account) {
                dst->account = wf_server_strdup(src->account);
                if (!dst->account) {
                    status = WF_ERR_ALLOC;
                }
            }
            if (status == WF_OK && src->codes.count > 0) {
                dst->codes = calloc(src->codes.count, sizeof(*dst->codes));
                if (!dst->codes) {
                    status = WF_ERR_ALLOC;
                }
                for (size_t j = 0; status == WF_OK && j < src->codes.count; j++) {
                    if (src->codes.items[j]) {
                        dst->codes[j] = wf_server_strdup(src->codes.items[j]);
                        if (!dst->codes[j]) {
                            status = WF_ERR_ALLOC;
                        } else {
                            dst->code_count++;
                        }
                    }
                }
            }
            if (status == WF_OK) {
                out->account_count++;
            }
        }
    }

    wf_lex_com_atproto_server_create_invite_codes_main_output_free(lex_out);
    if (status != WF_OK) {
        wf_server_create_invite_codes_result_free(out);
    }
    return status;
}

wf_status wf_server_revoke_invite_codes(
    wf_xrpc_client *client, const wf_server_revoke_invite_codes_input *input) {
    wf_response response = {0};
    cJSON *body = NULL;
    cJSON *array = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !input) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->codes || input->code_count == 0) {
        return WF_ERR_INVALID_ARG;
    }

    body = cJSON_CreateObject();
    if (!body) {
        return WF_ERR_ALLOC;
    }
    array = cJSON_AddArrayToObject(body, "codes");
    if (!array) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    for (size_t i = 0; i < input->code_count; i++) {
        if (!input->codes[i] || input->codes[i][0] == '\0' ||
            !cJSON_AddItemToArray(array,
                                  cJSON_CreateString(input->codes[i]))) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    status = wf_xrpc_procedure(client, "com.atproto.server.revokeInviteCodes",
                                json, &response);
    free(json);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_activate_account(wf_xrpc_client *client) {
    wf_response response = {0};
    wf_status status;

    if (!client) {
        return WF_ERR_INVALID_ARG;
    }
    status = wf_xrpc_procedure(client, "com.atproto.server.activateAccount", "{}",
                               &response);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_deactivate_account(
    wf_xrpc_client *client, const wf_server_deactivate_account_input *input) {
    wf_response response = {0};
    wf_lex_com_atproto_server_deactivate_account_main_input lex_in = {0};
    wf_status status;

    if (!client) {
        return WF_ERR_INVALID_ARG;
    }
    if (input && input->delete_after && input->delete_after[0] != '\0') {
        lex_in.has_delete_after = 1;
        lex_in.delete_after = input->delete_after;
    }

    status = wf_lex_com_atproto_server_deactivate_account_main_call(client,
                                                                    &lex_in, &response);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_confirm_email(wf_xrpc_client *client,
                                  const wf_server_confirm_email_input *input) {
    wf_response response = {0};
    wf_lex_com_atproto_server_confirm_email_main_input lex_in = {0};
    wf_status status;

    if (!client || !input) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->email || input->email[0] == '\0' || !input->token ||
        input->token[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    lex_in.email = input->email;
    lex_in.token = input->token;

    status = wf_lex_com_atproto_server_confirm_email_main_call(client, &lex_in,
                                                               &response);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_request_email_update(
    wf_xrpc_client *client, wf_server_request_email_update_result *out) {
    wf_response response = {0};
    cJSON *root = NULL;
    const cJSON *token_required;
    wf_status status;

    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    status = wf_xrpc_procedure(client, "com.atproto.server.requestEmailUpdate", "{}",
                               &response);
    if (status != WF_OK) {
        wf_response_free(&response);
        return status;
    }

    root = cJSON_ParseWithLength(response.body, response.body_len);
    wf_response_free(&response);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    token_required = cJSON_GetObjectItemCaseSensitive(root, "tokenRequired");
    if (token_required && cJSON_IsBool(token_required)) {
        out->token_required = cJSON_IsTrue(token_required) ? 1 : 0;
    }
    cJSON_Delete(root);
    return WF_OK;
}

wf_status wf_server_request_email_confirmation(wf_xrpc_client *client) {
    wf_response response = {0};
    wf_status status;

    if (!client) {
        return WF_ERR_INVALID_ARG;
    }
    status = wf_xrpc_procedure(client, "com.atproto.server.requestEmailConfirmation",
                               "{}", &response);
    wf_response_free(&response);
    return status;
}

wf_status wf_server_update_email(wf_xrpc_client *client,
                                 const wf_server_update_email_input *input) {
    wf_response response = {0};
    wf_lex_com_atproto_server_update_email_main_input lex_in = {0};
    wf_status status;

    if (!client || !input) {
        return WF_ERR_INVALID_ARG;
    }
    if (!input->email || input->email[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    lex_in.email = input->email;
    if (input->has_email_auth_factor) {
        lex_in.has_email_auth_factor = 1;
        lex_in.email_auth_factor = input->email_auth_factor ? 1 : 0;
    }
    if (input->has_token && input->token && input->token[0] != '\0') {
        lex_in.has_token = 1;
        lex_in.token = input->token;
    }

    status = wf_lex_com_atproto_server_update_email_main_call(client, &lex_in,
                                                              &response);
    wf_response_free(&response);
    return status;
}
