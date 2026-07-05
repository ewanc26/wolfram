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
    wf_oauth_token_response_free(&token);
    WF_CHECK(wf_oauth_token_response_parse(
        "{\"access_token\":\"x\",\"token_type\":\"Bearer\",\"sub\":\"did:plc:x\",\"scope\":\"atproto\"}",
        85, &token) == WF_ERR_PARSE);
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

int main(void) {
    test_pkce();
    test_metadata();
    test_endpoint_responses();
    test_dpop();
    WF_TEST_SUMMARY();
}
