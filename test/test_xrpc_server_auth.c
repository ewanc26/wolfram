/*
 * test_xrpc_server_auth.c — offline integration test for the XRPC server
 * service-token / Bearer auth middleware (wf_xrpc_server_set_auth_middleware).
 *
 * Starts a local server with one protected query route and one public route,
 * installs the middleware, and drives requests with minted app.bsky service
 * JWTs. Verifies positive authz, subject propagation to the handler, and the
 * negative cases (missing / wrong-aud / expired / tampered tokens, and the
 * public route without a token). No network: the issuer is a did:key resolved
 * locally by the default resolver.
 */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"
#include "wolfram/xrpc_server_auth.h"
#include "wolfram/server.h"
#include "wolfram/crypto.h"
#include "wolfram/oauth/dpop.h"
#include "wolfram/oauth/verify.h"
#include "test.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Handlers                                                            */
/* ------------------------------------------------------------------ */

static wf_status protected_handler(void *ctx, const wf_xrpc_request *req,
                                   wf_xrpc_response *resp) {
    (void)ctx;
    cJSON *obj = cJSON_CreateObject();
    char *json;
    if (!obj) {
        return WF_ERR_ALLOC;
    }
    cJSON_AddStringToObject(obj, "subject",
                            req->authed_subject ? req->authed_subject : "");
    cJSON_AddNumberToObject(obj, "kind", (int)req->authed_principal_kind);
    json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) {
        return WF_ERR_ALLOC;
    }
    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
    return WF_OK;
}

static wf_status public_handler(void *ctx, const wf_xrpc_request *req,
                                wf_xrpc_response *resp) {
    (void)ctx;
    (void)req;
    wf_xrpc_response_set_body(resp, "{\"public\":true}", 15);
    return WF_OK;
}

typedef struct rotating_resolver_ctx {
    const char *stale_key;
    const char *current_key;
    int calls;
} rotating_resolver_ctx;

static wf_status rotating_resolver(const char *did, char **out_didkey,
                                   void *ctx) {
    rotating_resolver_ctx *resolver = (rotating_resolver_ctx *)ctx;
    const char *key;
    if (!did || !out_didkey || !resolver) return WF_ERR_INVALID_ARG;
    key = resolver->calls++ == 0 ? resolver->stale_key : resolver->current_key;
    *out_didkey = strdup(key);
    return *out_didkey ? WF_OK : WF_ERR_ALLOC;
}

static char *oauth_access_jwk(const wf_signing_key *key) {
    char *didkey = NULL, *x_b64 = NULL, *y_b64 = NULL, *json = NULL;
    unsigned char *raw = NULL, x[32], y[32];
    size_t raw_len = 0;
    wf_key_type type;
    EC_GROUP *group = NULL;
    EC_POINT *point = NULL;
    BIGNUM *bx = NULL, *by = NULL;

    if (wf_signing_key_public_didkey(key, &didkey) != WF_OK ||
        wf_didkey_decode(didkey, &type, &raw, &raw_len) != WF_OK ||
        type != WF_KEY_TYPE_P256 || raw_len != 33) goto done;
    group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    point = group ? EC_POINT_new(group) : NULL;
    bx = BN_new();
    by = BN_new();
    if (!group || !point || !bx || !by ||
        EC_POINT_oct2point(group, point, raw, raw_len, NULL) != 1 ||
        EC_POINT_get_affine_coordinates(group, point, bx, by, NULL) != 1 ||
        BN_bn2binpad(bx, x, sizeof(x)) != (int)sizeof(x) ||
        BN_bn2binpad(by, y, sizeof(y)) != (int)sizeof(y) ||
        wf_crypto_base64url_encode(x, sizeof(x), &x_b64) != WF_OK ||
        wf_crypto_base64url_encode(y, sizeof(y), &y_b64) != WF_OK) goto done;
    size_t n = strlen(x_b64) + strlen(y_b64) + 64;
    json = malloc(n);
    if (json) snprintf(json, n,
        "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"%s\",\"y\":\"%s\"}",
        x_b64, y_b64);

done:
    free(didkey); free(raw); free(x_b64); free(y_b64);
    BN_free(bx); BN_free(by); EC_POINT_free(point); EC_GROUP_free(group);
    return json;
}

static char *oauth_access_token(const wf_signing_key *key, const char *sub,
                                const char *aud, const char *jkt) {
    cJSON *header = cJSON_CreateObject(), *payload = cJSON_CreateObject();
    cJSON *cnf = cJSON_CreateObject();
    char *hj = NULL, *pj = NULL, *hb = NULL, *pb = NULL, *input = NULL;
    char *sb = NULL, *jwt = NULL;
    unsigned char sig[64];
    int64_t now = (int64_t)time(NULL);
    if (!header || !payload || !cnf) goto done;
    cJSON_AddStringToObject(header, "alg", "ES256");
    cJSON_AddStringToObject(payload, "iss", "https://issuer.example.com");
    cJSON_AddStringToObject(payload, "sub", sub);
    cJSON_AddStringToObject(payload, "aud", aud);
    cJSON_AddNumberToObject(payload, "iat", (double)now);
    cJSON_AddNumberToObject(payload, "exp", (double)(now + 3600));
    cJSON_AddStringToObject(cnf, "jkt", jkt);
    cJSON_AddItemToObject(payload, "cnf", cnf);
    cnf = NULL;
    hj = cJSON_PrintUnformatted(header);
    pj = cJSON_PrintUnformatted(payload);
    if (!hj || !pj ||
        wf_crypto_base64url_encode((unsigned char *)hj, strlen(hj), &hb) != WF_OK ||
        wf_crypto_base64url_encode((unsigned char *)pj, strlen(pj), &pb) != WF_OK)
        goto done;
    size_t input_len = strlen(hb) + strlen(pb) + 2;
    input = malloc(input_len);
    if (!input) goto done;
    snprintf(input, input_len, "%s.%s", hb, pb);
    if (wf_sign(key, (unsigned char *)input, strlen(input), sig, sizeof(sig)) !=
            WF_OK ||
        wf_crypto_base64url_encode(sig, sizeof(sig), &sb) != WF_OK) goto done;
    size_t jwt_len = strlen(input) + strlen(sb) + 2;
    jwt = malloc(jwt_len);
    if (jwt) snprintf(jwt, jwt_len, "%s.%s", input, sb);

done:
    cJSON_Delete(header); cJSON_Delete(payload); cJSON_Delete(cnf);
    free(hj); free(pj); free(hb); free(pb); free(input); free(sb);
    return jwt;
}

/* ------------------------------------------------------------------ */
/* Test                                                                */
/* ------------------------------------------------------------------ */

static int run_test(void) {
    wf_xrpc_server *server = NULL;
    wf_xrpc_client *client = NULL;
    wf_response res = {0};
    char *good_token = NULL;
    char *wrong_aud_token = NULL;
    char *fragment_aud_token = NULL;
    char *expired_token = NULL;
    char *missing_lxm_token = NULL;
    char *tampered_token = NULL;
    char *oauth_jwk = NULL;
    char *oauth_token = NULL;
    char *oauth_proof = NULL;
    char *oauth_auth = NULL;
    wf_oauth_dpop_key *dpop_key = NULL;
    wf_oauth_trusted_keys *trusted_keys = NULL;
    wf_oauth_dpop_replay_cache *replay_cache = NULL;
    int failures = 0;

    const char *protected_nsid = "app.bsky.feed.getFeedSkeleton";
    const char *public_nsid = "io.example.public";

    /* Issuer signing key (P-256) + its did:key. The service token's `iss` is
     * the did:key, so the default resolver recovers the verification key with
     * no network. */
    wf_signing_key key = {0};
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &key) == WF_OK);
    char *issuer_didkey = NULL;
    WF_CHECK(wf_signing_key_public_didkey(&key, &issuer_didkey) == WF_OK);
    WF_CHECK(issuer_didkey != NULL);

    /* A stale key lets the injected resolver exercise verifyJwt-compatible
     * refresh-on-signature-failure behavior. */
    wf_signing_key stale_key = {0};
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &stale_key) == WF_OK);
    char *stale_didkey = NULL;
    WF_CHECK(wf_signing_key_public_didkey(&stale_key, &stale_didkey) == WF_OK);
    rotating_resolver_ctx resolver = {stale_didkey, issuer_didkey, 0};

    /* Server's own DID (for aud checks) — a distinct did:key. */
    wf_signing_key server_key = {0};
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &server_key) == WF_OK);
    char *server_did = NULL;
    WF_CHECK(wf_signing_key_public_didkey(&server_key, &server_did) == WF_OK);
    WF_CHECK(server_did != NULL);

    /* ---- Start server + routes ---- */
    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    WF_CHECK(server != NULL);
    if (!server) {
        goto cleanup_keys;
    }
    uint16_t port = wf_xrpc_server_port(server);
    WF_CHECK(port != 0);
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);

    WF_CHECK(wf_xrpc_server_register_query(server, protected_nsid,
                                           protected_handler, NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, public_nsid,
                                           public_handler, NULL) == WF_OK);

    /* ---- Build a DPoP-bound OAuth access token + proof ---- */
    wf_signing_key oauth_key = {0};
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &oauth_key) == WF_OK);
    oauth_jwk = oauth_access_jwk(&oauth_key);
    WF_CHECK(oauth_jwk != NULL);
    WF_CHECK(wf_oauth_trusted_keys_new(&trusted_keys) == WF_OK);
    WF_CHECK(wf_oauth_trusted_keys_add_jwk(trusted_keys, oauth_jwk) == WF_OK);
    WF_CHECK(wf_oauth_dpop_replay_cache_new(&replay_cache) == WF_OK);
    WF_CHECK(wf_oauth_dpop_key_generate(&dpop_key) == WF_OK);
    char dpop_jkt[44] = {0};
    WF_CHECK(wf_oauth_dpop_key_thumbprint(dpop_key, dpop_jkt) == WF_OK);
    oauth_token = oauth_access_token(&oauth_key, "did:plc:oauth-user",
                                     server_did, dpop_jkt);
    WF_CHECK(oauth_token != NULL);
    char protected_url[192];
    snprintf(protected_url, sizeof(protected_url), "%s/xrpc/%s", base_url,
             protected_nsid);
    wf_oauth_dpop_proof_options proof_opts = {
        "GET", protected_url, NULL, oauth_token, "middleware-proof-1", 0
    };
    WF_CHECK(wf_oauth_dpop_proof_create(dpop_key, &proof_opts, &oauth_proof) ==
             WF_OK);
    oauth_auth = malloc(strlen(oauth_token) + 6);
    WF_CHECK(oauth_auth != NULL);
    if (oauth_auth) snprintf(oauth_auth, strlen(oauth_token) + 6, "DPoP %s",
                             oauth_token);

    /* ---- Configure + attach the middleware ---- */
    wf_xrpc_server_auth_config *cfg = NULL;
    WF_CHECK(wf_xrpc_server_auth_config_new(&cfg) == WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_set_server_did(cfg, server_did) ==
             WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_set_server_origin(cfg, base_url) ==
             WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_protect(cfg, protected_nsid) == WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_set_resolver(cfg, rotating_resolver,
                                                     &resolver) == WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_set_trusted_keys(cfg, trusted_keys) ==
             WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_set_replay_cache(cfg, replay_cache) ==
             WF_OK);
    WF_CHECK(wf_xrpc_server_set_auth_middleware(server, cfg) == WF_OK);
    wf_xrpc_server_auth_config_free(cfg);
    cfg = NULL;

    /* ---- Client ---- */
    client = wf_xrpc_client_new(base_url);
    WF_CHECK(client != NULL);
    if (!client) {
        goto cleanup_server;
    }

    /* ---- Mint tokens ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = server_did;
        req.lxm = protected_nsid;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &good_token) ==
                 WF_OK);
        WF_CHECK(good_token != NULL);
    }

    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = "did:web:someone-else.example.com";
        req.lxm = protected_nsid;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &wrong_aud_token) ==
                 WF_OK);
    }

    {
        size_t aud_len = strlen(server_did) + strlen("#other-service") + 1;
        char *fragment_aud = malloc(aud_len);
        WF_CHECK(fragment_aud != NULL);
        if (fragment_aud) {
            snprintf(fragment_aud, aud_len, "%s#other-service", server_did);
            wf_service_auth_request req = {0};
            req.iss = issuer_didkey;
            req.aud = fragment_aud;
            req.lxm = protected_nsid;
            WF_CHECK(wf_server_create_service_auth(&req, &key,
                                                   &fragment_aud_token) ==
                     WF_OK);
        }
        free(fragment_aud);
    }

    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = server_did;
        req.lxm = protected_nsid;
        req.exp = 1000000000; /* 2001-09-09, safely in the past */
        WF_CHECK(wf_server_create_service_auth(&req, &key, &expired_token) ==
                 WF_OK);
    }

    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = server_did;
        WF_CHECK(wf_server_create_service_auth(&req, &key,
                                               &missing_lxm_token) == WF_OK);
    }

    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = server_did;
        req.lxm = protected_nsid;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &tampered_token) ==
                 WF_OK);
        /* Flip a char in the payload segment to break the signature. */
        char *d1 = strchr(tampered_token, '.');
        char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
        if (d1 && d2 && d2 > d1 + 1) {
            char *target = d1 + 1;
            *target = (*target == 'A') ? 'B' : 'A';
        }
    }

    #define SET_AUTH(tok) wf_xrpc_client_set_auth(client, (tok))

    /* ---- 1. Protected route WITH a valid token → 200, subject propagated ---- */
    {
        SET_AUTH(good_token);
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 200);
        WF_CHECK(s == WF_OK);
        WF_CHECK(resolver.calls == 2);
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        if (!root) {
            fprintf(stderr, "FAIL: protected reply not JSON\n");
            failures++;
        } else {
            cJSON *sub = cJSON_GetObjectItemCaseSensitive(root, "subject");
            cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
            if (!sub || !cJSON_IsString(sub) ||
                strcmp(sub->valuestring, issuer_didkey) != 0) {
                fprintf(stderr, "FAIL: subject mismatch: %s\n",
                        sub && sub->valuestring ? sub->valuestring : "NULL");
                failures++;
            }
            if (!kind || !cJSON_IsNumber(kind) ||
                (int)kind->valuedouble != (int)WF_XRPC_PRINCIPAL_SERVICE) {
                fprintf(stderr, "FAIL: principal kind mismatch\n");
                failures++;
            }
            cJSON_Delete(root);
        }
        wf_response_free(&res);
    }

    /* ---- 2. Protected route WITHOUT a token → 401 ---- */
    {
        SET_AUTH(NULL);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 3. Protected route WITH wrong-aud token → 401 ---- */
    {
        SET_AUTH(wrong_aud_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 4. Protected route WITH a different audience fragment → 401 ---- */
    {
        SET_AUTH(fragment_aud_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 5. Protected route WITH expired token → 401 ---- */
    {
        SET_AUTH(expired_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 6. Protected route WITH tampered-signature token → 401 ---- */
    {
        SET_AUTH(tampered_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 7. Protected route WITH no lxm claim → 401 ---- */
    {
        SET_AUTH(missing_lxm_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 8. Public route WITHOUT a token → 200 ---- */
    {
        SET_AUTH(NULL);
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client, public_nsid, NULL, &res);
        WF_CHECK(res.status == 200);
        WF_CHECK(s == WF_OK);
        wf_response_free(&res);
    }

    /* ---- 9. DPoP-bound OAuth token → user principal; replay → 401 ---- */
    {
        wf_http_header headers[] = {
            {"Authorization", oauth_auth}, {"DPoP", oauth_proof}
        };
        wf_response_free(&res);
        wf_status s = wf_http_get_with_headers(client, protected_url, headers,
                                               2, &res);
        WF_CHECK(s == WF_OK);
        WF_CHECK(res.status == 200);
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        cJSON *sub = root ? cJSON_GetObjectItemCaseSensitive(root, "subject") : NULL;
        cJSON *kind = root ? cJSON_GetObjectItemCaseSensitive(root, "kind") : NULL;
        WF_CHECK(sub && cJSON_IsString(sub) &&
                 strcmp(sub->valuestring, "did:plc:oauth-user") == 0);
        WF_CHECK(kind && cJSON_IsNumber(kind) &&
                 (int)kind->valuedouble == (int)WF_XRPC_PRINCIPAL_USER);
        cJSON_Delete(root);
        wf_response_free(&res);

        wf_http_get_with_headers(client, protected_url, headers, 2, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    #undef SET_AUTH

cleanup_server:
    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);
cleanup_keys:
    free(issuer_didkey);
    free(stale_didkey);
    free(server_did);
    free(good_token);
    free(wrong_aud_token);
    free(fragment_aud_token);
    free(expired_token);
    free(missing_lxm_token);
    free(tampered_token);
    free(oauth_jwk);
    free(oauth_token);
    free(oauth_proof);
    free(oauth_auth);
    wf_oauth_dpop_key_free(dpop_key);
    wf_oauth_trusted_keys_free(trusted_keys);
    wf_oauth_dpop_replay_cache_free(replay_cache);

    if (failures == 0) {
        printf("PASS: XRPC server auth middleware\n");
        return 0;
    }
    return 1;
}

int main(void) {
    int rc = run_test();
    WF_CHECK(rc == 0);
    WF_TEST_SUMMARY();
}
