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
#include "test.h"

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

/* ------------------------------------------------------------------ */
/* Test                                                                */
/* ------------------------------------------------------------------ */

static int run_test(void) {
    wf_xrpc_server *server = NULL;
    wf_xrpc_client *client = NULL;
    wf_response res = {0};
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

    WF_CHECK(wf_xrpc_server_register_query(server, protected_nsid,
                                           protected_handler, NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, public_nsid,
                                           public_handler, NULL) == WF_OK);

    /* ---- Configure + attach the middleware ---- */
    wf_xrpc_server_auth_config *cfg = NULL;
    WF_CHECK(wf_xrpc_server_auth_config_new(&cfg) == WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_set_server_did(cfg, server_did) ==
             WF_OK);
    WF_CHECK(wf_xrpc_server_auth_config_protect(cfg, protected_nsid) == WF_OK);
    WF_CHECK(wf_xrpc_server_set_auth_middleware(server, cfg) == WF_OK);
    wf_xrpc_server_auth_config_free(cfg);
    cfg = NULL;

    /* ---- Client ---- */
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);
    client = wf_xrpc_client_new(base_url);
    WF_CHECK(client != NULL);
    if (!client) {
        goto cleanup_server;
    }

    /* ---- Mint tokens ---- */
    char *good_token = NULL;
    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = server_did;
        req.lxm = protected_nsid;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &good_token) ==
                 WF_OK);
        WF_CHECK(good_token != NULL);
    }

    char *wrong_aud_token = NULL;
    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = "did:web:someone-else.example.com";
        req.lxm = protected_nsid;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &wrong_aud_token) ==
                 WF_OK);
    }

    char *expired_token = NULL;
    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = server_did;
        req.lxm = protected_nsid;
        req.exp = 1000000000; /* 2001-09-09, safely in the past */
        WF_CHECK(wf_server_create_service_auth(&req, &key, &expired_token) ==
                 WF_OK);
    }

    char *missing_lxm_token = NULL;
    {
        wf_service_auth_request req = {0};
        req.iss = issuer_didkey;
        req.aud = server_did;
        WF_CHECK(wf_server_create_service_auth(&req, &key,
                                               &missing_lxm_token) == WF_OK);
    }

    char *tampered_token = NULL;
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

    /* ---- 4. Protected route WITH expired token → 401 ---- */
    {
        SET_AUTH(expired_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 5. Protected route WITH tampered-signature token → 401 ---- */
    {
        SET_AUTH(tampered_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 6. Protected route WITH no lxm claim → 401 ---- */
    {
        SET_AUTH(missing_lxm_token);
        wf_response_free(&res);
        wf_xrpc_query(client, protected_nsid, NULL, &res);
        WF_CHECK(res.status == 401);
        wf_response_free(&res);
    }

    /* ---- 7. Public route WITHOUT a token → 200 ---- */
    {
        SET_AUTH(NULL);
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client, public_nsid, NULL, &res);
        WF_CHECK(res.status == 200);
        WF_CHECK(s == WF_OK);
        wf_response_free(&res);
    }

    #undef SET_AUTH

cleanup_server:
    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);
cleanup_keys:
    free(issuer_didkey);
    free(server_did);
    free(good_token);
    free(wrong_aud_token);
    free(expired_token);
    free(missing_lxm_token);
    free(tampered_token);

    if (failures == 0) {
        printf("PASS: XRPC server auth middleware\n");
        return 0;
    }
    return 1;
}

int main(void) {
    int rc = run_test();
    WF_TEST_SUMMARY();
}
