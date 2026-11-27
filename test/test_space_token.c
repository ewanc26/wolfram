/*
 * test_space_token.c -- spaces/token.h against test/vectors/spaces/token.json,
 * plus minting and the rejection rules.
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/spaces/token.h"
#include "test.h"
#include "spaces_vectors.h"

static int same(const char *a, const char *b) {
    return (!a && !b) || (a && b && strcmp(a, b) == 0);
}

static wf_space_token_type type_of(const char *name) {
    if (strcmp(name, "delegation") == 0) {
        return WF_SPACE_TOKEN_DELEGATION;
    }
    return strcmp(name, "credential") == 0 ? WF_SPACE_TOKEN_CREDENTIAL
                                           : WF_SPACE_TOKEN_CLIENT_ATTESTATION;
}

static long long vec_int(const cJSON *vec, const char *key) {
    return (long long)cJSON_GetObjectItemCaseSensitive(vec, key)->valuedouble;
}

static void run_vector(const cJSON *vec) {
    wf_space_token token;
    wf_space_token_type type = type_of(vec_str(vec, "type"));
    const char *jwt = vec_str(vec, "jwt");
    const char *key = vec_str(vec, "key");
    long long iat = vec_int(vec, "iat");
    long long exp = vec_int(vec, "exp");
    wf_space_token_type wrong = type == WF_SPACE_TOKEN_DELEGATION
                                    ? WF_SPACE_TOKEN_CREDENTIAL
                                    : WF_SPACE_TOKEN_DELEGATION;
    wf_status status = wf_space_token_parse(type, jwt, &token);

    WF_CHECK(status == WF_OK);
    if (status != WF_OK) {
        fprintf(stderr, "  vector: %s\n", vec_str(vec, "name"));
        return;
    }
    WF_CHECK(same(token.iss, vec_str(vec, "iss")));
    WF_CHECK(same(token.sub, vec_str(vec, "sub")));
    WF_CHECK(same(token.aud, vec_str(vec, "aud")));
    WF_CHECK(same(token.cnf_kid, vec_str(vec, "cnf_kid")));
    WF_CHECK(same(token.jti, vec_str(vec, "jti")));
    WF_CHECK(token.iat == iat && token.exp == exp);

#ifndef HAVE_LIBSECP256K1
    if (strcmp(token.alg, "ES256K") != 0)
#endif
    {
        WF_CHECK(wf_space_token_verify_signature(&token, key) == WF_OK);
        token.signature[10] ^= 1;
        WF_CHECK(wf_space_token_verify_signature(&token, key) ==
                 WF_ERR_VALIDATION);
        token.signature[10] ^= 1;
    }

    WF_CHECK(wf_space_token_check_claims(&token, iat, token.aud, token.sub) ==
             WF_OK);
    WF_CHECK(wf_space_token_check_claims(&token, exp + 4, NULL, NULL) == WF_OK);
    WF_CHECK(wf_space_token_check_claims(&token, exp + 5, NULL, NULL) ==
             WF_ERR_VALIDATION);
    WF_CHECK(wf_space_token_check_claims(&token, iat, "did:web:other#x",
                                         NULL) == WF_ERR_VALIDATION);
    WF_CHECK(wf_space_token_check_claims(&token, iat, NULL, "other") ==
             WF_ERR_VALIDATION);
    if (type == WF_SPACE_TOKEN_CREDENTIAL) {
        WF_CHECK(wf_space_token_check_claims(&token, iat - 6, NULL, NULL) ==
                 WF_ERR_VALIDATION);
    }
    wf_space_token_free(&token);

    /* One type is never accepted as another. */
    WF_CHECK(wf_space_token_parse(wrong, jwt, &token) == WF_ERR_VALIDATION);
    WF_CHECK(token.iss == NULL);
}

/* "b64(header).b64(claims).b64(64 zero bytes)". */
static char *unsigned_jwt(const char *header, const char *claims) {
    unsigned char zero[64] = {0};
    char *a = NULL;
    char *b = NULL;
    char *c = NULL;
    char *jwt = NULL;

    if (wf_crypto_base64url_encode((const unsigned char *)header,
                                   strlen(header), &a) == WF_OK &&
        wf_crypto_base64url_encode((const unsigned char *)claims,
                                   strlen(claims), &b) == WF_OK &&
        wf_crypto_base64url_encode(zero, sizeof(zero), &c) == WF_OK) {
        size_t n = strlen(a) + strlen(b) + strlen(c) + 3;
        jwt = malloc(n);
        if (jwt) {
            snprintf(jwt, n, "%s.%s.%s", a, b, c);
        }
    }
    free(a);
    free(b);
    free(c);
    return jwt;
}

#define SPACE "at:space/did:web:a.example/a.b.c/d"
#define CRED_HEADER                                                            \
    "{\"alg\":\"ES256\",\"typ\":\"atproto-space-credential+jwt\","             \
    "\"kid\":\"#atproto\"}"
#define CNF                                                                    \
    "\"cnf\":{\"kid\":"                                                        \
    "\"did:key:zDnaerESMGLw8NRXkwfGkQvpQ9u2R26RMXChzjkUbp9M1K7iw\"}"
#define CRED(iss, sub, times)                                                  \
    "{\"iss\":\"" iss "\",\"sub\":\"" sub "\"," CNF "," times ",\"jti\":"      \
    "\"j\"}"

static void expect_parse(const char *header, const char *claims,
                         wf_status expected) {
    char *jwt = unsigned_jwt(header, claims);
    wf_space_token token;
    wf_status status =
        wf_space_token_parse(WF_SPACE_TOKEN_CREDENTIAL, jwt, &token);

    WF_CHECK(status == expected);
    if (status != expected) {
        fprintf(stderr, "  claims: %s %s\n", header, claims);
    }
    wf_space_token_free(&token);
    free(jwt);
}

static void test_structure_rules(void) {
    wf_space_token token;

    expect_parse(CRED_HEADER,
                 CRED("did:web:a.example", SPACE, "\"iat\":100,\"exp\":3700"),
                 WF_OK);
    /* Longer than an hour, or no iat. */
    expect_parse(CRED_HEADER,
                 CRED("did:web:a.example", SPACE, "\"iat\":100,\"exp\":3701"),
                 WF_ERR_VALIDATION);
    expect_parse(CRED_HEADER, CRED("did:web:a.example", SPACE, "\"exp\":3700"),
                 WF_ERR_VALIDATION);
    /* Issued by someone other than the space authority. */
    expect_parse(CRED_HEADER,
                 CRED("did:web:b.example", SPACE, "\"iat\":100,\"exp\":200"),
                 WF_ERR_VALIDATION);
    /* A record URI is not a space reference. */
    expect_parse(CRED_HEADER,
                 CRED("did:web:a.example", SPACE "/did:web:a.example/a.b.c/d",
                      "\"iat\":100,\"exp\":200"),
                 WF_ERR_VALIDATION);
    /* Repeated claim, escaped NUL, trailing data, wrong kid or alg, crit. */
    expect_parse(
        CRED_HEADER,
        CRED("did:web:a.example", SPACE, "\"iat\":100,\"exp\":200,\"exp\":300"),
        WF_ERR_VALIDATION);
    expect_parse(CRED_HEADER,
                 CRED("did:web:a.example", SPACE,
                      "\"iat\":100,\"exp\":200,\"x\":\"\\u0000\""),
                 WF_ERR_VALIDATION);
    expect_parse(
        CRED_HEADER,
        CRED("did:web:a.example", SPACE, "\"iat\":100,\"exp\":200") "x",
        WF_ERR_VALIDATION);
    expect_parse("{\"alg\":\"ES256\","
                 "\"typ\":\"atproto-space-credential+jwt\"}",
                 CRED("did:web:a.example", SPACE, "\"iat\":100,\"exp\":200"),
                 WF_ERR_VALIDATION);
    expect_parse("{\"alg\":\"none\","
                 "\"typ\":\"atproto-space-credential+jwt\","
                 "\"kid\":\"#atproto\"}",
                 CRED("did:web:a.example", SPACE, "\"iat\":100,\"exp\":200"),
                 WF_ERR_VALIDATION);
    expect_parse("{\"alg\":\"ES256\","
                 "\"typ\":\"atproto-space-credential+jwt\","
                 "\"kid\":\"#atproto\",\"crit\":[\"x\"]}",
                 CRED("did:web:a.example", SPACE, "\"iat\":100,\"exp\":200"),
                 WF_ERR_VALIDATION);

    WF_CHECK(wf_space_token_parse(WF_SPACE_TOKEN_CREDENTIAL, "a.b", &token) ==
             WF_ERR_VALIDATION);
    WF_CHECK(wf_space_token_parse(WF_SPACE_TOKEN_CREDENTIAL, "a.b.c.d",
                                  &token) == WF_ERR_VALIDATION);
    WF_CHECK(wf_space_token_parse(WF_SPACE_TOKEN_CREDENTIAL, "..", &token) ==
             WF_ERR_VALIDATION);
}

/* Mint, parse, verify. */
static void round_trip(wf_space_token_type type, const char *jwt,
                       const char *did_key, long long lifetime) {
    wf_space_token token;

    WF_CHECK(wf_space_token_parse(type, jwt, &token) == WF_OK);
    WF_CHECK(wf_space_token_verify_signature(&token, did_key) == WF_OK);
    WF_CHECK(wf_space_token_check_claims(&token, 1000, NULL, NULL) == WF_OK);
    WF_CHECK(token.iat == 1000 && token.exp == 1000 + lifetime);
    WF_CHECK(token.jti && strlen(token.jti) == 32);
    wf_space_token_free(&token);
}

static void test_minting(const cJSON *root) {
    const char *space = vec_str(root, "space");
    wf_signing_key key;
    char *did_key = NULL;
    char *audience = NULL;
    char *jwt = NULL;

    WF_CHECK(wf_space_host_audience(space, &audience) == WF_OK);
    WF_CHECK(same(audience, vec_str(root, "host_audience")));
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &key) == WF_OK);
    WF_CHECK(wf_signing_key_public_didkey(&key, &did_key) == WF_OK);

    WF_CHECK(wf_space_delegation_token_create(&key, "did:web:a.example", space,
                                              1000, 0, &jwt) == WF_OK);
    round_trip(WF_SPACE_TOKEN_DELEGATION, jwt, did_key, 60);
    free(jwt);
    WF_CHECK(wf_space_credential_create(&key, space, did_key, 1000, 0, &jwt) ==
             WF_OK);
    round_trip(WF_SPACE_TOKEN_CREDENTIAL, jwt, did_key, 600);
    free(jwt);
    WF_CHECK(wf_space_client_attestation_create(
                 &key, "key-1", "https://app.example/client.json", audience,
                 1000, 30, &jwt) == WF_OK);
    round_trip(WF_SPACE_TOKEN_CLIENT_ATTESTATION, jwt, did_key, 30);
    free(jwt);

    WF_CHECK(wf_space_credential_create(&key, space, did_key, 1000, 3601,
                                        &jwt) == WF_ERR_INVALID_ARG);
    WF_CHECK(jwt == NULL);
    WF_CHECK(wf_space_credential_create(&key, space, "did:key:zx", 1000, 0,
                                        &jwt) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_space_delegation_token_create(&key, "nobody", space, 1000, 0,
                                              &jwt) == WF_ERR_INVALID_ARG);
    free(audience);
    free(did_key);
}

int main(void) {
    cJSON *root = vec_load("token.json");
    const cJSON *item;
    int count = 0;

    WF_CHECK(root != NULL);
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "tokens")) {
        run_vector(item);
        count++;
    }
    WF_CHECK(count == 5);
    test_structure_rules();
    test_minting(root);
    cJSON_Delete(root);
    WF_TEST_SUMMARY();
}
