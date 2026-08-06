#include "internal.h"

#include "../../crypto/crypto_internal.h"

#include <curl/curl.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct wf_oauth_dpop_key {
    EVP_PKEY *pkey;
};

static wf_status wf_oauth_dpop_key_wrap(EVP_PKEY *pkey,
                                        wf_oauth_dpop_key **out) {
    wf_oauth_dpop_key *key;
    if (!pkey || !out) return WF_ERR_INVALID_ARG;
    key = calloc(1, sizeof(*key));
    if (!key) {
        EVP_PKEY_free(pkey);
        return WF_ERR_ALLOC;
    }
    key->pkey = pkey;
    *out = key;
    return WF_OK;
}

wf_status wf_oauth_dpop_key_generate(wf_oauth_dpop_key **out) {
    EVP_PKEY *pkey;
    if (!out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    pkey = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    if (!pkey) return WF_ERR_ALLOC;
    return wf_oauth_dpop_key_wrap(pkey, out);
}

wf_status wf_oauth_dpop_key_import(const unsigned char private_key[32],
                                   wf_oauth_dpop_key **out) {
    EC_GROUP *group = NULL;
    BIGNUM *scalar = NULL, *order = NULL;
    EVP_PKEY *pkey = NULL;
    wf_status status = WF_ERR_PARSE;
    if (!private_key || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    scalar = BN_bin2bn(private_key, 32, NULL);
    order = BN_new();
    if (!group || !scalar || !order) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    /* Reject a scalar outside the valid private-key range before it is ever
     * handed to key construction -- this is the untrusted-input validation
     * EC_KEY_check_key used to perform as part of building the legacy
     * EC_KEY object; wf_p256_pkey_from_private itself does not re-derive
     * or re-validate the scalar, so callers importing external/untrusted
     * bytes must do this check themselves. */
    if (EC_GROUP_get_order(group, order, NULL) != 1 || BN_is_zero(scalar) ||
        BN_is_negative(scalar) || BN_cmp(scalar, order) >= 0) {
        goto done;
    }
    status = wf_p256_pkey_from_private(private_key, &pkey);
    if (status != WF_OK) goto done;
    status = wf_oauth_dpop_key_wrap(pkey, out);
    if (status == WF_OK) pkey = NULL;
done:
    EVP_PKEY_free(pkey);
    BN_clear_free(scalar);
    BN_free(order);
    EC_GROUP_free(group);
    return status;
}

wf_status wf_oauth_dpop_key_export(const wf_oauth_dpop_key *key,
                                   unsigned char private_key_out[32]) {
    BIGNUM *scalar = NULL;
    wf_status status;
    if (!key || !key->pkey || !private_key_out) return WF_ERR_INVALID_ARG;
    if (EVP_PKEY_get_bn_param(key->pkey, OSSL_PKEY_PARAM_PRIV_KEY, &scalar) !=
            1 ||
        !scalar || BN_bn2binpad(scalar, private_key_out, 32) != 32) {
        status = WF_ERR_PARSE;
    } else {
        status = WF_OK;
    }
    BN_free(scalar);
    return status;
}

void wf_oauth_dpop_key_free(wf_oauth_dpop_key *key) {
    if (!key) return;
    EVP_PKEY_free(key->pkey);
    free(key);
}

wf_status wf_oauth_dpop_jwk_json(const wf_oauth_dpop_key *key, char **out_jwk) {
    cJSON *jwk = NULL;
    char *x = NULL, *y = NULL, *json = NULL;
    wf_status status;
    if (!key || !out_jwk) return WF_ERR_INVALID_ARG;
    *out_jwk = NULL;
    status = wf_oauth_dpop_jwk(key, &jwk, &x, &y);
    if (status != WF_OK) goto done;
    json = cJSON_PrintUnformatted(jwk);
    if (!json) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    *out_jwk = json;
    json = NULL;
    status = WF_OK;
done:
    cJSON_Delete(jwk);
    free(x);
    free(y);
    free(json);
    return status;
}

wf_status wf_oauth_dpop_coordinates(const wf_oauth_dpop_key *key,
                                    unsigned char x[32], unsigned char y[32]) {
    unsigned char point_oct[65];
    size_t point_oct_len = 0;
    if (!key || !key->pkey) return WF_ERR_INVALID_ARG;
    /* The public key as a SEC1 octet string is 0x04 || X(32) || Y(32) for
     * an uncompressed P-256 point -- EVP_PKEY's own encoding, not a
     * hand-rolled one. */
    if (EVP_PKEY_get_octet_string_param(key->pkey, OSSL_PKEY_PARAM_PUB_KEY,
                                        point_oct, sizeof(point_oct),
                                        &point_oct_len) != 1 ||
        point_oct_len != sizeof(point_oct) || point_oct[0] != 0x04) {
        return WF_ERR_PARSE;
    }
    memcpy(x, point_oct + 1, 32);
    memcpy(y, point_oct + 33, 32);
    return WF_OK;
}

wf_status wf_oauth_dpop_jwk(const wf_oauth_dpop_key *key, cJSON **out,
                            char **x_out, char **y_out) {
    unsigned char x[32], y[32];
    cJSON *jwk = NULL;
    wf_status status;
    *out = NULL;
    *x_out = NULL;
    *y_out = NULL;
    status = wf_oauth_dpop_coordinates(key, x, y);
    if (status != WF_OK) return status;
    status = wf_oauth_base64url(x, sizeof(x), x_out);
    if (status == WF_OK) status = wf_oauth_base64url(y, sizeof(y), y_out);
    if (status != WF_OK) goto done;
    jwk = cJSON_CreateObject();
    if (!jwk || !cJSON_AddStringToObject(jwk, "kty", "EC") ||
        !cJSON_AddStringToObject(jwk, "crv", "P-256") ||
        !cJSON_AddStringToObject(jwk, "x", *x_out) ||
        !cJSON_AddStringToObject(jwk, "y", *y_out)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    *out = jwk;
    return WF_OK;
done:
    cJSON_Delete(jwk);
    free(*x_out);
    free(*y_out);
    *x_out = NULL;
    *y_out = NULL;
    return status;
}

wf_status wf_oauth_dpop_key_thumbprint(const wf_oauth_dpop_key *key,
                                       char thumbprint_out[44]) {
    cJSON *unused = NULL;
    char *x = NULL, *y = NULL, *canonical = NULL, *encoded = NULL;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    size_t needed;
    wf_status status;
    if (!key || !thumbprint_out) return WF_ERR_INVALID_ARG;
    status = wf_oauth_dpop_jwk(key, &unused, &x, &y);
    if (status != WF_OK) return status;
    needed =
        strlen(x) + strlen(y) +
        strlen("{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"\",\"y\":\"\"}") + 1;
    canonical = malloc(needed);
    if (!canonical) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(canonical, needed,
             "{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"%s\",\"y\":\"%s\"}", x,
             y);
    SHA256((const unsigned char *)canonical, strlen(canonical), digest);
    status = wf_oauth_base64url(digest, sizeof(digest), &encoded);
    if (status == WF_OK && strlen(encoded) != 43) status = WF_ERR_PARSE;
    if (status == WF_OK) memcpy(thumbprint_out, encoded, 44);
done:
    cJSON_Delete(unused);
    free(x);
    free(y);
    free(canonical);
    free(encoded);
    return status;
}

static char *wf_oauth_htu(const char *uri) {
    CURLU *parsed;
    char *scheme = NULL, *host = NULL, *credentials = NULL;
    const char *query, *fragment, *end;
    size_t len;
    char *htu;
    if (!uri) return NULL;
    parsed = curl_url();
    if (!parsed) return NULL;
    if (curl_url_set(parsed, CURLUPART_URL, uri, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        (strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0))
        goto invalid;
    if (curl_url_get(parsed, CURLUPART_USER, &credentials, 0) == CURLUE_OK)
        goto invalid;
    if (curl_url_get(parsed, CURLUPART_PASSWORD, &credentials, 0) == CURLUE_OK)
        goto invalid;
    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(parsed);
    query = strchr(uri, '?');
    fragment = strchr(uri, '#');
    end = uri + strlen(uri);
    if (query && query < end) end = query;
    if (fragment && fragment < end) end = fragment;
    len = (size_t)(end - uri);
    htu = malloc(len + 1);
    if (!htu) return NULL;
    memcpy(htu, uri, len);
    htu[len] = '\0';
    return htu;
invalid:
    curl_free(scheme);
    curl_free(host);
    curl_free(credentials);
    curl_url_cleanup(parsed);
    return NULL;
}

static int wf_oauth_http_method_valid(const char *method) {
    const unsigned char *cursor = (const unsigned char *)method;
    if (!cursor || !*cursor) return 0;
    while (*cursor) {
        if (!isupper(*cursor) && !isdigit(*cursor) &&
            strchr("!#$%&'*+-.^_`|~", *cursor) == NULL) {
            return 0;
        }
        cursor++;
    }
    return 1;
}

static wf_status wf_oauth_es256_sign(const wf_oauth_dpop_key *key,
                                     const unsigned char *input,
                                     size_t input_len,
                                     unsigned char signature[64]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    if (!key || !key->pkey || !input || !signature) return WF_ERR_INVALID_ARG;
    SHA256(input, input_len, digest);
    return wf_p256_sign_hash(key->pkey, digest, signature);
}

wf_status wf_oauth_dpop_proof_create(const wf_oauth_dpop_key *key,
                                     const wf_oauth_dpop_proof_options *options,
                                     char **jwt_out) {
    cJSON *header = NULL, *payload = NULL, *jwk = NULL;
    char *x = NULL, *y = NULL, *header_json = NULL, *payload_json = NULL;
    char *header_b64 = NULL, *payload_b64 = NULL, *signature_b64 = NULL;
    char *htu = NULL, *jti = NULL, *ath = NULL, *signing_input = NULL,
         *jwt = NULL;
    unsigned char signature[64], digest[SHA256_DIGEST_LENGTH];
    size_t input_len, jwt_len;
    int64_t issued_at;
    wf_status status;
    if (!key || !options || !jwt_out ||
        !wf_oauth_http_method_valid(options->http_method) ||
        !options->http_uri) {
        return WF_ERR_INVALID_ARG;
    }
    *jwt_out = NULL;
    htu = wf_oauth_htu(options->http_uri);
    if (!htu) return WF_ERR_INVALID_ARG;
    if (options->jti) {
        if (!*options->jti) {
            status = WF_ERR_INVALID_ARG;
            goto done;
        }
        jti = wf_oauth_strdup(options->jti);
        status = jti ? WF_OK : WF_ERR_ALLOC;
    } else {
        status = wf_oauth_random_jti(&jti);
    }
    if (status != WF_OK) goto done;
    status = wf_oauth_dpop_jwk(key, &jwk, &x, &y);
    if (status != WF_OK) goto done;
    header = cJSON_CreateObject();
    payload = cJSON_CreateObject();
    if (!header || !payload ||
        !cJSON_AddStringToObject(header, "typ", "dpop+jwt") ||
        !cJSON_AddStringToObject(header, "alg", "ES256")) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (!cJSON_AddItemToObject(header, "jwk", jwk)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    jwk = NULL; /* ownership moved to header */
    if (!cJSON_AddStringToObject(payload, "jti", jti) ||
        !cJSON_AddStringToObject(payload, "htm", options->http_method) ||
        !cJSON_AddStringToObject(payload, "htu", htu)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    issued_at =
        options->issued_at > 0 ? options->issued_at : (int64_t)time(NULL);
    if (!cJSON_AddNumberToObject(payload, "iat", (double)issued_at)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (options->nonce &&
        !cJSON_AddStringToObject(payload, "nonce", options->nonce)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (options->access_token) {
        SHA256((const unsigned char *)options->access_token,
               strlen(options->access_token), digest);
        status = wf_oauth_base64url(digest, sizeof(digest), &ath);
        if (status != WF_OK || !cJSON_AddStringToObject(payload, "ath", ath)) {
            if (status == WF_OK) status = WF_ERR_ALLOC;
            goto done;
        }
    }
    header_json = cJSON_PrintUnformatted(header);
    payload_json = cJSON_PrintUnformatted(payload);
    if (!header_json || !payload_json) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    status = wf_oauth_base64url((const unsigned char *)header_json,
                                strlen(header_json), &header_b64);
    if (status == WF_OK)
        status = wf_oauth_base64url((const unsigned char *)payload_json,
                                    strlen(payload_json), &payload_b64);
    if (status != WF_OK) goto done;
    input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    signing_input = malloc(input_len + 1);
    if (!signing_input) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(signing_input, input_len + 1, "%s.%s", header_b64, payload_b64);
    status = wf_oauth_es256_sign(key, (const unsigned char *)signing_input,
                                 input_len, signature);
    if (status == WF_OK)
        status =
            wf_oauth_base64url(signature, sizeof(signature), &signature_b64);
    if (status != WF_OK) goto done;
    jwt_len = input_len + 1 + strlen(signature_b64);
    jwt = malloc(jwt_len + 1);
    if (!jwt) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(jwt, jwt_len + 1, "%s.%s", signing_input, signature_b64);
    *jwt_out = jwt;
    jwt = NULL;
    status = WF_OK;
done:
    cJSON_Delete(header);
    cJSON_Delete(payload);
    cJSON_Delete(jwk);
    free(x);
    free(y);
    free(header_json);
    free(payload_json);
    free(header_b64);
    free(payload_b64);
    free(signature_b64);
    free(htu);
    free(jti);
    free(ath);
    free(signing_input);
    free(jwt);
    return status;
}

wf_status wf_oauth_client_assertion_create(
    const wf_oauth_dpop_key *key,
    const wf_oauth_client_assertion_options *options, char **jwt_out) {
    cJSON *header = NULL, *payload = NULL;
    char *jti = NULL, *header_json = NULL, *payload_json = NULL;
    char *header_b64 = NULL, *payload_b64 = NULL, *signature_b64 = NULL;
    char *signing_input = NULL, *jwt = NULL;
    unsigned char signature[64];
    size_t input_len, jwt_len;
    int64_t issued_at;
    wf_status status;
    if (!key || !options || !jwt_out || !options->client_id ||
        !*options->client_id || !options->authorization_server_issuer ||
        !wf_oauth_url_valid(options->authorization_server_issuer, 1, 1, 0) ||
        !options->key_id || !*options->key_id)
        return WF_ERR_INVALID_ARG;
    *jwt_out = NULL;
    issued_at =
        options->issued_at > 0 ? options->issued_at : (int64_t)time(NULL);
    if (issued_at <= 0 || issued_at > INT64_MAX - 60) return WF_ERR_INVALID_ARG;
    if (options->jti) {
        if (!*options->jti) return WF_ERR_INVALID_ARG;
        jti = wf_oauth_strdup(options->jti);
        status = jti ? WF_OK : WF_ERR_ALLOC;
    } else {
        status = wf_oauth_random_jti(&jti);
    }
    if (status != WF_OK) goto done;
    header = cJSON_CreateObject();
    payload = cJSON_CreateObject();
    if (!header || !payload ||
        !cJSON_AddStringToObject(header, "alg", "ES256") ||
        !cJSON_AddStringToObject(header, "kid", options->key_id) ||
        !cJSON_AddStringToObject(payload, "iss", options->client_id) ||
        !cJSON_AddStringToObject(payload, "sub", options->client_id) ||
        !cJSON_AddStringToObject(payload, "aud",
                                 options->authorization_server_issuer) ||
        !cJSON_AddStringToObject(payload, "jti", jti) ||
        !cJSON_AddNumberToObject(payload, "iat", (double)issued_at) ||
        !cJSON_AddNumberToObject(payload, "exp", (double)(issued_at + 60))) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    header_json = cJSON_PrintUnformatted(header);
    payload_json = cJSON_PrintUnformatted(payload);
    if (!header_json || !payload_json) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    status = wf_oauth_base64url((const unsigned char *)header_json,
                                strlen(header_json), &header_b64);
    if (status == WF_OK)
        status = wf_oauth_base64url((const unsigned char *)payload_json,
                                    strlen(payload_json), &payload_b64);
    if (status != WF_OK) goto done;
    input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    signing_input = malloc(input_len + 1);
    if (!signing_input) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(signing_input, input_len + 1, "%s.%s", header_b64, payload_b64);
    status = wf_oauth_es256_sign(key, (const unsigned char *)signing_input,
                                 input_len, signature);
    if (status == WF_OK)
        status =
            wf_oauth_base64url(signature, sizeof(signature), &signature_b64);
    if (status != WF_OK) goto done;
    jwt_len = input_len + 1 + strlen(signature_b64);
    jwt = malloc(jwt_len + 1);
    if (!jwt) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(jwt, jwt_len + 1, "%s.%s", signing_input, signature_b64);
    *jwt_out = jwt;
    jwt = NULL;
done:
    cJSON_Delete(header);
    cJSON_Delete(payload);
    free(jti);
    free(header_json);
    free(payload_json);
    free(header_b64);
    free(payload_b64);
    free(signature_b64);
    free(signing_input);
    free(jwt);
    return status;
}
