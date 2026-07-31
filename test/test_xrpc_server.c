/*
 * test_xrpc_server.c — integration test for the XRPC server module.
 *
 * Starts a local XRPC server, registers a handler, issues a query via the
 * XRPC client, and verifies the round-trip.
 */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Test handler                                                        */
/* ------------------------------------------------------------------ */
static wf_status test_query_handler(void *ctx, const wf_xrpc_request *req,
                                     wf_xrpc_response *resp) {
    (void)ctx;
    char *json;

    /* Verify the NSID was parsed correctly */
    if (!req->nsid || strcmp(req->nsid, "io.example.ping") != 0) {
        wf_xrpc_response_set_error(resp, 400, "BadNSID", "unexpected nsid");
        return WF_OK;
    }

    if (!req->params) {
        wf_xrpc_response_set_error(resp, 400, "MissingQuery",
                                    "no params received");
        return WF_OK;
    }

    /* Echo back the 'msg' query param */
    cJSON *msg = cJSON_GetObjectItemCaseSensitive(req->params, "msg");
    const char *val = (msg && cJSON_IsString(msg) && msg->valuestring)
                          ? msg->valuestring
                          : "default";

    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return WF_ERR_ALLOC;
    }
    cJSON_AddStringToObject(obj, "reply", val);
    json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) {
        return WF_ERR_ALLOC;
    }
    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
    return WF_OK;
}

static wf_status test_auth_handler(void *ctx, const wf_xrpc_request *req,
                                    wf_xrpc_response *resp) {
    (void)ctx;
    if (!req->auth_header) {
        wf_xrpc_response_set_error(resp, 401, "AuthRequired",
                                    "Authorization header required");
        return WF_OK;
    }
    char *json;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return WF_ERR_ALLOC;
    }
    cJSON_AddStringToObject(obj, "auth", req->auth_header);
    if (req->dpop_header) {
        cJSON_AddStringToObject(obj, "dpop", req->dpop_header);
    }
    json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) {
        return WF_ERR_ALLOC;
    }
    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
    return WF_OK;
}

static wf_status test_proc_handler(void *ctx, const wf_xrpc_request *req,
                                    wf_xrpc_response *resp) {
    (void)ctx;
    char *json;

    if (!req->params) {
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "status", "ok");
        json = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
    } else {
        cJSON_AddItemToObject(req->params, "status",
                              cJSON_CreateString("received"));
        json = cJSON_PrintUnformatted(req->params);
    }
    if (!json) {
        return WF_ERR_ALLOC;
    }
    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
    return WF_OK;
}

static wf_status test_http_handler(void *ctx, const wf_xrpc_request *req,
                                    wf_xrpc_response *resp) {
    (void)ctx;
    if (!req->path || strncmp(req->path, "/oauth/", 7) != 0)
        return WF_ERR_INVALID_ARG;
    if (strcmp(req->method, "POST") == 0) {
        const char expected[] = "grant_type=authorization_code&code=abc";
        if (!req->content_type ||
            strncmp(req->content_type, "application/x-www-form-urlencoded",
                    33) != 0 || req->body_len != strlen(expected) ||
            memcmp(req->body, expected, strlen(expected)) != 0)
            return WF_ERR_PARSE;
        resp->http_status = 201;
        wf_xrpc_response_add_header(resp, "DPoP-Nonce", "next-nonce");
    } else {
        cJSON *client = req->params
            ? cJSON_GetObjectItemCaseSensitive(req->params, "client_id") : NULL;
        if (!cJSON_IsString(client) || strcmp(client->valuestring, "native") != 0)
            return WF_ERR_PARSE;
        wf_xrpc_response_add_header(resp, "Set-Cookie",
                                    "mb_device=xyz; Path=/; HttpOnly");
    }
    wf_xrpc_response_add_header(resp, "Cache-Control", "no-store");
    /*
     * Echo the Cookie header into the body too, not just check it via the
     * client's captured Set-Cookie: the body proves what the HANDLER
     * received, the client-side capture proves what came back over the
     * wire, and a bug could break either independently.
     * is the only place this test can actually see what the handler
     * received — proving the cookie reached the handler, not just that the
     * server accepted the request.
     */
    if (req->cookie_header) {
        char body[256];
        int n = snprintf(body, sizeof(body), "{\"ok\":true,\"cookie\":\"%s\"}",
                         req->cookie_header);
        wf_xrpc_response_set_body(resp, body, (size_t)n);
    } else {
        wf_xrpc_response_set_body(resp, "{\"ok\":true}", 11);
    }
    return WF_OK;
}

/* Fallback handler: echoes the fields an AppView-style proxy needs
 * (nsid, method, raw query, atproto-proxy header, body) so the test can
 * assert they arrive intact for unregistered NSIDs. */
static wf_status test_fallback_handler(void *ctx, const wf_xrpc_request *req,
                                        wf_xrpc_response *resp) {
    (void)ctx;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return WF_ERR_ALLOC;
    cJSON_AddStringToObject(obj, "fallback", "yes");
    cJSON_AddStringToObject(obj, "nsid", req->nsid ? req->nsid : "");
    cJSON_AddStringToObject(obj, "method", req->method ? req->method : "");
    if (req->raw_query)
        cJSON_AddStringToObject(obj, "raw_query", req->raw_query);
    if (req->atproto_proxy)
        cJSON_AddStringToObject(obj, "atproto_proxy", req->atproto_proxy);
    if (req->body && req->body_len)
        cJSON_AddStringToObject(obj, "body_len", "set");
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return WF_ERR_ALLOC;
    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Test runner                                                         */
/* ------------------------------------------------------------------ */
static int run_test(void) {
    wf_xrpc_server *server = NULL;
    wf_xrpc_client *client = NULL;
    wf_response res = {0};
    int failures = 0;

    /* Start server */
    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: wf_xrpc_server_start returned NULL\n");
        return 1;
    }

    uint16_t port = wf_xrpc_server_port(server);
    if (port == 0) {
        fprintf(stderr, "FAIL: server port is 0\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Register routes */
    if (wf_xrpc_server_register_query(server, "io.example.ping",
                                       test_query_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: register ping\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    if (wf_xrpc_server_register_query(server, "io.example.auth",
                                       test_auth_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: register auth\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    if (wf_xrpc_server_register_procedure(server, "io.example.echo",
                                           test_proc_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: register proc\n");
        wf_xrpc_server_free(server);
        return 1;
    }
    if (wf_xrpc_server_register_http_route(server, "GET", "/oauth/test",
            test_http_handler, NULL) != WF_OK ||
        wf_xrpc_server_register_http_route(server, "POST", "/oauth/token",
            test_http_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: register generic HTTP routes\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Build base URL */
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);

    client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "FAIL: wf_xrpc_client_new\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Test 1: Basic query with parameter */
    {
        wf_xrpc_param params[] = {{"msg", "hello"}};
        wf_response_free(&res);
        wf_status s = wf_xrpc_query_params(client, "io.example.ping",
                                            params, 1, &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: query ping (status=%d)\n", (int)s);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: ping status=%ld\n", res.status);
            failures++;
        } else {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            if (!root) {
                fprintf(stderr, "FAIL: ping parse error\n");
                failures++;
            } else {
                cJSON *reply = cJSON_GetObjectItemCaseSensitive(root, "reply");
                if (!reply || !cJSON_IsString(reply) ||
                    strcmp(reply->valuestring, "hello") != 0) {
                    fprintf(stderr, "FAIL: ping reply mismatch\n");
                    failures++;
                }
                cJSON_Delete(root);
            }
        }
        wf_response_free(&res);
    }

    /* Test 2: Auth header echo */
    {
        char url[160];
        wf_http_header headers[] = {
            {"Authorization", "Bearer test-token"},
            {"DPoP", "test-proof"},
        };
        snprintf(url, sizeof(url), "%s/xrpc/io.example.auth", base_url);
        wf_response_free(&res);
        wf_status s = wf_http_get_with_headers(client, url, headers, 2, &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: query auth (status=%d)\n", (int)s);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: auth status=%ld\n", res.status);
            failures++;
        } else {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            if (!root) {
                fprintf(stderr, "FAIL: auth parse error\n");
                failures++;
            } else {
                cJSON *auth = cJSON_GetObjectItemCaseSensitive(root, "auth");
                cJSON *dpop = cJSON_GetObjectItemCaseSensitive(root, "dpop");
                if (!auth || !cJSON_IsString(auth) || !strstr(auth->valuestring, "test-token")) {
                    fprintf(stderr, "FAIL: auth mismatch: %s\n",
                            auth && auth->valuestring ? auth->valuestring : "NULL");
                    failures++;
                }
                if (!dpop || !cJSON_IsString(dpop) ||
                    strcmp(dpop->valuestring, "test-proof") != 0) {
                    fprintf(stderr, "FAIL: DPoP header mismatch\n");
                    failures++;
                }
                cJSON_Delete(root);
            }
        }
        wf_response_free(&res);
    }

    /*
     * Test 2b: Cookie header reaches an HTTP route.
     *
     * A browser-facing HTTP route (an OAuth authorize page, say) has no
     * Authorization header to carry a session — a cookie is what stands in
     * for one. Regression coverage for that path specifically, since it goes
     * through the plain-HTTP-route branch of the dispatcher rather than the
     * XRPC one the auth-header test above exercises.
     */
    {
        char url[160];
        wf_http_header headers[] = {
            {"Cookie", "mb_device=abc123; other=ignored"},
        };
        snprintf(url, sizeof(url), "%s/oauth/test?client_id=native", base_url);
        wf_response_free(&res);
        wf_status s = wf_http_get_with_headers(client, url, headers, 1, &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: cookie query (status=%d)\n", (int)s);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: cookie status=%ld\n", res.status);
            failures++;
        } else {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            cJSON *cookie = root
                ? cJSON_GetObjectItemCaseSensitive(root, "cookie") : NULL;
            if (!cookie || !cJSON_IsString(cookie) ||
                strcmp(cookie->valuestring,
                       "mb_device=abc123; other=ignored") != 0) {
                fprintf(stderr, "FAIL: cookie header mismatch: %s\n",
                        cookie && cookie->valuestring ? cookie->valuestring
                                                      : "NULL");
                failures++;
            }
            cJSON_Delete(root);
            if (!res.set_cookie ||
                strcmp(res.set_cookie, "mb_device=xyz; Path=/; HttpOnly") != 0) {
                fprintf(stderr, "FAIL: captured Set-Cookie mismatch: %s\n",
                        res.set_cookie ? res.set_cookie : "NULL");
                failures++;
            }
        }
        wf_response_free(&res);
    }

    /* Test 3: Procedure */
    {
        wf_response_free(&res);
        wf_status s = wf_xrpc_procedure(client, "io.example.echo",
                                         "{\"custom\":\"data\"}", &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: procedure (status=%d)\n", (int)s);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: procedure status=%ld\n", res.status);
            failures++;
        } else {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            if (!root) {
                fprintf(stderr, "FAIL: procedure parse error\n");
                failures++;
            } else {
                cJSON *custom = cJSON_GetObjectItemCaseSensitive(root, "custom");
                if (!custom || !cJSON_IsString(custom) ||
                    strcmp(custom->valuestring, "data") != 0) {
                    fprintf(stderr, "FAIL: procedure body mismatch\n");
                    failures++;
                }
                cJSON_Delete(root);
            }
        }
        wf_response_free(&res);
    }

    /* Test 4: Unregistered NSID returns 501 MethodNotImplemented (atproto spec) */
    {
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client, "io.example.missing", NULL, &res);
        if (s != WF_ERR_HTTP) {
            fprintf(stderr, "FAIL: missing expected WF_ERR_HTTP, got %d\n", (int)s);
            failures++;
        } else if (res.status != 501) {
            fprintf(stderr, "FAIL: missing expected 501, got %ld\n", res.status);
            failures++;
        }
        wf_response_free(&res);
    }

    /* Test 4b: an installed fallback receives unregistered NSIDs verbatim
     * (raw query, atproto-proxy header, body), wrong-method requests on a
     * registered NSID still error, and removing the fallback restores 501. */
    {
        wf_xrpc_server_set_fallback(server, test_fallback_handler, NULL);

        /* GET with raw query + atproto-proxy header preserved. */
        char url[200];
        snprintf(url, sizeof(url),
                 "%s/xrpc/io.example.unknown?a=1&b=two%%20words", base_url);
        wf_http_header hdrs[] = {
            {"atproto-proxy", "did:web:svc.example#bsky_appview"},
        };
        wf_status s = wf_http_get_with_headers(client, url, hdrs, 1, &res);
        cJSON *root = (s == WF_OK && res.body)
            ? cJSON_ParseWithLength(res.body, res.body_len) : NULL;
        if (!root) {
            fprintf(stderr, "FAIL: fallback GET status=%d http=%ld\n",
                    (int)s, res.status);
            failures++;
        } else {
            cJSON *raw = cJSON_GetObjectItemCaseSensitive(root, "raw_query");
            cJSON *pxy = cJSON_GetObjectItemCaseSensitive(root,
                                                          "atproto_proxy");
            if (!cJSON_IsString(raw) ||
                strcmp(raw->valuestring, "a=1&b=two words") != 0) {
                fprintf(stderr, "FAIL: fallback raw_query mismatch: got '%s'\n",
                        cJSON_IsString(raw) ? raw->valuestring : "(null)");
                failures++;
            }
            if (!cJSON_IsString(pxy) ||
                strcmp(pxy->valuestring, "did:web:svc.example#bsky_appview")
                    != 0) {
                fprintf(stderr, "FAIL: fallback atproto_proxy mismatch\n");
                failures++;
            }
            cJSON_Delete(root);
        }
        wf_response_free(&res);

        /* POST to an unknown NSID reaches the fallback with its body. */
        s = wf_xrpc_procedure(client, "io.example.unknownProc",
                              "{\"x\":1}", &res);
        root = (s == WF_OK && res.body)
            ? cJSON_ParseWithLength(res.body, res.body_len) : NULL;
        if (!root ||
            !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root,
                                                             "body_len"))) {
            fprintf(stderr, "FAIL: fallback POST status=%d http=%ld\n",
                    (int)s, res.status);
            failures++;
        }
        if (root) cJSON_Delete(root);
        wf_response_free(&res);

        /* Wrong method on a REGISTERED NSID keeps the 400 error. */
        s = wf_xrpc_procedure(client, "io.example.ping", "{}", &res);
        if (s != WF_ERR_HTTP || res.status != 400) {
            fprintf(stderr, "FAIL: wrong-method expected 400, got %d/%ld\n",
                    (int)s, res.status);
            failures++;
        }
        wf_response_free(&res);

        /* Removing the fallback restores the 501. */
        wf_xrpc_server_set_fallback(server, NULL, NULL);
        s = wf_xrpc_query(client, "io.example.missing", NULL, &res);
        if (s != WF_ERR_HTTP || res.status != 501) {
            fprintf(stderr, "FAIL: fallback removal expected 501, got %d/%ld\n",
                    (int)s, res.status);
            failures++;
        }
        wf_response_free(&res);
    }

    /* Test 5: exact-path GET/POST routes preserve form bodies and headers. */
    {
        char url[160];
        snprintf(url, sizeof(url), "%s/oauth/test?client_id=native", base_url);
        wf_status s = wf_http_get(client, url, &res);
        if (s != WF_OK || res.status != 200 || !res.body ||
            strcmp(res.body, "{\"ok\":true}") != 0) {
            fprintf(stderr, "FAIL: generic HTTP GET status=%d http=%ld\n",
                    (int)s, res.status);
            failures++;
        }
        wf_response_free(&res);

        snprintf(url, sizeof(url), "%s/oauth/token", base_url);
        s = wf_http_post(client, url, "application/x-www-form-urlencoded",
            "grant_type=authorization_code&code=abc", NULL, 0, &res);
        if (s != WF_OK || res.status != 201 || !res.dpop_nonce ||
            strcmp(res.dpop_nonce, "next-nonce") != 0) {
            fprintf(stderr, "FAIL: generic HTTP POST status=%d http=%ld\n",
                    (int)s, res.status);
            failures++;
        }
        wf_response_free(&res);

        s = wf_http_get(client, url, &res);
        if (s != WF_ERR_HTTP || res.status != 405) {
            fprintf(stderr, "FAIL: generic wrong method status=%d http=%ld\n",
                    (int)s, res.status);
            failures++;
        }
        wf_response_free(&res);
    }

    /* Cleanup */
    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);

    if (failures == 0) {
        printf("PASS: XRPC server round-trip\n");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Rate limiter tests                                                  */
/* ------------------------------------------------------------------ */

static int test_rate_limiter_basic(void) {
    wf_rate_limiter *rl;
    unsigned int retry;

    /* Invalid args */
    if (wf_rate_limiter_new(0, 1, 0) != NULL) {
        fprintf(stderr, "FAIL: rl new with 0 points should fail\n");
        return 1;
    }
    if (wf_rate_limiter_new(1, 0, 0) != NULL) {
        fprintf(stderr, "FAIL: rl new with 0 duration should fail\n");
        return 1;
    }

    rl = wf_rate_limiter_new(3, 1, 0); /* 3 tokens, 1 sec window */
    if (!rl) {
        fprintf(stderr, "FAIL: rl new returned NULL\n");
        return 1;
    }

    /* Consume 3 tokens — all should succeed */
    if (wf_rate_limiter_consume(rl, "alice", 1, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: rl consume 1\n");
        wf_rate_limiter_free(rl);
        return 1;
    }
    if (wf_rate_limiter_consume(rl, "alice", 1, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: rl consume 2\n");
        wf_rate_limiter_free(rl);
        return 1;
    }
    if (wf_rate_limiter_consume(rl, "alice", 1, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: rl consume 3\n");
        wf_rate_limiter_free(rl);
        return 1;
    }

    /* 4th should fail — bucket empty */
    if (wf_rate_limiter_consume(rl, "alice", 1, &retry) != WF_ERR_RATE_LIMIT) {
        fprintf(stderr, "FAIL: rl consume 4 should be rate limited\n");
        wf_rate_limiter_free(rl);
        return 1;
    }
    if (retry == 0) {
        fprintf(stderr, "FAIL: rl retry-after should be >0\n");
        wf_rate_limiter_free(rl);
        return 1;
    }

    /* Different key should still work */
    if (wf_rate_limiter_consume(rl, "bob", 1, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: rl different key should succeed\n");
        wf_rate_limiter_free(rl);
        return 1;
    }

    wf_rate_limiter_free(rl);
    printf("PASS: rate limiter basic\n");
    return 0;
}

static int test_rate_limiter_refill(void) {
    wf_rate_limiter *rl;
    unsigned int retry;

    /* 2 tokens, 2 sec window — 1 token/sec refill */
    rl = wf_rate_limiter_new(2, 2, 0);
    if (!rl) {
        fprintf(stderr, "FAIL: rl refill new failed\n");
        return 1;
    }

    /* Use all 2 tokens */
    if (wf_rate_limiter_consume(rl, "key", 2, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: rl refill consume 2\n");
        wf_rate_limiter_free(rl);
        return 1;
    }
    /* Bucket empty — should fail */
    if (wf_rate_limiter_consume(rl, "key", 1, &retry) != WF_ERR_RATE_LIMIT) {
        fprintf(stderr, "FAIL: rl refill should be empty\n");
        wf_rate_limiter_free(rl);
        return 1;
    }

    /* Wait for the bucket to refill. At least one token must appear. */
    sleep(1);
    if (wf_rate_limiter_consume(rl, "key", 1, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: rl refill after 1s should have 1 token\n");
        wf_rate_limiter_free(rl);
        return 1;
    }

    /*
     * Drain whatever else refilled, then assert the bucket is empty.
     *
     * Do not assume the sleep granted exactly one token: sleep(1) is a lower
     * bound, and on a loaded machine it returns late enough for the full 2 to
     * refill — which made this fail roughly one parallel run in two hundred.
     * The property worth testing is that the bucket empties and then refuses,
     * not that a specific number of tokens accrued in wall-clock time. The
     * drain is bounded by the capacity so a limiter that never refuses fails
     * here rather than looping.
     */
    {
        int drained = 0;
        while (wf_rate_limiter_consume(rl, "key", 1, &retry) == WF_OK) {
            if (++drained > 2) {
                fprintf(stderr, "FAIL: rl handed out more than its capacity\n");
                wf_rate_limiter_free(rl);
                return 1;
            }
        }
    }
    /* Empty now — and an immediate retry must still be refused. */
    if (wf_rate_limiter_consume(rl, "key", 1, &retry) != WF_ERR_RATE_LIMIT) {
        fprintf(stderr, "FAIL: rl refill should be empty again\n");
        wf_rate_limiter_free(rl);
        return 1;
    }

    wf_rate_limiter_free(rl);
    printf("PASS: rate limiter refill\n");
    return 0;
}

static int test_server_rate_limit(void) {
    wf_xrpc_server *server;
    wf_xrpc_client *client;
    wf_rate_limiter *rl;
    wf_response res = {0};
    int failures = 0;

    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: srv rate limit start\n");
        return 1;
    }

    /* Register handler and attach rate limiter */
    if (wf_xrpc_server_register_query(server, "io.example.ping",
                                       test_query_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: srv rate limit register\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* 2 tokens, 60 sec window — effectively 2 requests then blocked */
    rl = wf_rate_limiter_new(2, 60, 0);
    wf_xrpc_server_set_rate_limiter(server, rl);

    uint16_t port = wf_xrpc_server_port(server);
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);
    client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "FAIL: srv rate limit client\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* First 2 requests should succeed */
    wf_status s;
    wf_xrpc_param params[] = {{"msg", "ok"}};

    s = wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
    if (s != WF_OK || res.status != 200) {
        fprintf(stderr, "FAIL: srv rate limit req 1: status=%d http=%ld\n",
                (int)s, res.status);
        failures++;
    }
    wf_response_free(&res);

    s = wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
    if (s != WF_OK || res.status != 200) {
        fprintf(stderr, "FAIL: srv rate limit req 2: status=%d http=%ld\n",
                (int)s, res.status);
        failures++;
    }
    wf_response_free(&res);

    /* 3rd should get 429 */
    s = wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
    if (s != WF_ERR_HTTP || res.status != 429) {
        fprintf(stderr, "FAIL: srv rate limit req 3: expected 429, "
                "got status=%d http=%ld\n", (int)s, res.status);
        failures++;
    }
    wf_response_free(&res);

    /* Detach rate limiter — all requests should succeed again */
    wf_xrpc_server_set_rate_limiter(server, NULL);
    s = wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
    if (s != WF_OK || res.status != 200) {
        fprintf(stderr, "FAIL: srv rate limit after detach: status=%d http=%ld\n",
                (int)s, res.status);
        failures++;
    }
    wf_response_free(&res);

    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);
    wf_rate_limiter_free(rl);

    if (failures == 0) {
        printf("PASS: server rate limit\n");
        return 0;
    }
    return 1;
}

int main(void) {
    int failures = 0;

    failures += run_test();
    failures += test_rate_limiter_basic();
    failures += test_rate_limiter_refill();
    failures += test_server_rate_limit();

    return failures;
}
