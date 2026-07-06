#include "internal.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void wf_oauth_authorization_state_free(wf_oauth_authorization_state *state) {
    if (!state) return;
    free(state->issuer);
    free(state->code_verifier);
    free(state->app_state);
    free(state->client_auth_key_id);
    wf_oauth_dpop_key_free(state->dpop_key);
    memset(state, 0, sizeof(*state));
}

wf_status wf_oauth_authorization_state_create(
    const char *issuer, const wf_oauth_pkce *pkce,
    const wf_oauth_dpop_key *dpop_key, const char *app_state,
    const char *client_auth_key_id, int64_t now, int64_t ttl_seconds,
    wf_oauth_authorization_state *out) {
    unsigned char private_key[32];
    wf_status status;
    if (!issuer || !pkce || !dpop_key || !out || now <= 0 || ttl_seconds <= 0 ||
        now > INT64_MAX - ttl_seconds ||
        !wf_oauth_url_valid(issuer, 1, 1, 0)) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (wf_oauth_pkce_from_verifier(pkce->verifier, &(wf_oauth_pkce){0}) != WF_OK) {
        return WF_ERR_INVALID_ARG;
    }
    out->issuer = wf_oauth_strdup(issuer);
    out->code_verifier = wf_oauth_strdup(pkce->verifier);
    if (app_state) out->app_state = wf_oauth_strdup(app_state);
    if (client_auth_key_id) out->client_auth_key_id = wf_oauth_strdup(client_auth_key_id);
    status = wf_oauth_dpop_key_export(dpop_key, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &out->dpop_key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (!out->issuer || !out->code_verifier || (app_state && !out->app_state) ||
        (client_auth_key_id && (!*client_auth_key_id || !out->client_auth_key_id)) ||
        status != WF_OK) {
        if (status == WF_OK) status = WF_ERR_ALLOC;
        wf_oauth_authorization_state_free(out);
        return status;
    }
    out->expires_at = now + ttl_seconds;
    return WF_OK;
}

static wf_status wf_oauth_private_jwk(const wf_oauth_dpop_key *key, cJSON **out) {
    cJSON *jwk = NULL;
    char *x = NULL, *y = NULL, *d = NULL;
    unsigned char private_key[32];
    wf_status status = wf_oauth_dpop_jwk(key, &jwk, &x, &y);
    if (status == WF_OK) status = wf_oauth_dpop_key_export(key, private_key);
    if (status == WF_OK) status = wf_oauth_base64url(private_key, sizeof(private_key), &d);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (status == WF_OK && !cJSON_AddStringToObject(jwk, "d", d)) status = WF_ERR_ALLOC;
    free(x); free(y); free(d);
    if (status != WF_OK) { cJSON_Delete(jwk); return status; }
    *out = jwk;
    return WF_OK;
}

wf_status wf_oauth_authorization_state_serialize(
    const wf_oauth_authorization_state *state, char **json_out) {
    cJSON *root = NULL, *jwk = NULL, *auth = NULL;
    wf_status status;
    if (!state || !state->issuer || !state->code_verifier || !state->dpop_key ||
        state->expires_at <= 0 || !json_out) return WF_ERR_INVALID_ARG;
    *json_out = NULL;
    root = cJSON_CreateObject(); auth = cJSON_CreateObject();
    status = root && auth ? WF_OK : WF_ERR_ALLOC;
    if (status == WF_OK) status = wf_oauth_private_jwk(state->dpop_key, &jwk);
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(root, "iss", state->issuer) ||
         !cJSON_AddStringToObject(root, "verifier", state->code_verifier) ||
         !cJSON_AddNumberToObject(root, "expiresAt", (double)state->expires_at)))
        status = WF_ERR_ALLOC;
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(auth, "method", state->client_auth_key_id ?
                                  "private_key_jwt" : "none") ||
         (state->client_auth_key_id &&
          !cJSON_AddStringToObject(auth, "kid", state->client_auth_key_id))))
        status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "authMethod", auth)) status = WF_ERR_ALLOC;
        else auth = NULL;
    }
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "dpopJwk", jwk)) status = WF_ERR_ALLOC;
        else jwk = NULL;
    }
    if (status == WF_OK && state->app_state &&
        !cJSON_AddStringToObject(root, "appState", state->app_state)) status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        *json_out = cJSON_PrintUnformatted(root);
        if (!*json_out) status = WF_ERR_ALLOC;
    }
    cJSON_Delete(jwk);
    cJSON_Delete(auth);
    cJSON_Delete(root);
    return status;
}

static wf_status wf_oauth_base64url_32_decode(const char *encoded,
                                              unsigned char out[32]) {
    char padded[45];
    unsigned char decoded[33];
    int result, i;
    if (!encoded || strlen(encoded) != 43) return WF_ERR_PARSE;
    memcpy(padded, encoded, 43);
    padded[43] = '='; padded[44] = '\0';
    for (i = 0; i < 43; i++) {
        if (padded[i] == '-') padded[i] = '+';
        else if (padded[i] == '_') padded[i] = '/';
        else if (!isalnum((unsigned char)padded[i]) && padded[i] != '+' && padded[i] != '/')
            return WF_ERR_PARSE;
    }
    result = EVP_DecodeBlock(decoded, (const unsigned char *)padded, 44);
    if (result != 33) return WF_ERR_PARSE;
    memcpy(out, decoded, 32);
    OPENSSL_cleanse(decoded, sizeof(decoded));
    return WF_OK;
}

static wf_status wf_oauth_private_jwk_parse(const cJSON *jwk,
                                            wf_oauth_dpop_key **out) {
    const cJSON *item;
    const char *x, *y, *d;
    unsigned char private_key[32];
    wf_oauth_dpop_key *key = NULL;
    cJSON *expected = NULL;
    char *expected_x = NULL, *expected_y = NULL;
    wf_status status = WF_OK;
    *out = NULL;
    item = jwk ? cJSON_GetObjectItemCaseSensitive(jwk, "kty") : NULL;
    if (!cJSON_IsObject(jwk) || !cJSON_IsString(item) ||
        strcmp(item->valuestring, "EC") != 0) return WF_ERR_PARSE;
    item = cJSON_GetObjectItemCaseSensitive(jwk, "crv");
    if (!cJSON_IsString(item) || strcmp(item->valuestring, "P-256") != 0)
        return WF_ERR_PARSE;
#define REQUIRED_JWK_STRING(name, target) do { \
    item = cJSON_GetObjectItemCaseSensitive(jwk, name); \
    target = cJSON_IsString(item) ? item->valuestring : NULL; \
    if (!target) return WF_ERR_PARSE; \
} while (0)
    REQUIRED_JWK_STRING("x", x);
    REQUIRED_JWK_STRING("y", y);
    REQUIRED_JWK_STRING("d", d);
#undef REQUIRED_JWK_STRING
    status = wf_oauth_base64url_32_decode(d, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (status == WF_OK) status = wf_oauth_dpop_jwk(key, &expected,
                                                    &expected_x, &expected_y);
    if (status == WF_OK &&
        (strcmp(x, expected_x) != 0 || strcmp(y, expected_y) != 0)) {
        status = WF_ERR_PARSE;
    }
    free(expected_x); free(expected_y); cJSON_Delete(expected);
    if (status != WF_OK) { wf_oauth_dpop_key_free(key); return status; }
    *out = key;
    return WF_OK;
}

wf_status wf_oauth_authorization_state_parse(
    const char *json, size_t len, int64_t now, wf_oauth_authorization_state *out) {
    cJSON *root = NULL;
    const cJSON *jwk, *item;
    char *issuer = NULL, *verifier = NULL, *app_state = NULL, *auth_key_id = NULL;
    const char *x, *y, *d;
    unsigned char private_key[32];
    wf_oauth_dpop_key *key = NULL;
    cJSON *expected_jwk = NULL;
    char *expected_x = NULL, *expected_y = NULL;
    int64_t expires_at = 0;
    wf_status status = WF_OK;
    if (!json || !len || now <= 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) { status = WF_ERR_PARSE; goto done; }
    status = wf_oauth_json_string(root, "iss", 1, &issuer);
    if (status == WF_OK) status = wf_oauth_json_string(root, "verifier", 1, &verifier);
    if (status == WF_OK) status = wf_oauth_json_string(root, "appState", 0, &app_state);
    if (status == WF_OK) status = wf_oauth_positive_integer(root, "expiresAt", 1,
                                                             &expires_at, NULL);
    {
        const cJSON *auth = cJSON_GetObjectItemCaseSensitive(root, "authMethod");
        const cJSON *method = auth ? cJSON_GetObjectItemCaseSensitive(auth, "method") : NULL;
        if (status == WF_OK && (!cJSON_IsObject(auth) || !cJSON_IsString(method) ||
            (strcmp(method->valuestring, "none") != 0 &&
             strcmp(method->valuestring, "private_key_jwt") != 0))) status = WF_ERR_PARSE;
        if (status == WF_OK && strcmp(method->valuestring, "private_key_jwt") == 0)
            status = wf_oauth_json_string(auth, "kid", 1, &auth_key_id);
        if (status == WF_OK && strcmp(method->valuestring, "none") == 0 &&
            cJSON_GetObjectItemCaseSensitive(auth, "kid")) status = WF_ERR_PARSE;
    }
    jwk = cJSON_GetObjectItemCaseSensitive(root, "dpopJwk");
    item = jwk ? cJSON_GetObjectItemCaseSensitive(jwk, "kty") : NULL;
    if (status == WF_OK && (!cJSON_IsObject(jwk) || !cJSON_IsString(item) ||
                            strcmp(item->valuestring, "EC") != 0)) status = WF_ERR_PARSE;
#define JWK_STRING(name, target) do { item = jwk ? cJSON_GetObjectItemCaseSensitive(jwk, name) : NULL; \
    target = cJSON_IsString(item) ? item->valuestring : NULL; \
    if (status == WF_OK && !target) status = WF_ERR_PARSE; } while (0)
    JWK_STRING("crv", x); if (status == WF_OK && strcmp(x, "P-256") != 0) status = WF_ERR_PARSE;
    JWK_STRING("x", x); JWK_STRING("y", y); JWK_STRING("d", d);
#undef JWK_STRING
    if (status == WF_OK && (!wf_oauth_url_valid(issuer, 1, 1, 0) || expires_at <= now))
        status = WF_ERR_PARSE;
    if (status == WF_OK && wf_oauth_pkce_from_verifier(verifier, &(wf_oauth_pkce){0}) != WF_OK)
        status = WF_ERR_PARSE;
    if (status == WF_OK) status = wf_oauth_base64url_32_decode(d, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (status == WF_OK) status = wf_oauth_dpop_jwk(key, &expected_jwk, &expected_x, &expected_y);
    if (status == WF_OK && (strcmp(x, expected_x) != 0 || strcmp(y, expected_y) != 0))
        status = WF_ERR_PARSE;
    if (status == WF_OK) {
        out->issuer = issuer; issuer = NULL;
        out->code_verifier = verifier; verifier = NULL;
        out->app_state = app_state; app_state = NULL;
        out->client_auth_key_id = auth_key_id; auth_key_id = NULL;
        out->expires_at = expires_at;
        out->dpop_key = key; key = NULL;
    }
done:
    free(issuer); free(verifier); free(app_state); free(auth_key_id);
    free(expected_x); free(expected_y);
    cJSON_Delete(expected_jwk);
    wf_oauth_dpop_key_free(key);
    cJSON_Delete(root);
    if (status != WF_OK) wf_oauth_authorization_state_free(out);
    return status;
}

void wf_oauth_session_state_free(wf_oauth_session_state *session) {
    if (!session) return;
    free(session->issuer); free(session->subject); free(session->audience);
    free(session->scope); free(session->access_token); free(session->refresh_token);
    free(session->client_auth_key_id);
    wf_oauth_dpop_key_free(session->dpop_key);
    memset(session, 0, sizeof(*session));
}

wf_status wf_oauth_session_state_create(
    const char *issuer, const char *audience, const wf_oauth_dpop_key *dpop_key,
    const wf_oauth_token_response *token, const char *client_auth_key_id, int64_t now,
    wf_oauth_session_state *out) {
    unsigned char private_key[32];
    wf_status status;
    if (!issuer || !audience || !dpop_key || !token || !out || now <= 0 ||
        !token->sub || !token->scope || !token->access_token ||
        !token->token_type || strcmp(token->token_type, "DPoP") != 0 ||
        strncmp(token->sub, "did:", 4) != 0 ||
        !wf_oauth_url_valid(issuer, 1, 1, 0) ||
        !wf_oauth_url_valid(audience, 1, 1, 0) ||
        (token->expires_in_present &&
         (token->expires_in <= 0 || now > INT64_MAX - token->expires_in))) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->issuer = wf_oauth_strdup(issuer);
    out->subject = wf_oauth_strdup(token->sub);
    out->audience = wf_oauth_strdup(audience);
    out->scope = wf_oauth_strdup(token->scope);
    out->access_token = wf_oauth_strdup(token->access_token);
    if (token->refresh_token) out->refresh_token = wf_oauth_strdup(token->refresh_token);
    if (client_auth_key_id) out->client_auth_key_id = wf_oauth_strdup(client_auth_key_id);
    status = wf_oauth_dpop_key_export(dpop_key, private_key);
    if (status == WF_OK) status = wf_oauth_dpop_key_import(private_key, &out->dpop_key);
    OPENSSL_cleanse(private_key, sizeof(private_key));
    if (!out->issuer || !out->subject || !out->audience || !out->scope ||
        !out->access_token || (token->refresh_token && !out->refresh_token) ||
        (client_auth_key_id && (!*client_auth_key_id || !out->client_auth_key_id)) ||
        status != WF_OK) {
        if (status == WF_OK) status = WF_ERR_ALLOC;
        wf_oauth_session_state_free(out);
        return status;
    }
    if (token->expires_in_present) out->expires_at = now + token->expires_in;
    return WF_OK;
}

wf_status wf_oauth_session_state_serialize(const wf_oauth_session_state *session,
                                           char **json_out) {
    cJSON *root = NULL, *tokens = NULL, *jwk = NULL, *auth = NULL;
    wf_status status;
    if (!session || !json_out || !session->issuer || !session->subject ||
        !session->audience || !session->scope || !session->access_token ||
        !session->dpop_key) return WF_ERR_INVALID_ARG;
    *json_out = NULL;
    root = cJSON_CreateObject(); tokens = cJSON_CreateObject(); auth = cJSON_CreateObject();
    status = root && tokens && auth ? wf_oauth_private_jwk(session->dpop_key, &jwk) : WF_ERR_ALLOC;
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(tokens, "iss", session->issuer) ||
         !cJSON_AddStringToObject(tokens, "sub", session->subject) ||
         !cJSON_AddStringToObject(tokens, "aud", session->audience) ||
         !cJSON_AddStringToObject(tokens, "scope", session->scope) ||
         !cJSON_AddStringToObject(tokens, "access_token", session->access_token) ||
         !cJSON_AddStringToObject(tokens, "token_type", "DPoP"))) status = WF_ERR_ALLOC;
    if (status == WF_OK &&
        (!cJSON_AddStringToObject(auth, "method", session->client_auth_key_id ?
                                  "private_key_jwt" : "none") ||
         (session->client_auth_key_id &&
          !cJSON_AddStringToObject(auth, "kid", session->client_auth_key_id))))
        status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "authMethod", auth)) status = WF_ERR_ALLOC;
        else auth = NULL;
    }
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "dpopJwk", jwk)) status = WF_ERR_ALLOC;
        else jwk = NULL;
    }
    if (status == WF_OK && session->refresh_token &&
        !cJSON_AddStringToObject(tokens, "refresh_token", session->refresh_token))
        status = WF_ERR_ALLOC;
    if (status == WF_OK && session->expires_at > 0 &&
        !cJSON_AddNumberToObject(tokens, "expires_at", (double)session->expires_at))
        status = WF_ERR_ALLOC;
    if (status == WF_OK) {
        if (!cJSON_AddItemToObject(root, "tokenSet", tokens)) status = WF_ERR_ALLOC;
        else tokens = NULL;
    }
    if (status == WF_OK) {
        *json_out = cJSON_PrintUnformatted(root);
        if (!*json_out) status = WF_ERR_ALLOC;
    }
    cJSON_Delete(jwk); cJSON_Delete(auth); cJSON_Delete(tokens); cJSON_Delete(root);
    return status;
}

wf_status wf_oauth_session_state_parse(const char *json, size_t len,
                                       const char *expected_subject,
                                       wf_oauth_session_state *out) {
    cJSON *root = NULL;
    const cJSON *tokens, *jwk, *auth, *method, *type;
    wf_status status;
    if (!json || !len || !expected_subject || !out ||
        strncmp(expected_subject, "did:", 4) != 0) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) { status = WF_ERR_PARSE; goto done; }
    auth = cJSON_GetObjectItemCaseSensitive(root, "authMethod");
    method = auth ? cJSON_GetObjectItemCaseSensitive(auth, "method") : NULL;
    tokens = cJSON_GetObjectItemCaseSensitive(root, "tokenSet");
    jwk = cJSON_GetObjectItemCaseSensitive(root, "dpopJwk");
    if (!cJSON_IsObject(auth) || !cJSON_IsString(method) ||
        (strcmp(method->valuestring, "none") != 0 &&
         strcmp(method->valuestring, "private_key_jwt") != 0) ||
        !cJSON_IsObject(tokens)) { status = WF_ERR_PARSE; goto done; }
    status = wf_oauth_json_string(tokens, "iss", 1, &out->issuer);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "sub", 1, &out->subject);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "aud", 1, &out->audience);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "scope", 1, &out->scope);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "access_token", 1,
                                                       &out->access_token);
    if (status == WF_OK) status = wf_oauth_json_string(tokens, "refresh_token", 0,
                                                       &out->refresh_token);
    if (status == WF_OK && strcmp(method->valuestring, "private_key_jwt") == 0)
        status = wf_oauth_json_string(auth, "kid", 1, &out->client_auth_key_id);
    if (status == WF_OK && strcmp(method->valuestring, "none") == 0 &&
        cJSON_GetObjectItemCaseSensitive(auth, "kid")) status = WF_ERR_PARSE;
    if (status == WF_OK) status = wf_oauth_positive_integer(tokens, "expires_at", 0,
                                                            &out->expires_at, NULL);
    type = cJSON_GetObjectItemCaseSensitive(tokens, "token_type");
    if (status == WF_OK && (!cJSON_IsString(type) || strcmp(type->valuestring, "DPoP") != 0))
        status = WF_ERR_PARSE;
    if (status == WF_OK && (!wf_oauth_url_valid(out->issuer, 1, 1, 0) ||
                            !wf_oauth_url_valid(out->audience, 1, 1, 0) ||
                            strcmp(out->subject, expected_subject) != 0)) status = WF_ERR_PARSE;
    if (status == WF_OK) status = wf_oauth_private_jwk_parse(jwk, &out->dpop_key);
done:
    cJSON_Delete(root);
    if (status != WF_OK) wf_oauth_session_state_free(out);
    return status;
}
