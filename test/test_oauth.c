#include "test.h"
#include "wolfram/oauth.h"

#include <cJSON.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

#include <stdlib.h>
#include <string.h>

static unsigned char *base64url_decode(const char *encoded, size_t len,
                                       size_t *decoded_len) {
    size_t padded_len = ((len + 3) / 4) * 4;
    char *padded = malloc(padded_len + 1);
    unsigned char *decoded = malloc(padded_len + 1);
    int result;
    size_t i, padding;
    if (!padded || !decoded) {
        free(padded);
        free(decoded);
        return NULL;
    }
    for (i = 0; i < len; i++) {
        padded[i] = encoded[i] == '-' ? '+' : encoded[i] == '_' ? '/' : encoded[i];
    }
    for (; i < padded_len; i++) padded[i] = '=';
    padded[padded_len] = '\0';
    result = EVP_DecodeBlock(decoded, (const unsigned char *)padded, (int)padded_len);
    padding = padded_len - len;
    free(padded);
    if (result < 0 || (size_t)result < padding) {
        free(decoded);
        return NULL;
    }
    *decoded_len = (size_t)result - padding;
    return decoded;
}

static void test_pkce(void) {
    static const char verifier[] =
        "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    wf_oauth_pkce pkce;
    WF_CHECK(wf_oauth_pkce_from_verifier(verifier, &pkce) == WF_OK);
    WF_CHECK(strcmp(pkce.verifier, verifier) == 0);
    WF_CHECK(strcmp(pkce.challenge,
                    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM") == 0);
    WF_CHECK(wf_oauth_pkce_from_verifier("too-short", &pkce) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_oauth_pkce_generate(&pkce) == WF_OK);
    WF_CHECK(strlen(pkce.verifier) == 43);
    WF_CHECK(strlen(pkce.challenge) == 43);
}

static const char resource_json[] =
    "{\"resource\":\"https://pds.example\","
    "\"authorization_servers\":[\"https://auth.example\"],"
    "\"scopes_supported\":[\"atproto\"]}";

static const char server_json[] =
    "{"
    "\"issuer\":\"https://auth.example\","
    "\"authorization_endpoint\":\"https://auth.example/authorize\","
    "\"token_endpoint\":\"https://auth.example/token\","
    "\"pushed_authorization_request_endpoint\":\"https://auth.example/par\","
    "\"response_types_supported\":[\"code\"],"
    "\"grant_types_supported\":[\"authorization_code\",\"refresh_token\"],"
    "\"code_challenge_methods_supported\":[\"S256\"],"
    "\"token_endpoint_auth_methods_supported\":[\"none\",\"private_key_jwt\"],"
    "\"token_endpoint_auth_signing_alg_values_supported\":[\"ES256\"],"
    "\"scopes_supported\":[\"atproto\",\"transition:generic\"],"
    "\"dpop_signing_alg_values_supported\":[\"ES256\"],"
    "\"protected_resources\":[\"https://pds.example\"],"
    "\"authorization_response_iss_parameter_supported\":true,"
    "\"require_pushed_authorization_requests\":true,"
    "\"client_id_metadata_document_supported\":true"
    "}";

static void test_metadata(void) {
    wf_oauth_resource_metadata resource = {0};
    wf_oauth_server_metadata server = {0};
    wf_oauth_client_metadata client = {0};
    wf_oauth_client_metadata private_client = {0};
    wf_oauth_dpop_key *auth_key = NULL;
    static const char client_json[] =
        "{"
        "\"client_id\":\"https://app.example/oauth-client-metadata.json\","
        "\"client_name\":\"Example\","
        "\"client_uri\":\"https://app.example/\","
        "\"redirect_uris\":[\"https://app.example/callback\"],"
        "\"response_types\":[\"code\"],"
        "\"grant_types\":[\"authorization_code\",\"refresh_token\"],"
        "\"scope\":\"atproto transition:generic\","
        "\"token_endpoint_auth_method\":\"none\","
        "\"application_type\":\"web\","
        "\"dpop_bound_access_tokens\":true"
        "}";
    static const char bad_server_json[] =
        "{\"issuer\":\"https://auth.example\"}";
    static const char private_client_json[] =
        "{"
        "\"client_id\":\"https://app.example/oauth-client-metadata.json\","
        "\"client_name\":\"Example\","
        "\"client_uri\":\"https://app.example/\","
        "\"redirect_uris\":[\"https://app.example/callback\"],"
        "\"response_types\":[\"code\"],"
        "\"grant_types\":[\"authorization_code\",\"refresh_token\"],"
        "\"scope\":\"atproto\","
        "\"token_endpoint_auth_method\":\"private_key_jwt\","
        "\"token_endpoint_auth_signing_alg\":\"ES256\","
        "\"jwks\":{\"keys\":[{\"kty\":\"EC\",\"crv\":\"P-256\","
        "\"kid\":\"auth-key-1\",\"x\":\"x\",\"y\":\"y\"}]},"
        "\"application_type\":\"web\",\"dpop_bound_access_tokens\":true}"
        ;

    WF_CHECK(wf_oauth_resource_metadata_parse(
                 resource_json, strlen(resource_json), "https://pds.example",
                 &resource) == WF_OK);
    WF_CHECK(resource.authorization_servers.count == 1);
    WF_CHECK(strcmp(resource.authorization_servers.items[0],
                    "https://auth.example") == 0);
    wf_oauth_resource_metadata_free(&resource);

    WF_CHECK(wf_oauth_resource_metadata_parse(
                 resource_json, strlen(resource_json), "https://wrong.example",
                 &resource) == WF_ERR_PARSE);

    WF_CHECK(wf_oauth_server_metadata_parse(
                 server_json, strlen(server_json), "https://auth.example",
                 &server) == WF_OK);
    WF_CHECK(strcmp(server.pushed_authorization_request_endpoint,
                    "https://auth.example/par") == 0);
    WF_CHECK(server.protected_resources.count == 1);
    wf_oauth_server_metadata_free(&server);
    WF_CHECK(wf_oauth_server_metadata_parse(
                 bad_server_json, strlen(bad_server_json), NULL,
                 &server) == WF_ERR_PARSE);

    WF_CHECK(wf_oauth_client_metadata_parse(
                 client_json, strlen(client_json),
                 "https://app.example/oauth-client-metadata.json",
                 &client) == WF_OK);
    WF_CHECK(client.dpop_bound_access_tokens == 1);
    WF_CHECK(client.redirect_uris.count == 1);
    wf_oauth_client_metadata_free(&client);
    WF_CHECK(wf_oauth_client_metadata_parse(
                 client_json, strlen(client_json),
                 "https://different.example/oauth-client-metadata.json",
                 &client) == WF_ERR_PARSE);

    WF_CHECK(wf_oauth_client_metadata_parse(private_client_json,
        strlen(private_client_json),
        "https://app.example/oauth-client-metadata.json",
        &private_client) == WF_OK);
    WF_CHECK(wf_oauth_dpop_key_generate(&auth_key) == WF_OK);
    if (auth_key) {
        wf_oauth_client_auth auth = {
            .client_id = "https://app.example/oauth-client-metadata.json",
            .authorization_server_issuer = "https://auth.example",
            .signing_key = auth_key,
            .key_id = "auth-key-1",
        };
        WF_CHECK(wf_oauth_client_auth_validate(
            &server, &private_client, &auth) == WF_ERR_INVALID_ARG);
        /* Reparse server after the earlier owned instance was freed. */
        WF_CHECK(wf_oauth_server_metadata_parse(server_json, strlen(server_json),
            "https://auth.example", &server) == WF_OK);
        WF_CHECK(wf_oauth_client_auth_validate(
            &server, &private_client, &auth) == WF_OK);
        auth.key_id = NULL;
        WF_CHECK(wf_oauth_client_auth_validate(
            &server, &private_client, &auth) == WF_ERR_PARSE);
        auth.key_id = "unadvertised-key";
        WF_CHECK(wf_oauth_client_auth_validate(
            &server, &private_client, &auth) == WF_ERR_PARSE);
        wf_oauth_server_metadata_free(&server);
    }
    wf_oauth_dpop_key_free(auth_key);
    wf_oauth_client_metadata_free(&private_client);
}

static void test_endpoint_responses(void) {
    wf_oauth_par_response par = {0};
    wf_oauth_token_response token = {0};
    static const char par_json[] =
        "{\"request_uri\":\"urn:ietf:params:oauth:request_uri:abc\",\"expires_in\":300}";
    static const char token_json[] =
        "{\"access_token\":\"access\",\"token_type\":\"DPoP\","
        "\"sub\":\"did:plc:alice\",\"scope\":\"atproto\","
        "\"refresh_token\":\"refresh\",\"expires_in\":3600}";
    WF_CHECK(wf_oauth_par_response_parse(par_json, strlen(par_json), &par) == WF_OK);
    WF_CHECK(par.expires_in == 300);
    wf_oauth_par_response_free(&par);
    WF_CHECK(wf_oauth_par_response_parse("{\"expires_in\":0}", 16, &par) == WF_ERR_PARSE);
    WF_CHECK(wf_oauth_token_response_parse(token_json, strlen(token_json), &token) == WF_OK);
    WF_CHECK(strcmp(token.sub, "did:plc:alice") == 0);
    WF_CHECK(token.expires_in_present && token.expires_in == 3600);
    WF_CHECK(wf_oauth_token_response_validate_subject(
        &token, "did:plc:alice") == WF_OK);
    WF_CHECK(wf_oauth_token_response_validate_subject(
        &token, "did:plc:mallory") == WF_ERR_PARSE);
    wf_oauth_token_response_free(&token);
    WF_CHECK(wf_oauth_token_response_parse(
        "{\"access_token\":\"x\",\"token_type\":\"Bearer\",\"sub\":\"did:plc:x\",\"scope\":\"atproto\"}",
        85, &token) == WF_ERR_PARSE);
}

static void test_callback(void) {
    wf_oauth_callback_result result = {0};
    wf_oauth_callback_params success = {
        .state = "one-time-state",
        .code = "authorization-code",
        .issuer = "https://auth.example",
    };
    wf_oauth_callback_params denied = {
        .state = "one-time-state",
        .issuer = "https://auth.example",
        .error = "access_denied",
        .error_description = "User cancelled",
    };
    WF_CHECK(wf_oauth_callback_validate(&success, "one-time-state",
        "https://auth.example", 1, &result) == WF_OK);
    WF_CHECK(strcmp(result.code, "authorization-code") == 0);
    wf_oauth_callback_result_free(&result);
    WF_CHECK(wf_oauth_callback_validate(&denied, "one-time-state",
        "https://auth.example", 1, &result) == WF_OK);
    WF_CHECK(strcmp(result.error, "access_denied") == 0);
    wf_oauth_callback_result_free(&result);

    success.state = "replayed-or-unknown";
    WF_CHECK(wf_oauth_callback_validate(&success, "one-time-state",
        "https://auth.example", 1, &result) == WF_ERR_PARSE);
    success.state = "one-time-state";
    success.issuer = "https://attacker.example";
    WF_CHECK(wf_oauth_callback_validate(&success, "one-time-state",
        "https://auth.example", 1, &result) == WF_ERR_PARSE);
    success.issuer = NULL;
    WF_CHECK(wf_oauth_callback_validate(&success, "one-time-state",
        "https://auth.example", 1, &result) == WF_ERR_PARSE);
    WF_CHECK(wf_oauth_callback_validate(&success, "one-time-state",
        "https://auth.example", 0, &result) == WF_OK);
    wf_oauth_callback_result_free(&result);
    success.response = "unsupported-jarm";
    WF_CHECK(wf_oauth_callback_validate(&success, "one-time-state",
        "https://auth.example", 0, &result) == WF_ERR_PARSE);
}

static void test_authorization_url(void) {
    char *url = NULL;
    WF_CHECK(wf_oauth_authorization_url_create(
        "https://auth.example/authorize", "https://app.example/client.json",
        "urn:ietf:params:oauth:request_uri:a/b+c", &url) == WF_OK);
    WF_CHECK(url != NULL);
    if (url) {
        WF_CHECK(strcmp(url,
            "https://auth.example/authorize?client_id=https%3A%2F%2Fapp.example%2Fclient.json&request_uri=urn%3Aietf%3Aparams%3Aoauth%3Arequest_uri%3Aa%2Fb%2Bc") == 0);
    }
    wf_oauth_string_free(url);
}

static void test_authorization_state(void) {
    unsigned char private_key[32] = {0};
    wf_oauth_dpop_key *key = NULL;
    wf_oauth_pkce pkce;
    wf_oauth_authorization_state state = {0}, restored = {0};
    char *json = NULL, *tampered = NULL, *x;
    private_key[31] = 1;
    WF_CHECK(wf_oauth_dpop_key_import(private_key, &key) == WF_OK);
    WF_CHECK(wf_oauth_pkce_from_verifier(
        "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", &pkce) == WF_OK);
    WF_CHECK(wf_oauth_authorization_state_create(
        "https://auth.example", &pkce, key, "return-to-settings",
        "auth-key-1", 1700000000, 600, &state) == WF_OK);
    WF_CHECK(state.dpop_key != key);
    WF_CHECK(state.expires_at == 1700000600);
    WF_CHECK(wf_oauth_authorization_state_serialize(&state, &json) == WF_OK);
    WF_CHECK(json != NULL);
    if (json) {
        WF_CHECK(wf_oauth_authorization_state_parse(
            json, strlen(json), 1700000300, &restored) == WF_OK);
        WF_CHECK(strcmp(restored.issuer, "https://auth.example") == 0);
        WF_CHECK(strcmp(restored.app_state, "return-to-settings") == 0);
        WF_CHECK(strcmp(restored.client_auth_key_id, "auth-key-1") == 0);
        wf_oauth_authorization_state_free(&restored);
        WF_CHECK(wf_oauth_authorization_state_parse(
            json, strlen(json), 1700000600, &restored) == WF_ERR_PARSE);

        tampered = malloc(strlen(json) + 1);
        WF_CHECK(tampered != NULL);
        if (tampered) {
            strcpy(tampered, json);
            x = strstr(tampered, "\"x\":\"");
            WF_CHECK(x != NULL);
            if (x) x[5] = x[5] == 'A' ? 'B' : 'A';
            WF_CHECK(wf_oauth_authorization_state_parse(
                tampered, strlen(tampered), 1700000300, &restored) == WF_ERR_PARSE);
        }
    }
    free(tampered);
    wf_oauth_string_free(json);
    wf_oauth_authorization_state_free(&state);
    wf_oauth_dpop_key_free(key);
}

static void test_session_state(void) {
    unsigned char private_key[32] = {0};
    wf_oauth_dpop_key *key = NULL;
    wf_oauth_token_response token = {
        .access_token = "access",
        .token_type = "DPoP",
        .sub = "did:plc:alice",
        .scope = "atproto transition:generic",
        .refresh_token = "refresh",
        .expires_in = 3600,
        .expires_in_present = 1,
    };
    wf_oauth_session_state session = {0}, restored = {0};
    char *json = NULL, *tampered = NULL, *sub;
    private_key[31] = 1;
    WF_CHECK(wf_oauth_dpop_key_import(private_key, &key) == WF_OK);
    WF_CHECK(wf_oauth_session_state_create(
        "https://auth.example", "https://pds.example", key, &token,
        "auth-key-1", 1700000000, &session) == WF_OK);
    WF_CHECK(session.expires_at == 1700003600);
    WF_CHECK(session.dpop_key != key);
    WF_CHECK(wf_oauth_session_state_serialize(&session, &json) == WF_OK);
    WF_CHECK(json != NULL);
    if (json) {
        WF_CHECK(wf_oauth_session_state_parse(
            json, strlen(json), "did:plc:alice", &restored) == WF_OK);
        WF_CHECK(strcmp(restored.subject, "did:plc:alice") == 0);
        WF_CHECK(strcmp(restored.refresh_token, "refresh") == 0);
        WF_CHECK(strcmp(restored.client_auth_key_id, "auth-key-1") == 0);
        WF_CHECK(restored.expires_at == 1700003600);
        wf_oauth_session_state_free(&restored);

        tampered = malloc(strlen(json) + 1);
        WF_CHECK(tampered != NULL);
        if (tampered) {
            strcpy(tampered, json);
            sub = strstr(tampered, "did:plc:alice");
            WF_CHECK(sub != NULL);
            if (sub) sub[8] = 'm';
            WF_CHECK(wf_oauth_session_state_parse(
                tampered, strlen(tampered), "did:plc:alice",
                &restored) == WF_ERR_PARSE);
        }
    }
    free(tampered);
    wf_oauth_string_free(json);
    wf_oauth_session_state_free(&session);
    wf_oauth_dpop_key_free(key);
}

static void test_dpop(void) {
    unsigned char private_key[32] = {0};
    unsigned char exported[32] = {0};
    wf_oauth_dpop_key *key = NULL;
    wf_oauth_dpop_key *generated_key = NULL;
    wf_oauth_dpop_proof_options options = {
        .http_method = "POST",
        .http_uri = "https://auth.example/token?ignored=yes#fragment",
        .nonce = "server-nonce",
        .access_token = "access-token",
        .jti = "test-jti",
        .issued_at = 1700000000,
    };
    char thumbprint[44];
    char *jwt = NULL, *first_dot, *second_dot;
    unsigned char *header_bytes = NULL, *payload_bytes = NULL, *sig_bytes = NULL;
    size_t header_len = 0, payload_len = 0, sig_len = 0;
    cJSON *header = NULL, *payload = NULL;
    EC_KEY *verify_key = NULL;
    ECDSA_SIG *signature = NULL;
    BIGNUM *r = NULL, *s = NULL;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    const cJSON *field;

    private_key[31] = 1; /* P-256 generator, giving deterministic public JWK. */
    WF_CHECK(wf_oauth_dpop_key_generate(&generated_key) == WF_OK);
    wf_oauth_dpop_key_free(generated_key);
    WF_CHECK(wf_oauth_dpop_key_import(private_key, &key) == WF_OK);
    WF_CHECK(wf_oauth_dpop_key_export(key, exported) == WF_OK);
    WF_CHECK(memcmp(private_key, exported, sizeof(private_key)) == 0);
    WF_CHECK(wf_oauth_dpop_key_thumbprint(key, thumbprint) == WF_OK);
    WF_CHECK(strcmp(thumbprint, "xx0BcA-wMohw8atYDJOe6peGModklG2wRHBlXHMvl0M") == 0);
    WF_CHECK(wf_oauth_dpop_proof_create(key, &options, &jwt) == WF_OK);
    WF_CHECK(jwt != NULL);
    if (!jwt) {
        wf_oauth_dpop_key_free(key);
        return;
    }

    first_dot = strchr(jwt, '.');
    second_dot = first_dot ? strchr(first_dot + 1, '.') : NULL;
    WF_CHECK(first_dot != NULL && second_dot != NULL && strchr(second_dot + 1, '.') == NULL);
    if (!first_dot || !second_dot) goto done;
    header_bytes = base64url_decode(jwt, (size_t)(first_dot - jwt), &header_len);
    payload_bytes = base64url_decode(first_dot + 1,
                                     (size_t)(second_dot - first_dot - 1),
                                     &payload_len);
    sig_bytes = base64url_decode(second_dot + 1, strlen(second_dot + 1), &sig_len);
    WF_CHECK(header_bytes != NULL && payload_bytes != NULL && sig_bytes != NULL);
    WF_CHECK(sig_len == 64);
    if (!header_bytes || !payload_bytes || !sig_bytes || sig_len != 64) goto done;
    header = cJSON_ParseWithLength((const char *)header_bytes, header_len);
    payload = cJSON_ParseWithLength((const char *)payload_bytes, payload_len);
    WF_CHECK(header != NULL && payload != NULL);
    field = header ? cJSON_GetObjectItemCaseSensitive(header, "typ") : NULL;
    WF_CHECK(cJSON_IsString(field) && strcmp(field->valuestring, "dpop+jwt") == 0);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "htu") : NULL;
    WF_CHECK(cJSON_IsString(field) &&
             strcmp(field->valuestring, "https://auth.example/token") == 0);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "ath") : NULL;
    WF_CHECK(cJSON_IsString(field) &&
             strcmp(field->valuestring, "Pxa-1wifRlPl7yG_0oJNfzqq7MelmOfonFgOFgapzFI") == 0);

    verify_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    WF_CHECK(verify_key != NULL);
    /* Recreate the public key from scalar 1 without reaching through opaque API. */
    if (verify_key) {
        const EC_GROUP *group = EC_KEY_get0_group(verify_key);
        EC_POINT *point = EC_POINT_new(group);
        BIGNUM *one = BN_new();
        if (point && one && BN_one(one) == 1 &&
            EC_POINT_mul(group, point, one, NULL, NULL, NULL) == 1) {
            WF_CHECK(EC_KEY_set_public_key(verify_key, point) == 1);
        }
        EC_POINT_free(point);
        BN_free(one);
    }
    r = BN_bin2bn(sig_bytes, 32, NULL);
    s = BN_bin2bn(sig_bytes + 32, 32, NULL);
    signature = ECDSA_SIG_new();
    if (signature && r && s && ECDSA_SIG_set0(signature, r, s) == 1) {
        r = NULL;
        s = NULL;
        SHA256((const unsigned char *)jwt, (size_t)(second_dot - jwt), digest);
        WF_CHECK(ECDSA_do_verify(digest, sizeof(digest), signature, verify_key) == 1);
    } else {
        WF_CHECK(0);
    }

done:
    cJSON_Delete(header);
    cJSON_Delete(payload);
    free(header_bytes);
    free(payload_bytes);
    free(sig_bytes);
    ECDSA_SIG_free(signature);
    BN_free(r);
    BN_free(s);
    EC_KEY_free(verify_key);
    wf_oauth_string_free(jwt);
    wf_oauth_dpop_key_free(key);
}

static void test_client_assertion(void) {
    unsigned char private_key[32] = {0};
    wf_oauth_dpop_key *key = NULL;
    wf_oauth_client_assertion_options options = {
        .client_id = "https://app.example/oauth-client-metadata.json",
        .authorization_server_issuer = "https://auth.example",
        .key_id = "auth-key-1",
        .jti = "assertion-jti",
        .issued_at = 1700000000,
    };
    char *jwt = NULL, *first_dot, *second_dot;
    unsigned char *header_bytes = NULL, *payload_bytes = NULL, *sig_bytes = NULL;
    size_t header_len = 0, payload_len = 0, sig_len = 0;
    cJSON *header = NULL, *payload = NULL;
    const cJSON *field;
    EC_KEY *verify_key = NULL;
    ECDSA_SIG *signature = NULL;
    BIGNUM *r = NULL, *s = NULL;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    private_key[31] = 1;
    WF_CHECK(wf_oauth_dpop_key_import(private_key, &key) == WF_OK);
    WF_CHECK(wf_oauth_client_assertion_create(key, &options, &jwt) == WF_OK);
    WF_CHECK(jwt != NULL);
    if (!jwt) goto done;
    first_dot = strchr(jwt, '.');
    second_dot = first_dot ? strchr(first_dot + 1, '.') : NULL;
    WF_CHECK(first_dot && second_dot && !strchr(second_dot + 1, '.'));
    if (!first_dot || !second_dot) goto done;
    header_bytes = base64url_decode(jwt, (size_t)(first_dot - jwt), &header_len);
    payload_bytes = base64url_decode(first_dot + 1,
        (size_t)(second_dot - first_dot - 1), &payload_len);
    sig_bytes = base64url_decode(second_dot + 1, strlen(second_dot + 1), &sig_len);
    header = header_bytes ? cJSON_ParseWithLength((char *)header_bytes, header_len) : NULL;
    payload = payload_bytes ? cJSON_ParseWithLength((char *)payload_bytes, payload_len) : NULL;
    WF_CHECK(header && payload && sig_bytes && sig_len == 64);
    field = header ? cJSON_GetObjectItemCaseSensitive(header, "alg") : NULL;
    WF_CHECK(cJSON_IsString(field) && strcmp(field->valuestring, "ES256") == 0);
    field = header ? cJSON_GetObjectItemCaseSensitive(header, "kid") : NULL;
    WF_CHECK(cJSON_IsString(field) && strcmp(field->valuestring, "auth-key-1") == 0);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "iss") : NULL;
    WF_CHECK(cJSON_IsString(field) && strcmp(field->valuestring, options.client_id) == 0);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "sub") : NULL;
    WF_CHECK(cJSON_IsString(field) && strcmp(field->valuestring, options.client_id) == 0);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "aud") : NULL;
    WF_CHECK(cJSON_IsString(field) &&
             strcmp(field->valuestring, options.authorization_server_issuer) == 0);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "jti") : NULL;
    WF_CHECK(cJSON_IsString(field) && strcmp(field->valuestring, "assertion-jti") == 0);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "iat") : NULL;
    WF_CHECK(cJSON_IsNumber(field) && field->valuedouble == 1700000000);
    field = payload ? cJSON_GetObjectItemCaseSensitive(payload, "exp") : NULL;
    WF_CHECK(cJSON_IsNumber(field) && field->valuedouble == 1700000060);

    verify_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (verify_key && sig_bytes && sig_len == 64) {
        const EC_GROUP *group = EC_KEY_get0_group(verify_key);
        EC_POINT *point = EC_POINT_new(group);
        BIGNUM *one = BN_new();
        if (point && one && BN_one(one) == 1 &&
            EC_POINT_mul(group, point, one, NULL, NULL, NULL) == 1)
            WF_CHECK(EC_KEY_set_public_key(verify_key, point) == 1);
        EC_POINT_free(point); BN_free(one);
        r = BN_bin2bn(sig_bytes, 32, NULL); s = BN_bin2bn(sig_bytes + 32, 32, NULL);
        signature = ECDSA_SIG_new();
        if (signature && r && s && ECDSA_SIG_set0(signature, r, s) == 1) {
            r = NULL; s = NULL;
            SHA256((unsigned char *)jwt, (size_t)(second_dot - jwt), digest);
            WF_CHECK(ECDSA_do_verify(digest, sizeof(digest), signature, verify_key) == 1);
        } else WF_CHECK(0);
    } else WF_CHECK(0);
done:
    cJSON_Delete(header); cJSON_Delete(payload);
    free(header_bytes); free(payload_bytes); free(sig_bytes);
    ECDSA_SIG_free(signature); BN_free(r); BN_free(s); EC_KEY_free(verify_key);
    wf_oauth_string_free(jwt); wf_oauth_dpop_key_free(key);
}

int main(void) {
    test_pkce();
    test_metadata();
    test_endpoint_responses();
    test_callback();
    test_authorization_url();
    test_authorization_state();
    test_session_state();
    test_dpop();
    test_client_assertion();
    WF_TEST_SUMMARY();
}
