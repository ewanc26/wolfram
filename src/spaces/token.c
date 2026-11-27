#include "wolfram/spaces/token.h"

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <openssl/rand.h>

#include "wolfram/spaces/uri.h"
#include "wolfram/syntax.h"

#define MAX_TOKEN 16384
#define MAX_TIME 9007199254740991LL /* 2^53 - 1, JSON's exact range */

static const char *token_typ(wf_space_token_type type) {
    switch (type) {
        case WF_SPACE_TOKEN_DELEGATION:
            return "atproto-space-delegation+jwt";
        case WF_SPACE_TOKEN_CREDENTIAL:
            return "atproto-space-credential+jwt";
        case WF_SPACE_TOKEN_CLIENT_ATTESTATION:
            return "atproto-client-attestation+jwt";
    }
    return NULL;
}

static char *copy_string(const char *text) {
    size_t len = strlen(text) + 1;
    char *copy = malloc(len);
    if (copy) {
        memcpy(copy, text, len);
    }
    return copy;
}

void wf_space_token_free(wf_space_token *token) {
    if (token) {
        free(token->alg);
        free(token->kid);
        free(token->iss);
        free(token->sub);
        free(token->aud);
        free(token->cnf_kid);
        free(token->jti);
        free(token->signing_input);
        memset(token, 0, sizeof(*token));
    }
}

static int is_p256_didkey(const char *did_key) {
    wf_key_type type;
    unsigned char *raw = NULL;
    size_t len = 0;
    int ok;

    ok = did_key && strncmp(did_key, "did:key:", 8) == 0 &&
         wf_didkey_decode(did_key, &type, &raw, &len) == WF_OK &&
         type == WF_KEY_TYPE_P256;
    free(raw);
    return ok;
}

/* The authority of a space reference, or NULL (also for a record URI). */
static char *space_authority(const char *space_ref) {
    wf_space_uri uri;
    char *authority = NULL;

    if (wf_space_uri_parse(space_ref, &uri) == WF_OK) {
        if (!uri.author) {
            authority = uri.authority;
            uri.authority = NULL;
        }
        wf_space_uri_free(&uri);
    }
    return authority;
}

/* True when no object member name repeats: two decoders could otherwise read
 * different claims from one signed token. */
static int unique_members(const cJSON *object) {
    const cJSON *a;
    const cJSON *b;

    cJSON_ArrayForEach(a, object) {
        for (b = a->next; b; b = b->next) {
            if (a->string && b->string && strcmp(a->string, b->string) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

/* Decode one base64url segment into a JSON object. */
static wf_status segment_object(const char *start, size_t len, cJSON **out) {
    unsigned char *bytes = NULL;
    size_t bytes_len = 0;
    wf_status status =
        wf_crypto_base64url_decode_strict(start, len, &bytes, &bytes_len);
    const char *end = NULL;

    *out = NULL;
    if (status != WF_OK) {
        return status == WF_ERR_ALLOC ? status : WF_ERR_VALIDATION;
    }
    /* A NUL, raw or escaped, would silently truncate a claim. */
    if (memchr(bytes, '\0', bytes_len) ||
        strstr((const char *)bytes, "\\u0000")) {
        free(bytes);
        return WF_ERR_VALIDATION;
    }
    *out = cJSON_ParseWithOpts((const char *)bytes, &end, 0);
    if (!cJSON_IsObject(*out) || !end || *end != '\0' ||
        !unique_members(*out)) {
        cJSON_Delete(*out);
        *out = NULL;
        status = WF_ERR_VALIDATION;
    }
    free(bytes);
    return status;
}

/* Copy a non-empty string member. An absent optional member leaves NULL. */
static wf_status take_string(const cJSON *object, const char *name,
                             int required, char **out) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!value) {
        return required ? WF_ERR_VALIDATION : WF_OK;
    }
    if (!cJSON_IsString(value) || value->valuestring[0] == '\0') {
        return WF_ERR_VALIDATION;
    }
    *out = copy_string(value->valuestring);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status take_time(const cJSON *object, const char *name, int required,
                           long long *out) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!value) {
        return required ? WF_ERR_VALIDATION : WF_OK;
    }
    if (!cJSON_IsNumber(value) || !(value->valuedouble > 0) ||
        value->valuedouble > (double)MAX_TIME ||
        value->valuedouble != (double)(long long)value->valuedouble) {
        return WF_ERR_VALIDATION;
    }
    *out = (long long)value->valuedouble;
    return WF_OK;
}

static wf_status check_structure(const wf_space_token *token) {
    char *authority;
    int ok = 1;

    switch (token->type) {
        case WF_SPACE_TOKEN_DELEGATION:
            authority = space_authority(token->sub);
            ok = authority && token->aud && wf_syntax_did_is_valid(token->iss);
            free(authority);
            break;
        case WF_SPACE_TOKEN_CREDENTIAL:
            authority = space_authority(token->sub);
            ok = authority && strcmp(authority, token->iss) == 0 &&
                 is_p256_didkey(token->cnf_kid) && token->iat > 0 &&
                 token->exp > token->iat &&
                 token->exp - token->iat <= WF_SPACE_CREDENTIAL_MAX_LIFETIME;
            free(authority);
            break;
        case WF_SPACE_TOKEN_CLIENT_ATTESTATION:
            ok = token->aud && strcmp(token->iss, token->sub) == 0;
            break;
    }
    return ok ? WF_OK : WF_ERR_VALIDATION;
}

wf_status wf_space_token_parse(wf_space_token_type type, const char *jwt,
                               wf_space_token *out) {
    const char *first;
    const char *second;
    cJSON *header = NULL;
    cJSON *claims = NULL;
    const cJSON *typ;
    unsigned char *sig = NULL;
    size_t sig_len = 0;
    wf_status status = WF_ERR_VALIDATION;
    size_t len;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!jwt || !token_typ(type)) {
        return WF_ERR_INVALID_ARG;
    }
    len = strlen(jwt);
    first = len <= MAX_TOKEN ? strchr(jwt, '.') : NULL;
    second = first ? strchr(first + 1, '.') : NULL;
    if (!second || strchr(second + 1, '.')) {
        return WF_ERR_VALIDATION;
    }
    out->type = type;

    status = segment_object(jwt, (size_t)(first - jwt), &header);
    if (status == WF_OK) {
        status =
            segment_object(first + 1, (size_t)(second - first - 1), &claims);
    }
    if (status == WF_OK) {
        typ = cJSON_GetObjectItemCaseSensitive(header, "typ");
        /* No critical extension is understood, so none is accepted. */
        if (!cJSON_IsString(typ) ||
            strcmp(typ->valuestring, token_typ(type)) != 0 ||
            cJSON_GetObjectItemCaseSensitive(header, "crit")) {
            status = WF_ERR_VALIDATION;
        }
    }
    if (status == WF_OK) {
        status = take_string(header, "alg", 1, &out->alg);
    }
    if (status == WF_OK) {
        status = take_string(header, "kid", 0, &out->kid);
    }
    if (status == WF_OK &&
        ((strcmp(out->alg, "ES256") != 0 && strcmp(out->alg, "ES256K") != 0) ||
         (type != WF_SPACE_TOKEN_CLIENT_ATTESTATION &&
          (!out->kid || strcmp(out->kid, "#atproto") != 0)))) {
        status = WF_ERR_VALIDATION;
    }
    if (status == WF_OK) {
        status = take_string(claims, "iss", 1, &out->iss);
    }
    if (status == WF_OK) {
        status = take_string(claims, "sub", 1, &out->sub);
    }
    if (status == WF_OK) {
        status = take_string(claims, "aud", 0, &out->aud);
    }
    if (status == WF_OK) {
        status = take_string(claims, "jti", 1, &out->jti);
    }
    if (status == WF_OK) {
        status = take_string(cJSON_GetObjectItemCaseSensitive(claims, "cnf"),
                             "kid", 0, &out->cnf_kid);
    }
    if (status == WF_OK) {
        status = take_time(claims, "exp", 1, &out->exp);
    }
    if (status == WF_OK) {
        status = take_time(claims, "iat", 0, &out->iat);
    }
    if (status == WF_OK) {
        status = check_structure(out);
    }
    if (status == WF_OK) {
        status = wf_crypto_base64url_decode_strict(
            second + 1, strlen(second + 1), &sig, &sig_len);
        if (status != WF_OK) {
            status = status == WF_ERR_ALLOC ? status : WF_ERR_VALIDATION;
        } else if (sig_len != sizeof(out->signature)) {
            status = WF_ERR_VALIDATION;
        } else {
            memcpy(out->signature, sig, sig_len);
        }
    }
    if (status == WF_OK) {
        size_t input_len = (size_t)(second - jwt);
        out->signing_input = malloc(input_len + 1);
        if (!out->signing_input) {
            status = WF_ERR_ALLOC;
        } else {
            memcpy(out->signing_input, jwt, input_len);
            out->signing_input[input_len] = '\0';
        }
    }
    free(sig);
    cJSON_Delete(header);
    cJSON_Delete(claims);
    if (status != WF_OK) {
        wf_space_token_free(out);
    }
    return status;
}

wf_status wf_space_token_verify_signature(const wf_space_token *token,
                                          const char *did_key) {
    wf_key_type type;
    unsigned char *raw = NULL;
    size_t len = 0;
    const char *alg;
    wf_status status;

    if (!token || !token->signing_input || !token->alg || !did_key) {
        return WF_ERR_INVALID_ARG;
    }
    status = wf_didkey_decode(did_key, &type, &raw, &len);
    free(raw);
    if (status != WF_OK) {
        return status == WF_ERR_ALLOC ? status : WF_ERR_VALIDATION;
    }
    alg = type == WF_KEY_TYPE_P256 ? "ES256" : "ES256K";
    if (strcmp(alg, token->alg) != 0) {
        return WF_ERR_VALIDATION;
    }
    status =
        (token->type == WF_SPACE_TOKEN_CLIENT_ATTESTATION
             ? wf_verify_allow_malleable
             : wf_verify)(did_key, (const unsigned char *)token->signing_input,
                          strlen(token->signing_input), token->signature,
                          sizeof(token->signature));
    if (status != WF_OK && status != WF_ERR_ALLOC) {
        status = WF_ERR_VALIDATION;
    }
    return status;
}

wf_status wf_space_token_check_claims(const wf_space_token *token,
                                      long long now, const char *aud,
                                      const char *sub) {
    if (!token || !token->sub || now <= 0 || now > MAX_TIME) {
        return WF_ERR_INVALID_ARG;
    }
    if (now - WF_SPACE_CLOCK_SKEW >= token->exp ||
        (token->type == WF_SPACE_TOKEN_CREDENTIAL &&
         token->iat > now + WF_SPACE_CLOCK_SKEW) ||
        (aud && (!token->aud || strcmp(token->aud, aud) != 0)) ||
        (sub && strcmp(token->sub, sub) != 0)) {
        return WF_ERR_VALIDATION;
    }
    return WF_OK;
}

wf_status wf_space_host_audience(const char *space_ref, char **out) {
    static const char service[] = "#atproto_pds";
    char *authority;
    size_t len;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    authority = space_ref ? space_authority(space_ref) : NULL;
    if (!authority) {
        return WF_ERR_INVALID_ARG;
    }
    len = strlen(authority);
    *out = malloc(len + sizeof(service));
    if (*out) {
        memcpy(*out, authority, len);
        memcpy(*out + len, service, sizeof(service));
    }
    free(authority);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

/* ---- minting ---- */

static wf_status encode_json(const cJSON *object, char **out) {
    char *text = cJSON_PrintUnformatted(object);
    wf_status status;

    *out = NULL;
    if (!text) {
        return WF_ERR_ALLOC;
    }
    status = wf_crypto_base64url_encode((const unsigned char *)text,
                                        strlen(text), out);
    cJSON_free(text);
    return status;
}

/* "a.b" in a new allocation. */
static char *dotted(const char *a, const char *b) {
    size_t x = strlen(a);
    size_t y = strlen(b);
    char *s = malloc(x + y + 2);
    if (s) {
        memcpy(s, a, x);
        s[x] = '.';
        memcpy(s + x + 1, b, y + 1);
    }
    return s;
}

static wf_status mint(const wf_signing_key *key, wf_space_token_type type,
                      const char *kid, const char *iss, const char *sub,
                      const char *aud, const char *cnf_kid, long long now,
                      long long lifetime, char **out_jwt) {
    cJSON *header = cJSON_CreateObject();
    cJSON *claims = cJSON_CreateObject();
    cJSON *cnf = NULL;
    unsigned char random[16];
    unsigned char signature[64];
    char jti[33];
    char *header_part = NULL;
    char *claims_part = NULL;
    char *signature_part = NULL;
    char *input = NULL;
    wf_status status = WF_ERR_ALLOC;
    size_t i;
    int ok;

    if (RAND_bytes(random, sizeof(random)) != 1) {
        status = WF_ERR_CRYPTO;
        goto done;
    }
    for (i = 0; i < sizeof(random); i++) {
        jti[2 * i] = "0123456789abcdef"[random[i] >> 4];
        jti[2 * i + 1] = "0123456789abcdef"[random[i] & 15];
    }
    jti[32] = '\0';

    ok = header && claims &&
         cJSON_AddStringToObject(header, "alg",
                                 key->type == WF_KEY_TYPE_P256 ? "ES256"
                                                               : "ES256K") &&
         cJSON_AddStringToObject(header, "typ", token_typ(type)) &&
         (!kid || cJSON_AddStringToObject(header, "kid", kid)) &&
         cJSON_AddStringToObject(claims, "iss", iss) &&
         cJSON_AddStringToObject(claims, "sub", sub) &&
         (!aud || cJSON_AddStringToObject(claims, "aud", aud));
    if (ok && cnf_kid) {
        cnf = cJSON_AddObjectToObject(claims, "cnf");
        ok = cnf && cJSON_AddStringToObject(cnf, "kid", cnf_kid);
    }
    ok = ok && cJSON_AddNumberToObject(claims, "iat", (double)now) &&
         cJSON_AddNumberToObject(claims, "exp", (double)(now + lifetime)) &&
         cJSON_AddStringToObject(claims, "jti", jti);
    if (!ok) {
        goto done;
    }
    status = encode_json(header, &header_part);
    if (status == WF_OK) {
        status = encode_json(claims, &claims_part);
    }
    if (status == WF_OK) {
        input = dotted(header_part, claims_part);
        status = input ? wf_sign(key, (const unsigned char *)input,
                                 strlen(input), signature, sizeof(signature))
                       : WF_ERR_ALLOC;
    }
    if (status == WF_OK) {
        status = wf_crypto_base64url_encode(signature, sizeof(signature),
                                            &signature_part);
    }
    if (status == WF_OK) {
        *out_jwt = dotted(input, signature_part);
        status = *out_jwt ? WF_OK : WF_ERR_ALLOC;
    }
done:
    cJSON_Delete(header);
    cJSON_Delete(claims);
    free(header_part);
    free(claims_part);
    free(signature_part);
    free(input);
    return status;
}

/* Shared argument checks; applies the default lifetime. */
static int mint_args(const wf_signing_key *key, long long now,
                     long long *lifetime, long long fallback) {
    if (*lifetime == 0) {
        *lifetime = fallback;
    }
    return key &&
           (key->type == WF_KEY_TYPE_P256 ||
            key->type == WF_KEY_TYPE_SECP256K1) &&
           now > 0 && *lifetime > 0 && now <= MAX_TIME - *lifetime;
}

wf_status wf_space_delegation_token_create(const wf_signing_key *key,
                                           const char *issuer,
                                           const char *space_ref, long long now,
                                           long long lifetime, char **out_jwt) {
    char *aud = NULL;
    wf_status status;

    if (!out_jwt) {
        return WF_ERR_INVALID_ARG;
    }
    *out_jwt = NULL;
    if (!mint_args(key, now, &lifetime, WF_SPACE_DELEGATION_LIFETIME) ||
        !issuer || !wf_syntax_did_is_valid(issuer)) {
        return WF_ERR_INVALID_ARG;
    }
    status = wf_space_host_audience(space_ref, &aud);
    if (status == WF_OK) {
        status = mint(key, WF_SPACE_TOKEN_DELEGATION, "#atproto", issuer,
                      space_ref, aud, NULL, now, lifetime, out_jwt);
    }
    free(aud);
    return status;
}

wf_status wf_space_credential_create(const wf_signing_key *key,
                                     const char *space_ref, const char *cnf_kid,
                                     long long now, long long lifetime,
                                     char **out_jwt) {
    char *authority;
    wf_status status;

    if (!out_jwt) {
        return WF_ERR_INVALID_ARG;
    }
    *out_jwt = NULL;
    if (!mint_args(key, now, &lifetime, WF_SPACE_CREDENTIAL_LIFETIME) ||
        lifetime > WF_SPACE_CREDENTIAL_MAX_LIFETIME || !space_ref ||
        !is_p256_didkey(cnf_kid)) {
        return WF_ERR_INVALID_ARG;
    }
    authority = space_authority(space_ref);
    if (!authority) {
        return WF_ERR_INVALID_ARG;
    }
    status = mint(key, WF_SPACE_TOKEN_CREDENTIAL, "#atproto", authority,
                  space_ref, NULL, cnf_kid, now, lifetime, out_jwt);
    free(authority);
    return status;
}

wf_status wf_space_client_attestation_create(
    const wf_signing_key *key, const char *key_id, const char *client_id,
    const char *audience, long long now, long long lifetime, char **out_jwt) {
    if (!out_jwt) {
        return WF_ERR_INVALID_ARG;
    }
    *out_jwt = NULL;
    if (!mint_args(key, now, &lifetime, WF_SPACE_ATTESTATION_LIFETIME) ||
        key->type != WF_KEY_TYPE_P256 || !key_id || !*key_id || !client_id ||
        !*client_id || !audience || !*audience) {
        return WF_ERR_INVALID_ARG;
    }
    return mint(key, WF_SPACE_TOKEN_CLIENT_ATTESTATION, key_id, client_id,
                client_id, audience, NULL, now, lifetime, out_jwt);
}
