/*
 * service_auth.c — mint and verify AT Protocol service-auth (service JWT)
 * tokens.
 *
 * This is the PDS-side complement of com.atproto.server.getServiceAuth: rather
 * than requesting a signed token from a PDS (wf_agent_get_service_auth_typed)
 * or extracting the `token` field from that call's response
 * (wf_server_parse_auth_token), a self-hosted PDS can *mint* a token locally by
 * signing a compact JWT with its own repo signing key. Such tokens carry
 * inter-service auth to labeler / ozone / feedgen backends.
 *
 * The wire format matches @atproto/xrpc-server `createServiceJwt`
 * (/Volumes/Storage/Developer/Local/atproto/packages/xrpc-server/src/auth.ts):
 *
 *   header  = { "typ": "JWT", "alg": <keypair.jwtAlg> }
 *   payload = noUndefinedVals({ iat, iss, aud, exp, lxm, jti })
 *   jwt     = base64url(header) "." base64url(payload) "." base64url(sig)
 *
 * where `sig` is the 64-byte compact (r||s, low-S) ECDSA signature over the
 * ASCII bytes of `base64url(header) "." base64url(payload)`. P-256 keys use
 * alg "ES256", secp256k1 keys use "ES256K" — the same convention the SDK's
 * did:key verifier understands.
 *
 * Crypto is delegated to the shared primitives in wolfram/crypto.h
 * (wf_sign / wf_verify / wf_signing_key_public_didkey): no JOSE signing is
 * hand-rolled here. Only the JSON assembly, base64url transcoding, and the
 * hex `jti` nonce are local — mirroring how plc.c and the oauth module keep a
 * small self-contained base64url helper rather than sharing one across modules.
 */

#include "wolfram/server.h"
#include "wolfram/crypto.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include <openssl/rand.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── base64url (RFC 4648 §5, no padding) ─────────────────────────── */

static const char wf_sa_b64url_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static char *wf_sa_base64url_encode(const unsigned char *in, size_t len) {
    size_t out_len = (len + 2) / 3 * 4; /* padded length upper bound */
    char *out = malloc(out_len + 1);
    size_t i = 0, o = 0;
    if (!out) {
        return NULL;
    }
    while (i + 3 <= len) {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) |
                     (uint32_t)in[i + 2];
        out[o++] = wf_sa_b64url_alphabet[(n >> 18) & 0x3f];
        out[o++] = wf_sa_b64url_alphabet[(n >> 12) & 0x3f];
        out[o++] = wf_sa_b64url_alphabet[(n >> 6) & 0x3f];
        out[o++] = wf_sa_b64url_alphabet[n & 0x3f];
        i += 3;
    }
    if (len - i == 1) {
        uint32_t n = (uint32_t)in[i] << 16;
        out[o++] = wf_sa_b64url_alphabet[(n >> 18) & 0x3f];
        out[o++] = wf_sa_b64url_alphabet[(n >> 12) & 0x3f];
    } else if (len - i == 2) {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = wf_sa_b64url_alphabet[(n >> 18) & 0x3f];
        out[o++] = wf_sa_b64url_alphabet[(n >> 12) & 0x3f];
        out[o++] = wf_sa_b64url_alphabet[(n >> 6) & 0x3f];
    }
    out[o] = '\0';
    return out;
}

static int wf_sa_b64url_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

/* Decode a base64url (no-padding) segment. Returns a heap buffer of *out_len
 * bytes, or NULL on malformed input / OOM. */
static unsigned char *wf_sa_base64url_decode(const char *in, size_t in_len,
                                             size_t *out_len) {
    unsigned char *out;
    size_t o = 0;
    size_t full = in_len / 4;
    size_t rem = in_len % 4;
    if (rem == 1) {
        return NULL; /* impossible base64 tail */
    }
    out = malloc(full * 3 + 3);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < full; i++) {
        int a = wf_sa_b64url_value(in[i * 4]);
        int b = wf_sa_b64url_value(in[i * 4 + 1]);
        int c = wf_sa_b64url_value(in[i * 4 + 2]);
        int d = wf_sa_b64url_value(in[i * 4 + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            free(out);
            return NULL;
        }
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                     ((uint32_t)c << 6) | (uint32_t)d;
        out[o++] = (unsigned char)((n >> 16) & 0xff);
        out[o++] = (unsigned char)((n >> 8) & 0xff);
        out[o++] = (unsigned char)(n & 0xff);
    }
    if (rem == 2) {
        int a = wf_sa_b64url_value(in[full * 4]);
        int b = wf_sa_b64url_value(in[full * 4 + 1]);
        if (a < 0 || b < 0) {
            free(out);
            return NULL;
        }
        out[o++] = (unsigned char)(((a << 18) | (b << 12)) >> 16);
    } else if (rem == 3) {
        int a = wf_sa_b64url_value(in[full * 4]);
        int b = wf_sa_b64url_value(in[full * 4 + 1]);
        int c = wf_sa_b64url_value(in[full * 4 + 2]);
        if (a < 0 || b < 0 || c < 0) {
            free(out);
            return NULL;
        }
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                     ((uint32_t)c << 6);
        out[o++] = (unsigned char)((n >> 16) & 0xff);
        out[o++] = (unsigned char)((n >> 8) & 0xff);
    }
    *out_len = o;
    return out;
}

/* ── jti nonce (16 random bytes, hex) — matches crypto.randomStr(16,'hex') ── */

static wf_status wf_sa_random_jti(char **out) {
    unsigned char raw[16];
    static const char hex[] = "0123456789abcdef";
    char *s;
    *out = NULL;
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
        return WF_ERR_PARSE;
    }
    s = malloc(sizeof(raw) * 2 + 1);
    if (!s) {
        return WF_ERR_ALLOC;
    }
    for (size_t i = 0; i < sizeof(raw); i++) {
        s[i * 2] = hex[(raw[i] >> 4) & 0x0f];
        s[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    s[sizeof(raw) * 2] = '\0';
    *out = s;
    return WF_OK;
}

/* Map a signing key type to its JWT `alg` header value. */
static const char *wf_sa_jwt_alg(wf_key_type type) {
    switch (type) {
        case WF_KEY_TYPE_P256:
            return "ES256";
        case WF_KEY_TYPE_SECP256K1:
            return "ES256K";
        default:
            return NULL;
    }
}

/* Upstream verifyJwt accepts an issuer DID with one optional, nonempty service
 * fragment. The DID portion itself still follows the shared atproto syntax. */
static int wf_sa_issuer_is_valid(const char *issuer) {
    const char *fragment;
    char *did;
    size_t did_len;
    int valid;

    if (!issuer) return 0;
    fragment = strchr(issuer, '#');
    if (!fragment) return wf_syntax_did_is_valid(issuer);
    if (fragment[1] == '\0' || strchr(fragment + 1, '#')) return 0;
    did_len = (size_t)(fragment - issuer);
    did = malloc(did_len + 1);
    if (!did) return 0;
    memcpy(did, issuer, did_len);
    did[did_len] = '\0';
    valid = wf_syntax_did_is_valid(did);
    free(did);
    return valid;
}

/* ── issuance ─────────────────────────────────────────────────────── */

wf_status wf_server_create_service_auth(const wf_service_auth_request *req,
                                        const wf_signing_key *key,
                                        char **out_token) {
    cJSON *header = NULL, *payload = NULL;
    char *header_json = NULL, *payload_json = NULL;
    char *header_b64 = NULL, *payload_b64 = NULL, *sig_b64 = NULL;
    char *signing_input = NULL, *jti = NULL, *jwt = NULL;
    unsigned char sig[64];
    const char *alg;
    int64_t iat, exp;
    size_t input_len, jwt_len;
    wf_status status;

    if (out_token) {
        *out_token = NULL;
    }
    if (!req || !key || !out_token || !req->iss || !req->iss[0] || !req->aud ||
        !req->aud[0]) {
        return WF_ERR_INVALID_ARG;
    }
    alg = wf_sa_jwt_alg(key->type);
    if (!alg) {
        return WF_ERR_INVALID_ARG;
    }

    iat = (int64_t)time(NULL);
    exp = req->exp > 0 ? req->exp : iat + 60; /* default: now + 60s */

    status = wf_sa_random_jti(&jti);
    if (status != WF_OK) {
        goto done;
    }

    header = cJSON_CreateObject();
    payload = cJSON_CreateObject();
    if (!header || !payload) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    /* Header: typ JWT, alg per key type. */
    if (!cJSON_AddStringToObject(header, "typ", "JWT") ||
        !cJSON_AddStringToObject(header, "alg", alg)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    /* Payload claim order mirrors upstream createServiceJwt: iat, iss, aud,
     * exp, [lxm], jti, [nuance]. */
    if (!cJSON_AddNumberToObject(payload, "iat", (double)iat) ||
        !cJSON_AddStringToObject(payload, "iss", req->iss) ||
        !cJSON_AddStringToObject(payload, "aud", req->aud) ||
        !cJSON_AddNumberToObject(payload, "exp", (double)exp)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (req->lxm && req->lxm[0] &&
        !cJSON_AddStringToObject(payload, "lxm", req->lxm)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (!cJSON_AddStringToObject(payload, "jti", jti)) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    if (req->nuance && req->nuance[0] &&
        !cJSON_AddStringToObject(payload, "nuance", req->nuance)) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    header_json = cJSON_PrintUnformatted(header);
    payload_json = cJSON_PrintUnformatted(payload);
    if (!header_json || !payload_json) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    header_b64 = wf_sa_base64url_encode((const unsigned char *)header_json,
                                        strlen(header_json));
    payload_b64 = wf_sa_base64url_encode((const unsigned char *)payload_json,
                                         strlen(payload_json));
    if (!header_b64 || !payload_b64) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    signing_input = malloc(input_len + 1);
    if (!signing_input) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(signing_input, input_len + 1, "%s.%s", header_b64, payload_b64);

    /* wf_sign hashes with SHA-256, signs (ES256/ES256K), and normalises to
     * low-S, producing the 64-byte compact r||s the atproto wire format uses. */
    status = wf_sign(key, (const unsigned char *)signing_input, input_len, sig,
                     sizeof(sig));
    if (status != WF_OK) {
        goto done;
    }
    sig_b64 = wf_sa_base64url_encode(sig, sizeof(sig));
    if (!sig_b64) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    jwt_len = input_len + 1 + strlen(sig_b64);
    jwt = malloc(jwt_len + 1);
    if (!jwt) {
        status = WF_ERR_ALLOC;
        goto done;
    }
    snprintf(jwt, jwt_len + 1, "%s.%s", signing_input, sig_b64);
    *out_token = jwt;
    jwt = NULL;
    status = WF_OK;

done:
    cJSON_Delete(header);
    cJSON_Delete(payload);
    free(header_json);
    free(payload_json);
    free(header_b64);
    free(payload_b64);
    free(sig_b64);
    free(signing_input);
    free(jti);
    free(jwt);
    return status;
}

/* ── verification ─────────────────────────────────────────────────── */

void wf_service_auth_claims_free(wf_service_auth_claims *claims) {
    if (!claims) {
        return;
    }
    free(claims->alg);
    free(claims->iss);
    free(claims->aud);
    free(claims->jti);
    free(claims->lxm);
    free(claims->nuance);
    memset(claims, 0, sizeof(*claims));
}

static char *wf_sa_strdup(const char *s) {
    size_t len;
    char *copy;
    if (!s) {
        return NULL;
    }
    len = strlen(s);
    copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

/* Parse one base64url JWT segment [start, start+len) into a cJSON object. */
static cJSON *wf_sa_parse_segment(const char *start, size_t len) {
    size_t json_len = 0;
    unsigned char *json = wf_sa_base64url_decode(start, len, &json_len);
    cJSON *obj;
    if (!json) {
        return NULL;
    }
    obj = cJSON_ParseWithLength((const char *)json, json_len);
    free(json);
    if (obj && !cJSON_IsObject(obj)) {
        cJSON_Delete(obj);
        return NULL;
    }
    return obj;
}

wf_status wf_server_verify_service_auth(const char *token,
                                        const char *signing_key_didkey,
                                        int64_t now_unix,
                                        wf_service_auth_claims *out) {
    const char *dot1, *dot2;
    size_t h_len, p_len, s_len, signing_len;
    cJSON *header = NULL, *payload = NULL;
    unsigned char *sig = NULL;
    size_t sig_len = 0;
    wf_service_auth_claims claims = {0};
    cJSON *alg, *iss, *aud, *exp, *iat, *jti, *lxm, *nuance;
    wf_key_type signing_key_type;
    unsigned char *signing_key_raw = NULL;
    size_t signing_key_raw_len = 0;
    int64_t now;
    wf_status status = WF_ERR_PARSE;

    if (!token || !signing_key_didkey || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    /* Split into the three dot-delimited segments. */
    dot1 = strchr(token, '.');
    if (!dot1) {
        return WF_ERR_PARSE;
    }
    dot2 = strchr(dot1 + 1, '.');
    if (!dot2 || strchr(dot2 + 1, '.') != NULL) {
        return WF_ERR_PARSE;
    }
    h_len = (size_t)(dot1 - token);
    p_len = (size_t)(dot2 - (dot1 + 1));
    s_len = strlen(dot2 + 1);
    signing_len = (size_t)(dot2 - token); /* header "." payload */
    if (h_len == 0 || p_len == 0 || s_len == 0) {
        return WF_ERR_PARSE;
    }

    header = wf_sa_parse_segment(token, h_len);
    payload = wf_sa_parse_segment(dot1 + 1, p_len);
    if (!header || !payload) {
        goto done;
    }

    alg = cJSON_GetObjectItemCaseSensitive(header, "alg");
    if (!cJSON_IsString(alg) || !alg->valuestring) {
        goto done;
    }
    if (wf_didkey_decode(signing_key_didkey, &signing_key_type,
                         &signing_key_raw, &signing_key_raw_len) != WF_OK ||
        signing_key_raw_len != 33 ||
        ((signing_key_type == WF_KEY_TYPE_P256 &&
          strcmp(alg->valuestring, "ES256") != 0) ||
         (signing_key_type == WF_KEY_TYPE_SECP256K1 &&
          strcmp(alg->valuestring, "ES256K") != 0))) {
        goto done;
    }

    iss = cJSON_GetObjectItemCaseSensitive(payload, "iss");
    aud = cJSON_GetObjectItemCaseSensitive(payload, "aud");
    exp = cJSON_GetObjectItemCaseSensitive(payload, "exp");
    iat = cJSON_GetObjectItemCaseSensitive(payload, "iat");
    jti = cJSON_GetObjectItemCaseSensitive(payload, "jti");
    lxm = cJSON_GetObjectItemCaseSensitive(payload, "lxm");
    nuance = cJSON_GetObjectItemCaseSensitive(payload, "nuance");

    if (!cJSON_IsString(iss) || !iss->valuestring || !cJSON_IsString(aud) ||
        !aud->valuestring || !cJSON_IsNumber(exp) ||
        !wf_sa_issuer_is_valid(iss->valuestring)) {
        goto done;
    }

    /* Reject expired tokens (mirrors verifyJwt's `Date.now()/1000 > exp`). */
    now = now_unix > 0 ? now_unix : (int64_t)time(NULL);
    if ((int64_t)exp->valuedouble < now) {
        goto done;
    }

    /* Verify the signature over `header.payload` with the issuer's key. */
    sig = wf_sa_base64url_decode(dot2 + 1, s_len, &sig_len);
    if (!sig) {
        goto done;
    }
    if (wf_verify(signing_key_didkey, (const unsigned char *)token, signing_len,
                  sig, sig_len) != WF_OK) {
        goto done;
    }

    /* Signature valid — copy claims out. */
    claims.alg = wf_sa_strdup(alg->valuestring);
    claims.iss = wf_sa_strdup(iss->valuestring);
    claims.aud = wf_sa_strdup(aud->valuestring);
    claims.exp = (int64_t)exp->valuedouble;
    if (cJSON_IsNumber(iat)) {
        claims.iat = (int64_t)iat->valuedouble;
    }
    if (cJSON_IsString(jti) && jti->valuestring) {
        claims.jti = wf_sa_strdup(jti->valuestring);
    }
    if (cJSON_IsString(lxm) && lxm->valuestring) {
        claims.lxm = wf_sa_strdup(lxm->valuestring);
    }
    if (cJSON_IsString(nuance) && nuance->valuestring) {
        claims.nuance = wf_sa_strdup(nuance->valuestring);
    }
    if (!claims.alg || !claims.iss || !claims.aud ||
        (cJSON_IsString(jti) && jti->valuestring && !claims.jti) ||
        (cJSON_IsString(lxm) && lxm->valuestring && !claims.lxm) ||
        (cJSON_IsString(nuance) && nuance->valuestring && !claims.nuance)) {
        wf_service_auth_claims_free(&claims);
        status = WF_ERR_ALLOC;
        goto done;
    }

    *out = claims;
    status = WF_OK;

done:
    cJSON_Delete(header);
    cJSON_Delete(payload);
    free(sig);
    free(signing_key_raw);
    return status;
}
