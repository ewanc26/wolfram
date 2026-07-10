/**
 * test_oauth_verify.c — offline field-level tests for the OAuth
 * resource-server Bearer + DPoP verification module (wolfram/oauth/verify.h).
 *
 * Builds ES256 keys with wolfram's own generator, signs access-token and DPoP
 * proof JWTs with a minimal honest OpenSSL signer (low-S normalized, matching
 * the project's existing verification convention), and asserts both the happy
 * path and every negative case listed in the task.
 */

#include "test.h"
#include "wolfram/oauth/verify.h"
#include "wolfram/oauth/dpop.h"

#include <cJSON.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Test signing helpers (OpenSSL, low-S normalized)                   */
/* ------------------------------------------------------------------ */

static EC_KEY *key_from_scalar(const unsigned char priv[32]) {
    EC_KEY *ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM *s = BN_bin2bn(priv, 32, NULL);
    const EC_GROUP *g = EC_KEY_get0_group(ec);
    EC_POINT *pt = EC_POINT_new(g);
    EC_POINT_mul(g, pt, s, NULL, NULL, NULL);
    EC_KEY_set_private_key(ec, s);
    EC_KEY_set_public_key(ec, pt);
    BN_free(s);
    EC_POINT_free(pt);
    return ec;
}

static char *b64url(const unsigned char *in, size_t len) {
    size_t plen = ((len + 2) / 3) * 4;
    char *p = malloc(plen + 1), *out = malloc(plen + 1);
    size_t i, n = 0;
    if (!p || !out) { free(p); free(out); return NULL; }
    EVP_EncodeBlock((unsigned char *)p, in, (int)len);
    for (i = 0; i < plen; i++) {
        char c = p[i];
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
        else if (c == '=') break;
        out[n++] = c;
    }
    out[n] = '\0';
    free(p);
    return out;
}

/* Sign `signing_input` with `ec`, producing a base64url raw (r||s) signature.
 * Normalizes S to the lower half of the curve order (low-S) to match the
 * project's existing verification convention. */
static char *es256_sign(EC_KEY *ec, const char *signing_input) {
    unsigned char digest[32], raw[64];
    SHA256((const unsigned char *)signing_input, strlen(signing_input), digest);
    ECDSA_SIG *sig = ECDSA_do_sign(digest, 32, ec);
    if (!sig) return NULL;
    const BIGNUM *r, *s;
    ECDSA_SIG_get0(sig, &r, &s);
    const EC_GROUP *g = EC_KEY_get0_group(ec);
    BIGNUM *order = BN_new();
    EC_GROUP_get_order(g, order, NULL);
    BIGNUM *half = BN_dup(order);
    BN_rshift1(half, half);
    BIGNUM *sn = BN_dup(s);
    if (BN_cmp(sn, half) > 0) {
        BIGNUM *norm = BN_dup(order);
        BN_sub(norm, norm, sn);
        BN_free(sn);
        sn = norm;
    }
    BN_bn2binpad(r, raw, 32);
    BN_bn2binpad(sn, raw + 32, 32);
    BN_free(order);
    BN_free(half);
    BN_free(sn);
    ECDSA_SIG_free(sig);
    return b64url(raw, 64);
}

static wf_status pub_coords(EC_KEY *ec, unsigned char x[32], unsigned char y[32]) {
    const EC_GROUP *g = EC_KEY_get0_group(ec);
    const EC_POINT *pt = EC_KEY_get0_public_key(ec);
    BIGNUM *bx = BN_new(), *by = BN_new();
    wf_status st;
    if (EC_POINT_get_affine_coordinates_GFp(g, pt, bx, by, NULL) != 1 ||
        BN_bn2binpad(bx, x, 32) != 32 || BN_bn2binpad(by, y, 32) != 32) {
        st = WF_ERR_PARSE;
    } else {
        st = WF_OK;
    }
    BN_free(bx);
    BN_free(by);
    return st;
}

/* RFC 7638 thumbprint of a public JWK given its base64url x/y strings. */
static char *jwk_thumbprint(const char *x_b64, const char *y_b64) {
    char canon[256];
    unsigned char digest[32];
    snprintf(canon, sizeof(canon),
             "{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"%s\",\"y\":\"%s\"}",
             x_b64, y_b64);
    SHA256((const unsigned char *)canon, strlen(canon), digest);
    return b64url(digest, 32);
}

/* ------------------------------------------------------------------ */
/* JWT builders                                                        */
/* ------------------------------------------------------------------ */

/* Build a complete JWT from a header JSON object and payload JSON object. */
static char *make_jwt(EC_KEY *ec, cJSON *header, cJSON *payload) {
    char *hj = cJSON_PrintUnformatted(header);
    char *pj = cJSON_PrintUnformatted(payload);
    char *hb = b64url((const unsigned char *)hj, strlen(hj));
    char *pb = b64url((const unsigned char *)pj, strlen(pj));
    size_t silen = strlen(hb) + 1 + strlen(pb);
    char *si = malloc(silen + 1);
    char *sig, *jwt;
    snprintf(si, silen + 1, "%s.%s", hb, pb);
    sig = es256_sign(ec, si);
    size_t jlen = silen + 1 + strlen(sig);
    jwt = malloc(jlen + 1);
    snprintf(jwt, jlen + 1, "%s.%s", si, sig);
    free(hj);
    free(pj);
    free(hb);
    free(pb);
    free(si);
    free(sig);
    cJSON_Delete(header);
    cJSON_Delete(payload);
    return jwt;
}

/* Access-token JWT signed by `at_ec`, optionally binding `cnf.jkt`. */
static char *make_access_token(EC_KEY *at_ec, const char *sub, const char *iss,
                               const char *aud, const char *scope,
                               const char *cnf_jkt, int64_t iat, int64_t exp) {
    cJSON *h = cJSON_CreateObject();
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "alg", "ES256");
    cJSON_AddStringToObject(h, "kid", "test-key-1");
    cJSON_AddStringToObject(p, "iss", iss);
    cJSON_AddStringToObject(p, "sub", sub);
    cJSON_AddStringToObject(p, "aud", aud);
    cJSON_AddStringToObject(p, "scope", scope);
    cJSON_AddNumberToObject(p, "iat", (double)iat);
    cJSON_AddNumberToObject(p, "exp", (double)exp);
    if (cnf_jkt) {
        cJSON *cnf = cJSON_CreateObject();
        cJSON_AddStringToObject(cnf, "jkt", cnf_jkt);
        cJSON_AddItemToObject(p, "cnf", cnf);
    }
    return make_jwt(at_ec, h, p);
}

/* DPoP proof JWT signed by `dp_ec`. `jwk` is the proof's public JWK object
 * (added to the header). */
static char *make_dpop_proof(EC_KEY *dp_ec, cJSON *jwk, const char *htm,
                             const char *htu, const char *ath, const char *jti,
                             int64_t iat) {
    cJSON *h = cJSON_CreateObject();
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "typ", "dpop+jwt");
    cJSON_AddStringToObject(h, "alg", "ES256");
    cJSON_AddItemToObject(h, "jwk", jwk); /* ownership moved */
    cJSON_AddStringToObject(p, "htm", htm);
    cJSON_AddStringToObject(p, "htu", htu);
    cJSON_AddStringToObject(p, "jti", jti);
    cJSON_AddNumberToObject(p, "iat", (double)iat);
    if (ath) cJSON_AddStringToObject(p, "ath", ath);
    return make_jwt(dp_ec, h, p);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static char *str_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *o = malloc(n);
    memcpy(o, s, n);
    return o;
}

int main(void) {
    wf_oauth_dpop_key *at_key = NULL, *dp_key = NULL;
    wf_oauth_trusted_keys *keys = NULL;
    wf_oauth_dpop_replay_cache *replay = NULL;
    unsigned char at_scalar[32], dp_scalar[32];
    EC_KEY *at_ec = NULL, *dp_ec = NULL;
    unsigned char at_x[32], at_y[32], dp_x[32], dp_y[32];
    char *at_xb = NULL, *at_yb = NULL, *dp_xb = NULL, *dp_yb = NULL;
    char *at_jwk = NULL, *dp_jwk = NULL, *dp_jkt = NULL;
    char *access_token = NULL, *dpop_proof = NULL, *auth_hdr = NULL;
    wf_oauth_verified_token *tok = NULL;
    int64_t now = (int64_t)time(NULL);
    const char *uri = "https://api.example.com/xrpc/com.example.foo";
    const char *did = "did:web:example.com";

    WF_CHECK(wf_oauth_dpop_key_generate(&at_key) == WF_OK);
    WF_CHECK(wf_oauth_dpop_key_generate(&dp_key) == WF_OK);
    WF_CHECK(wf_oauth_dpop_key_export(at_key, at_scalar) == WF_OK);
    WF_CHECK(wf_oauth_dpop_key_export(dp_key, dp_scalar) == WF_OK);
    at_ec = key_from_scalar(at_scalar);
    dp_ec = key_from_scalar(dp_scalar);
    WF_CHECK(at_ec && dp_ec);
    WF_CHECK(pub_coords(at_ec, at_x, at_y) == WF_OK);
    WF_CHECK(pub_coords(dp_ec, dp_x, dp_y) == WF_OK);
    at_xb = b64url(at_x, 32);
    at_yb = b64url(at_y, 32);
    dp_xb = b64url(dp_x, 32);
    dp_yb = b64url(dp_y, 32);
    {
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"%s\",\"y\":\"%s\"}",
                 at_xb, at_yb);
        at_jwk = str_dup(buf);
        snprintf(buf, sizeof(buf),
                 "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"%s\",\"y\":\"%s\"}",
                 dp_xb, dp_yb);
        dp_jwk = str_dup(buf);
    }
    dp_jkt = jwk_thumbprint(dp_xb, dp_yb);

    WF_CHECK(wf_oauth_trusted_keys_new(&keys) == WF_OK);
    WF_CHECK(wf_oauth_trusted_keys_add_jwk(keys, at_jwk) == WF_OK);
    WF_CHECK(wf_oauth_dpop_replay_cache_new(&replay) == WF_OK);

    /* --- Build the happy-path tokens (DPoP-bound access token). --- */
    access_token = make_access_token(at_ec, did, "https://op.example.com",
                                     "https://api.example.com",
                                     "atproto repo", dp_jkt, now, now + 3600);
    {
        unsigned char digest[32];
        char *ath;
        SHA256((const unsigned char *)access_token, strlen(access_token), digest);
        ath = b64url(digest, 32);
        cJSON *jwk = cJSON_Parse(dp_jwk);
        dpop_proof = make_dpop_proof(dp_ec, jwk, "POST", uri, ath,
                                     "jti-happy-1", now);
        free(ath);
    }
    WF_CHECK(access_token && dpop_proof);
    auth_hdr = malloc(strlen(access_token) + 8);
    sprintf(auth_hdr, "Bearer %s", access_token);

    /* --- Happy path: wf_oauth_verify_request --- */
    {
        wf_status st = wf_oauth_verify_request(auth_hdr, dpop_proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st == WF_OK);
        WF_CHECK(tok != NULL);
        if (tok) {
            WF_CHECK(tok->dpop_bound == 1);
            WF_CHECK(tok->sub && strcmp(tok->sub, did) == 0);
            WF_CHECK(tok->iss && strcmp(tok->iss, "https://op.example.com") == 0);
            WF_CHECK(tok->aud && strcmp(tok->aud, "https://api.example.com") == 0);
            WF_CHECK(tok->scope && strcmp(tok->scope, "atproto repo") == 0);
            WF_CHECK(tok->dpop_jkt && strcmp(tok->dpop_jkt, dp_jkt) == 0);
            WF_CHECK(tok->exp == now + 3600);
        }
        wf_oauth_verified_token_free(tok);
        tok = NULL;
    }

    /* --- Happy path with "DPoP" scheme (real atproto clients) --- */
    {
        char *dpop_auth = malloc(strlen(access_token) + 6);
        sprintf(dpop_auth, "DPoP %s", access_token);
        /* fresh proof with a unique jti to avoid the replay cache */
        unsigned char digest2[32];
        char *ath2;
        SHA256((const unsigned char *)access_token, strlen(access_token), digest2);
        ath2 = b64url(digest2, 32);
        cJSON *jwk2 = cJSON_Parse(dp_jwk);
        char *dpop_proof2 = make_dpop_proof(dp_ec, jwk2, "POST", uri, ath2,
                                            "jti-happy-dpop", now);
        free(ath2);
        wf_status st = wf_oauth_verify_request(dpop_auth, dpop_proof2, "POST", uri,
                                                keys, replay, &tok);
        free(dpop_auth);
        free(dpop_proof2);
        WF_CHECK(st == WF_OK);
        WF_CHECK(tok != NULL);
        if (tok) WF_CHECK(tok->dpop_bound == 1);
        wf_oauth_verified_token_free(tok);
        tok = NULL;
    }

    /* --- Negative: tampered DPoP proof signature --- */
    {
        char *bad = str_dup(dpop_proof);
        size_t len = strlen(bad);
        bad[len - 1] = (bad[len - 1] == 'A') ? 'B' : 'A';
        wf_status st = wf_oauth_verify_request(auth_hdr, bad, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st != WF_OK);
        WF_CHECK(tok == NULL);
        free(bad);
    }

    /* --- Negative: expired access token --- */
    {
        char *exp_at = make_access_token(at_ec, did, "https://op.example.com",
                                         "https://api.example.com", "atproto",
                                         dp_jkt, now - 100, now - 10);
        char *exp_auth = malloc(strlen(exp_at) + 8);
        sprintf(exp_auth, "Bearer %s", exp_at);
        wf_status st = wf_oauth_verify_request(exp_auth, dpop_proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st != WF_OK);
        free(exp_at);
        free(exp_auth);
    }

    /* --- Negative: wrong htm --- */
    {
        unsigned char digest[32];
        char *ath;
        SHA256((const unsigned char *)access_token, strlen(access_token), digest);
        ath = b64url(digest, 32);
        cJSON *jwk = cJSON_Parse(dp_jwk);
        char *proof = make_dpop_proof(dp_ec, jwk, "GET", uri, ath,
                                      "jti-htm-1", now);
        wf_status st = wf_oauth_verify_request(auth_hdr, proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st != WF_OK);
        free(ath);
        free(proof);
    }

    /* --- Negative: wrong htu --- */
    {
        unsigned char digest[32];
        char *ath;
        SHA256((const unsigned char *)access_token, strlen(access_token), digest);
        ath = b64url(digest, 32);
        cJSON *jwk = cJSON_Parse(dp_jwk);
        char *proof = make_dpop_proof(dp_ec, jwk, "POST",
                                      "https://evil.example.com/xrpc/x", ath,
                                      "jti-htu-1", now);
        wf_status st = wf_oauth_verify_request(auth_hdr, proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st != WF_OK);
        free(ath);
        free(proof);
    }

    /* --- Negative: ath mismatch --- */
    {
        cJSON *jwk = cJSON_Parse(dp_jwk);
        char *proof = make_dpop_proof(dp_ec, jwk, "POST", uri,
                                      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                      "jti-ath-1", now);
        wf_status st = wf_oauth_verify_request(auth_hdr, proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st != WF_OK);
        free(proof);
    }

    /* --- Negative: reused jti (same replay cache) --- */
    {
        /* First verify with a fresh jti succeeds... */
        unsigned char digest[32];
        char *ath;
        SHA256((const unsigned char *)access_token, strlen(access_token), digest);
        ath = b64url(digest, 32);
        cJSON *jwk = cJSON_Parse(dp_jwk);
        char *proof = make_dpop_proof(dp_ec, jwk, "POST", uri, ath,
                                      "jti-replay-1", now);
        wf_status st1 = wf_oauth_verify_request(auth_hdr, proof, "POST", uri,
                                                keys, replay, &tok);
        wf_oauth_verified_token_free(tok);
        tok = NULL;
        WF_CHECK(st1 == WF_OK);
        /* ...then the same jti is rejected. */
        cJSON *jwk2 = cJSON_Parse(dp_jwk);
        char *proof2 = make_dpop_proof(dp_ec, jwk2, "POST", uri, ath,
                                       "jti-replay-1", now);
        wf_status st2 = wf_oauth_verify_request(auth_hdr, proof2, "POST", uri,
                                                keys, replay, &tok);
        WF_CHECK(st2 == WF_ERR_DUPLICATE);
        WF_CHECK(tok == NULL);
        free(ath);
        free(proof);
        free(proof2);
    }

    /* --- Negative: missing jwk in proof header --- */
    {
        cJSON *h = cJSON_CreateObject();
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(h, "typ", "dpop+jwt");
        cJSON_AddStringToObject(h, "alg", "ES256");
        cJSON_AddStringToObject(p, "htm", "POST");
        cJSON_AddStringToObject(p, "htu", uri);
        cJSON_AddStringToObject(p, "jti", "jti-nojwk-1");
        cJSON_AddNumberToObject(p, "iat", (double)now);
        char *proof = make_jwt(dp_ec, h, p);
        wf_status st = wf_oauth_verify_request(auth_hdr, proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st != WF_OK);
        free(proof);
    }

    /* --- Negative: alg not ES256 (HS256) --- */
    {
        cJSON *h = cJSON_CreateObject();
        cJSON *p = cJSON_CreateObject();
        cJSON *jwk = cJSON_Parse(dp_jwk);
        cJSON_AddStringToObject(h, "typ", "dpop+jwt");
        cJSON_AddStringToObject(h, "alg", "HS256");
        cJSON_AddItemToObject(h, "jwk", jwk);
        cJSON_AddStringToObject(p, "htm", "POST");
        cJSON_AddStringToObject(p, "htu", uri);
        cJSON_AddStringToObject(p, "jti", "jti-alg-1");
        cJSON_AddNumberToObject(p, "iat", (double)now);
        char *proof = make_jwt(dp_ec, h, p);
        wf_status st = wf_oauth_verify_request(auth_hdr, proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st != WF_OK);
        free(proof);
    }

    /* --- Bearer-only path (no DPoP) --- */
    {
        wf_status st = wf_oauth_verify_request(auth_hdr, NULL, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st == WF_OK);
        if (tok) {
            WF_CHECK(tok->dpop_bound == 0);
            WF_CHECK(tok->sub && strcmp(tok->sub, did) == 0);
            /* The bearer token carries a cnf.jkt confirmation claim. */
            WF_CHECK(tok->dpop_jkt && strcmp(tok->dpop_jkt, dp_jkt) == 0);
        }
        wf_oauth_verified_token_free(tok);
        tok = NULL;
    }

    /* --- DPoP-only path (no access token) --- */
    {
        unsigned char digest[32];
        char *ath;
        SHA256((const unsigned char *)access_token, strlen(access_token), digest);
        ath = b64url(digest, 32);
        cJSON *jwk = cJSON_Parse(dp_jwk);
        char *proof = make_dpop_proof(dp_ec, jwk, "POST", uri, NULL,
                                      "jti-dponly-1", now);
        wf_status st = wf_oauth_verify_request(NULL, proof, "POST", uri,
                                               keys, replay, &tok);
        WF_CHECK(st == WF_OK);
        if (tok) {
            WF_CHECK(tok->dpop_bound == 1);
            WF_CHECK(tok->sub == NULL);
            WF_CHECK(tok->dpop_jkt && strcmp(tok->dpop_jkt, dp_jkt) == 0);
        }
        wf_oauth_verified_token_free(tok);
        tok = NULL;
        free(ath);
        free(proof);
    }

    /* --- Direct wf_oauth_verify_bearer / verify_dpop smoke --- */
    {
        wf_oauth_verified_token *bt = NULL, *dt = NULL;
        wf_oauth_dpop_replay_cache *replay2 = NULL;
        WF_CHECK(wf_oauth_dpop_replay_cache_new(&replay2) == WF_OK);
        WF_CHECK(wf_oauth_verify_bearer(access_token, keys, &bt) == WF_OK);
        if (bt) {
            WF_CHECK(bt->sub && strcmp(bt->sub, did) == 0);
            WF_CHECK(bt->dpop_bound == 0);
            WF_CHECK(bt->dpop_jkt && strcmp(bt->dpop_jkt, dp_jkt) == 0);
        }
        wf_oauth_verified_token_free(bt);
        WF_CHECK(wf_oauth_verify_dpop(dpop_proof, access_token, "POST", uri,
                                      replay2, &dt) == WF_OK);
        if (dt) {
            WF_CHECK(dt->dpop_bound == 1);
            WF_CHECK(dt->dpop_jkt && strcmp(dt->dpop_jkt, dp_jkt) == 0);
        }
        wf_oauth_verified_token_free(dt);
        wf_oauth_dpop_replay_cache_free(replay2);
    }

    /* Cleanup. */
    free(at_xb);
    free(at_yb);
    free(dp_xb);
    free(dp_yb);
    free(at_jwk);
    free(dp_jwk);
    free(dp_jkt);
    free(access_token);
    free(dpop_proof);
    free(auth_hdr);
    wf_oauth_dpop_key_free(at_key);
    wf_oauth_dpop_key_free(dp_key);
    wf_oauth_trusted_keys_free(keys);
    wf_oauth_dpop_replay_cache_free(replay);
    EC_KEY_free(at_ec);
    EC_KEY_free(dp_ec);

    WF_TEST_SUMMARY();
}
