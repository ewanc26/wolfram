/*
 * test_xrpc_server.c — integration test for the XRPC server module.
 *
 * Starts a local XRPC server, registers a handler, issues a query via the
 * XRPC client, and verifies the round-trip.
 */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
    if (strcmp(req->method, "GET") == 0 &&
        strcmp(req->path, "/oauth/redirect-test") == 0) {
        wf_xrpc_response_add_header(resp, "Location",
                                    "/oauth/consent?client_id=native");
        resp->http_status = 302;
        return WF_OK;
    }
    if (strcmp(req->method, "POST") == 0) {
        const char expected[] = "grant_type=authorization_code&code=abc";
        if (!req->content_type ||
            strncmp(req->content_type, "application/x-www-form-urlencoded",
                    33) != 0 ||
            req->body_len != strlen(expected) ||
            memcmp(req->body, expected, strlen(expected)) != 0)
            return WF_ERR_PARSE;
        resp->http_status = 201;
        wf_xrpc_response_add_header(resp, "DPoP-Nonce", "next-nonce");
    } else {
        cJSON *client =
            req->params
                ? cJSON_GetObjectItemCaseSensitive(req->params, "client_id")
                : NULL;
        if (!cJSON_IsString(client) ||
            strcmp(client->valuestring, "native") != 0)
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
 * (nsid, method, raw query, atproto-proxy header, content-negotiation
 * headers, x-atproto-* passthrough headers, body) so the test can assert
 * they arrive intact for unregistered NSIDs. */
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
    if (req->accept_language)
        cJSON_AddStringToObject(obj, "accept_language", req->accept_language);
    if (req->accept_encoding)
        cJSON_AddStringToObject(obj, "accept_encoding", req->accept_encoding);
    if (req->atproto_accept_labelers)
        cJSON_AddStringToObject(obj, "atproto_accept_labelers",
                                req->atproto_accept_labelers);
    if (req->x_bsky_topics)
        cJSON_AddStringToObject(obj, "x_bsky_topics", req->x_bsky_topics);
    if (req->atproto_headers && req->atproto_headers_count > 0) {
        cJSON *arr = cJSON_CreateArray();
        if (arr) {
            for (size_t i = 0; i < req->atproto_headers_count; i++) {
                cJSON *pair = cJSON_CreateObject();
                if (!pair) continue;
                cJSON_AddStringToObject(pair, "name",
                                        req->atproto_headers[i].name);
                cJSON_AddStringToObject(pair, "value",
                                        req->atproto_headers[i].value);
                cJSON_AddItemToArray(arr, pair);
            }
            cJSON_AddItemToObject(obj, "atproto_headers", arr);
        }
    }
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
                                           test_http_handler, NULL) != WF_OK ||
        wf_xrpc_server_register_http_route(server, "GET",
                                           "/oauth/redirect-test",
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
        wf_status s =
            wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
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
                if (!auth || !cJSON_IsString(auth) ||
                    !strstr(auth->valuestring, "test-token")) {
                    fprintf(stderr, "FAIL: auth mismatch: %s\n",
                            auth && auth->valuestring ? auth->valuestring
                                                      : "NULL");
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
            cJSON *cookie =
                root ? cJSON_GetObjectItemCaseSensitive(root, "cookie") : NULL;
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
                strcmp(res.set_cookie, "mb_device=xyz; Path=/; HttpOnly") !=
                    0) {
                fprintf(stderr, "FAIL: captured Set-Cookie mismatch: %s\n",
                        res.set_cookie ? res.set_cookie : "NULL");
                failures++;
            }
        }
        wf_response_free(&res);
    }

    /*
     * Test 2c: Location header on a redirect.
     *
     * A 3xx response makes wf_http_get_with_headers return WF_ERR_HTTP, so
     * `res.status` alone already says a redirect happened — but not where
     * to. MetalBear's OAuth authorize endpoint answers every outcome with a
     * 302 (success redirects to the client, a blocked attempt redirects to
     * sign-in), so telling those apart is exactly what a caller needs
     * Location for.
     */
    {
        char url[160];
        snprintf(url, sizeof(url), "%s/oauth/redirect-test", base_url);
        wf_response_free(&res);
        wf_status s = wf_http_get_with_headers(client, url, NULL, 0, &res);
        if (s != WF_ERR_HTTP) {
            fprintf(stderr, "FAIL: redirect query (status=%d)\n", (int)s);
            failures++;
        } else if (res.status != 302) {
            fprintf(stderr, "FAIL: redirect status=%ld\n", res.status);
            failures++;
        } else if (!res.location ||
                   strcmp(res.location, "/oauth/consent?client_id=native") !=
                       0) {
            fprintf(stderr, "FAIL: captured Location mismatch: %s\n",
                    res.location ? res.location : "NULL");
            failures++;
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
                cJSON *custom =
                    cJSON_GetObjectItemCaseSensitive(root, "custom");
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

    /* The native MHD shim must keep method/URL metadata separate from the
     * receive buffer once a POST spans more than its first socket read. */
    {
        const size_t payload_len = 64 * 1024;
        char *json = malloc(payload_len + 32);
        if (!json) {
            failures++;
        } else {
            memcpy(json, "{\"payload\":\"", 12);
            memset(json + 12, 'a', payload_len);
            memcpy(json + 12 + payload_len, "\"}", 3);
            wf_status s =
                wf_xrpc_procedure(client, "io.example.echo", json, &res);
            if (s != WF_OK || res.status != 200 || !res.body ||
                res.body_len < payload_len) {
                fprintf(stderr,
                        "FAIL: multi-chunk procedure status=%d http=%ld\n",
                        (int)s, res.status);
                failures++;
            }
            wf_response_free(&res);
            free(json);
        }
    }

    /* Test 4: Unregistered NSID returns 501 MethodNotImplemented (atproto spec)
     */
    {
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client, "io.example.missing", NULL, &res);
        if (s != WF_ERR_HTTP) {
            fprintf(stderr, "FAIL: missing expected WF_ERR_HTTP, got %d\n",
                    (int)s);
            failures++;
        } else if (res.status != 501) {
            fprintf(stderr, "FAIL: missing expected 501, got %ld\n",
                    res.status);
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
                          ? cJSON_ParseWithLength(res.body, res.body_len)
                          : NULL;
        if (!root) {
            fprintf(stderr, "FAIL: fallback GET status=%d http=%ld\n", (int)s,
                    res.status);
            failures++;
        } else {
            cJSON *raw = cJSON_GetObjectItemCaseSensitive(root, "raw_query");
            cJSON *pxy =
                cJSON_GetObjectItemCaseSensitive(root, "atproto_proxy");
            if (!cJSON_IsString(raw) ||
                strcmp(raw->valuestring, "a=1&b=two words") != 0) {
                fprintf(stderr, "FAIL: fallback raw_query mismatch: got '%s'\n",
                        cJSON_IsString(raw) ? raw->valuestring : "(null)");
                failures++;
            }
            if (!cJSON_IsString(pxy) ||
                strcmp(pxy->valuestring, "did:web:svc.example#bsky_appview") !=
                    0) {
                fprintf(stderr, "FAIL: fallback atproto_proxy mismatch\n");
                failures++;
            }
            {
                wf_http_header fwd_hdrs[] = {
                    {"Accept-Language", "en-US"},
                    {"Accept-Encoding", "gzip"},
                    {"atproto-accept-labelers", "did:plc:lab1,did:plc:lab2"},
                    {"X-Bsky-Topics", "topic-a,topic-b"},
                    {"x-atproto-session-id", "abc123"},
                    {"x-atproto-bsky-topics", "topic-c"},
                };
                wf_status fs =
                    wf_http_get_with_headers(client, url, fwd_hdrs, 6, &res);
                cJSON *froot =
                    (fs == WF_OK && res.body)
                        ? cJSON_ParseWithLength(res.body, res.body_len)
                        : NULL;
                if (!froot) {
                    fprintf(
                        stderr,
                        "FAIL: fallback header-forward status=%d http=%ld\n",
                        (int)fs, res.status);
                    failures++;
                } else {
                    cJSON *al = cJSON_GetObjectItemCaseSensitive(
                        froot, "accept_language");
                    cJSON *ae = cJSON_GetObjectItemCaseSensitive(
                        froot, "accept_encoding");
                    cJSON *aal = cJSON_GetObjectItemCaseSensitive(
                        froot, "atproto_accept_labelers");
                    cJSON *t = cJSON_GetObjectItemCaseSensitive(
                        froot, "x_bsky_topics");
                    cJSON *ah = cJSON_GetObjectItemCaseSensitive(
                        froot, "atproto_headers");
                    if (!cJSON_IsString(al) ||
                        strcmp(al->valuestring, "en-US") != 0) {
                        fprintf(stderr,
                                "FAIL: fallback accept_language mismatch\n");
                        failures++;
                    }
                    if (!cJSON_IsString(ae) ||
                        strcmp(ae->valuestring, "gzip") != 0) {
                        fprintf(stderr,
                                "FAIL: fallback accept_encoding mismatch\n");
                        failures++;
                    }
                    if (!cJSON_IsString(aal) ||
                        strcmp(aal->valuestring, "did:plc:lab1,did:plc:lab2") !=
                            0) {
                        fprintf(stderr,
                                "FAIL: fallback atproto_accept_labelers "
                                "mismatch\n");
                        failures++;
                    }
                    if (!cJSON_IsString(t) ||
                        strcmp(t->valuestring, "topic-a,topic-b") != 0) {
                        fprintf(stderr,
                                "FAIL: fallback x_bsky_topics mismatch\n");
                        failures++;
                    }
                    if (!cJSON_IsArray(ah) || cJSON_GetArraySize(ah) != 2) {
                        fprintf(stderr,
                                "FAIL: fallback atproto_headers count=%d\n",
                                (int)cJSON_GetArraySize(ah));
                        failures++;
                    } else {
                        cJSON *first = cJSON_GetArrayItem(ah, 0);
                        cJSON *name =
                            cJSON_GetObjectItemCaseSensitive(first, "name");
                        cJSON *value =
                            cJSON_GetObjectItemCaseSensitive(first, "value");
                        if (!cJSON_IsString(name) ||
                            strcmp(name->valuestring, "x-atproto-session-id") !=
                                0 ||
                            !cJSON_IsString(value) ||
                            strcmp(value->valuestring, "abc123") != 0) {
                            fprintf(stderr, "FAIL: fallback atproto header[0] "
                                            "mismatch\n");
                            failures++;
                        }
                    }
                    cJSON_Delete(froot);
                }
                wf_response_free(&res);
            }
            cJSON_Delete(root);
        }
        wf_response_free(&res);

        /* POST to an unknown NSID reaches the fallback with its body. */
        s = wf_xrpc_procedure(client, "io.example.unknownProc", "{\"x\":1}",
                              &res);
        root = (s == WF_OK && res.body)
                   ? cJSON_ParseWithLength(res.body, res.body_len)
                   : NULL;
        if (!root || !cJSON_IsString(
                         cJSON_GetObjectItemCaseSensitive(root, "body_len"))) {
            fprintf(stderr, "FAIL: fallback POST status=%d http=%ld\n", (int)s,
                    res.status);
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
                         "grant_type=authorization_code&code=abc", NULL, 0,
                         &res);
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
        fprintf(stderr,
                "FAIL: srv rate limit req 3: expected 429, "
                "got status=%d http=%ld\n",
                (int)s, res.status);
        failures++;
    }
    wf_response_free(&res);

    /* Detach rate limiter — all requests should succeed again */
    wf_xrpc_server_set_rate_limiter(server, NULL);
    s = wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
    if (s != WF_OK || res.status != 200) {
        fprintf(stderr,
                "FAIL: srv rate limit after detach: status=%d http=%ld\n",
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

/* Any request that reaches this handler at all was not rejected by a rate
 * limiter — used to prove pong's requests get past ping's route-specific
 * bucket, independent of what test_query_handler would otherwise say about
 * an nsid it doesn't recognise. */
static wf_status test_pong_handler(void *ctx, const wf_xrpc_request *req,
                                   wf_xrpc_response *resp) {
    (void)ctx;
    (void)req;
    wf_xrpc_response_set_body(resp, "{\"ok\":true}", 11);
    return WF_OK;
}

/*
 * wf_xrpc_server_set_route_rate_limiter attaches a limiter to an exact
 * method+url, replacing the global one for that route only (the doc
 * comment's "defaults to IP-based limiter if none set"). The route's own
 * limiter must actually be enforced, and a different route with no
 * route-specific limiter must fall through to the (looser) global one
 * rather than being caught by the strict one meant for someone else's
 * endpoint.
 */
static int test_server_route_rate_limit(void) {
    wf_xrpc_server *server;
    wf_xrpc_client *client;
    wf_rate_limiter *global_rl;
    wf_rate_limiter *route_rl;
    wf_response res = {0};
    int failures = 0;

    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: route rate limit start\n");
        return 1;
    }
    if (wf_xrpc_server_register_query(server, "io.example.ping",
                                      test_query_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: route rate limit register ping\n");
        wf_xrpc_server_free(server);
        return 1;
    }
    if (wf_xrpc_server_register_query(server, "io.example.pong",
                                      test_pong_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: route rate limit register pong\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* A generous global budget, and a strict route-specific one attached to
     * ping only: 1 token, 60s window. */
    global_rl = wf_rate_limiter_new(100, 60, 0);
    wf_xrpc_server_set_rate_limiter(server, global_rl);
    route_rl = wf_rate_limiter_new(1, 60, 0);
    wf_xrpc_server_set_route_rate_limiter(server, "GET",
                                          "/xrpc/io.example.ping", route_rl);

    uint16_t port = wf_xrpc_server_port(server);
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);
    client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "FAIL: route rate limit client\n");
        wf_xrpc_server_free(server);
        wf_rate_limiter_free(global_rl);
        return 1;
    }

    wf_status s;
    wf_xrpc_param params[] = {{"msg", "ok"}};

    /* ping's 1-token bucket: first request succeeds. */
    s = wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
    if (s != WF_OK || res.status != 200) {
        fprintf(stderr,
                "FAIL: route rate limit ping req 1: status=%d http=%ld\n",
                (int)s, res.status);
        failures++;
    }
    wf_response_free(&res);

    /* ...second is rejected by the ROUTE limiter, despite the global budget
     * of 100 having plenty left — proving the route-specific limiter is the
     * one actually consulted, not silently ignored. */
    s = wf_xrpc_query_params(client, "io.example.ping", params, 1, &res);
    if (s != WF_ERR_HTTP || res.status != 429) {
        fprintf(stderr,
                "FAIL: route rate limit ping req 2: expected 429 (route "
                "limiter should have fired), got status=%d http=%ld\n",
                (int)s, res.status);
        failures++;
    }
    wf_response_free(&res);

    /* pong has no route-specific limiter, so it falls through to the global
     * 100-token budget — and must not be caught by ping's exhausted
     * 1-token route bucket, which would mean routes weren't actually being
     * distinguished. */
    for (int i = 0; i < 5; i++) {
        s = wf_xrpc_query_params(client, "io.example.pong", params, 1, &res);
        if (s != WF_OK || res.status != 200) {
            fprintf(stderr,
                    "FAIL: route rate limit pong req %d: status=%d http=%ld "
                    "(a route limiter meant for a different endpoint leaked)\n",
                    i + 1, (int)s, res.status);
            failures++;
            wf_response_free(&res);
            break;
        }
        wf_response_free(&res);
    }

    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);
    wf_rate_limiter_free(global_rl);

    if (failures == 0) {
        printf("PASS: route-specific rate limit\n");
        return 0;
    }
    return 1;
}

static const char *g_seen_client_ip;

static wf_status test_client_ip_handler(void *ctx, const wf_xrpc_request *req,
                                        wf_xrpc_response *resp) {
    (void)ctx;
    static char captured[64];
    if (req->client_ip) {
        snprintf(captured, sizeof(captured), "%s", req->client_ip);
        g_seen_client_ip = captured;
    }
    wf_xrpc_response_set_body(resp, "{\"ok\":true}", 11);
    return WF_OK;
}

/*
 * wf_xrpc_request.client_ip exists so a handler can key its own rate limits
 * (or logging) by requester address the way the reference PDS does for
 * createSession ("<identifier>-<ip>"). A loopback test client must see
 * "127.0.0.1" — not NULL, not "unknown".
 */
static int test_request_client_ip(void) {
    wf_xrpc_server *server;
    wf_xrpc_client *client;
    wf_response res = {0};
    int failures = 0;

    g_seen_client_ip = NULL;
    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: client ip start\n");
        return 1;
    }
    if (wf_xrpc_server_register_query(server, "io.example.whoami",
                                      test_client_ip_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: client ip register\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    uint16_t port = wf_xrpc_server_port(server);
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);
    client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "FAIL: client ip client\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    wf_status s = wf_xrpc_query(client, "io.example.whoami", NULL, &res);
    if (s != WF_OK || res.status != 200) {
        fprintf(stderr, "FAIL: client ip request: status=%d http=%ld\n", (int)s,
                res.status);
        failures++;
    }
    wf_response_free(&res);

    if (!g_seen_client_ip || strcmp(g_seen_client_ip, "127.0.0.1") != 0) {
        fprintf(stderr, "FAIL: expected client_ip \"127.0.0.1\", got %s\n",
                g_seen_client_ip ? g_seen_client_ip : "(null)");
        failures++;
    }

    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);

    if (failures == 0) {
        printf("PASS: request client_ip\n");
        return 0;
    }
    return 1;
}

/* Raw GET so a caller-supplied header (e.g. a spoofed/trusted client-IP
 * header) can be attached — wf_xrpc_client has no way to set arbitrary
 * request headers. */
static int raw_get_with_header(uint16_t port, const char *nsid,
                               const char *header_line) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    char req[256];
    int n = snprintf(req, sizeof(req),
                     "GET /xrpc/%s HTTP/1.1\r\n"
                     "Host: 127.0.0.1:%u\r\n"
                     "%s"
                     "Connection: close\r\n"
                     "\r\n",
                     nsid, (unsigned)port, header_line ? header_line : "");
    if (write(fd, req, (size_t)n) != n) {
        close(fd);
        return -1;
    }
    char buf[512];
    size_t used = 0;
    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (poll(&pfd, 1, 3000) <= 0) break;
        if (used + 1 >= sizeof(buf)) break;
        ssize_t r = read(fd, buf + used, sizeof(buf) - 1 - used);
        if (r <= 0) break;
        used += (size_t)r;
    }
    close(fd);
    return 0;
}

/*
 * wf_xrpc_server_set_trusted_client_ip_header: when unset (the default),
 * an attacker-supplied "CF-Connecting-IP" header must be ignored and the
 * real socket peer used. Once a deployment opts in, that header must be
 * trusted instead — this is only safe behind a proxy topology that
 * guarantees the header can't be forged end-to-end, which is a deployment
 * property this test can't verify; it only checks the mechanism.
 */
static int test_trusted_client_ip_header(void) {
    wf_xrpc_server *server;
    int failures = 0;

    g_seen_client_ip = NULL;
    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: trusted ip header start\n");
        return 1;
    }
    if (wf_xrpc_server_register_query(server, "io.example.whoami",
                                      test_client_ip_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: trusted ip header register\n");
        wf_xrpc_server_free(server);
        return 1;
    }
    uint16_t port = wf_xrpc_server_port(server);

    /* Disabled by default: a spoofed header must not override the peer. */
    raw_get_with_header(port, "io.example.whoami",
                        "CF-Connecting-IP: 6.6.6.6\r\n");
    if (!g_seen_client_ip || strcmp(g_seen_client_ip, "127.0.0.1") != 0) {
        fprintf(stderr, "FAIL: untrusted header overrode peer, got %s\n",
                g_seen_client_ip ? g_seen_client_ip : "(null)");
        failures++;
    }

    /* Enabled: the header value must now be used. */
    if (wf_xrpc_server_set_trusted_client_ip_header(
            server, "CF-Connecting-IP") != WF_OK) {
        fprintf(stderr, "FAIL: set_trusted_client_ip_header\n");
        wf_xrpc_server_free(server);
        return 1;
    }
    g_seen_client_ip = NULL;
    raw_get_with_header(port, "io.example.whoami",
                        "CF-Connecting-IP: 203.0.113.7\r\n");
    if (!g_seen_client_ip || strcmp(g_seen_client_ip, "203.0.113.7") != 0) {
        fprintf(stderr, "FAIL: expected trusted header ip, got %s\n",
                g_seen_client_ip ? g_seen_client_ip : "(null)");
        failures++;
    }

    /* Enabled but absent from this request: falls back to the peer. */
    g_seen_client_ip = NULL;
    raw_get_with_header(port, "io.example.whoami", NULL);
    if (!g_seen_client_ip || strcmp(g_seen_client_ip, "127.0.0.1") != 0) {
        fprintf(stderr, "FAIL: expected peer fallback, got %s\n",
                g_seen_client_ip ? g_seen_client_ip : "(null)");
        failures++;
    }

    /* Disabling again (NULL) restores the peer even with the header present. */
    wf_xrpc_server_set_trusted_client_ip_header(server, NULL);
    g_seen_client_ip = NULL;
    raw_get_with_header(port, "io.example.whoami",
                        "CF-Connecting-IP: 6.6.6.6\r\n");
    if (!g_seen_client_ip || strcmp(g_seen_client_ip, "127.0.0.1") != 0) {
        fprintf(stderr, "FAIL: expected peer after disable, got %s\n",
                g_seen_client_ip ? g_seen_client_ip : "(null)");
        failures++;
    }

    wf_xrpc_server_free(server);

    if (failures == 0) {
        printf("PASS: trusted client ip header\n");
        return 0;
    }
    return 1;
}

/* wf_xrpc_client's wf_response only surfaces a few named headers
 * (dpop_nonce, set_cookie, location) — none of the ones under test here —
 * so this drives a raw socket instead of the client library. */
static int raw_post_headers(uint16_t port, const char *nsid, char *out,
                            size_t out_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    char req[256];
    int n = snprintf(req, sizeof(req),
                     "POST /xrpc/%s HTTP/1.1\r\n"
                     "Host: 127.0.0.1:%u\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: 2\r\n"
                     "Connection: close\r\n"
                     "\r\n{}",
                     nsid, (unsigned)port);
    if (write(fd, req, (size_t)n) != n) {
        close(fd);
        return -1;
    }
    size_t used = 0;
    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (poll(&pfd, 1, 3000) <= 0) break;
        if (used + 1 >= out_cap) break;
        ssize_t r = read(fd, out + used, out_cap - 1 - used);
        if (r <= 0) break;
        used += (size_t)r;
    }
    out[used] = '\0';
    close(fd);
    return 0;
}

/* Returns 1 and fills `out` on a match, 0 otherwise. Not a shared static
 * buffer — a caller checking several headers from the same response needs
 * each value to survive past the next call. */
static int find_header(const char *raw, const char *name, char *out,
                       size_t out_cap) {
    char prefix[48];
    snprintf(prefix, sizeof(prefix), "\r\n%s:", name);
    const char *p = strstr(raw, prefix);
    if (!p) return 0;
    p += strlen(prefix);
    while (*p == ' ') p++;
    const char *end = strstr(p, "\r\n");
    if (!end) return 0;
    size_t len = (size_t)(end - p);
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

/*
 * A 429 response must carry RateLimit-Limit/Remaining/Reset/Policy in
 * addition to Retry-After, matching the reference PDS's
 * rate-limiter-http.ts setStatusHeaders exactly — a well-behaved client
 * checks these to back off before it ever gets rate limited, not only
 * after.
 */
static int test_rate_limit_headers(void) {
    wf_xrpc_server *server;
    wf_rate_limiter *rl;
    int failures = 0;

    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: rate limit headers start\n");
        return 1;
    }
    /* A procedure (POST), not test_query_handler's GET-only ping — this
     * test drives raw POST requests. */
    if (wf_xrpc_server_register_procedure(server, "io.example.pong",
                                          test_pong_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: rate limit headers register\n");
        wf_xrpc_server_free(server);
        return 1;
    }
    rl =
        wf_rate_limiter_new(1, 60, 0); /* 1 token: the 2nd request is limited */
    wf_xrpc_server_set_rate_limiter(server, rl);

    uint16_t port = wf_xrpc_server_port(server);
    char raw[2048];

    /* First request consumes the only token — no assertion needed, just
     * getting the bucket into the state the 2nd request needs. */
    raw_post_headers(port, "io.example.pong", raw, sizeof(raw));

    if (raw_post_headers(port, "io.example.pong", raw, sizeof(raw)) != 0) {
        fprintf(stderr, "FAIL: rate limit headers request\n");
        failures++;
    } else {
        if (!strstr(raw, "429")) {
            fprintf(stderr, "FAIL: expected 429, got: %.60s\n", raw);
            failures++;
        }
        char limit[32], remaining[32], reset[32], policy[32], retry[32];
        int has_limit =
            find_header(raw, "RateLimit-Limit", limit, sizeof(limit));
        int has_remaining = find_header(raw, "RateLimit-Remaining", remaining,
                                        sizeof(remaining));
        int has_reset =
            find_header(raw, "RateLimit-Reset", reset, sizeof(reset));
        int has_policy =
            find_header(raw, "RateLimit-Policy", policy, sizeof(policy));
        int has_retry = find_header(raw, "Retry-After", retry, sizeof(retry));

        if (!has_limit || strcmp(limit, "1") != 0) {
            fprintf(stderr, "FAIL: RateLimit-Limit = %s, want 1\n",
                    has_limit ? limit : "(missing)");
            failures++;
        }
        if (!has_remaining || strcmp(remaining, "0") != 0) {
            fprintf(stderr, "FAIL: RateLimit-Remaining = %s, want 0\n",
                    has_remaining ? remaining : "(missing)");
            failures++;
        }
        if (!has_reset) {
            fprintf(stderr, "FAIL: RateLimit-Reset missing\n");
            failures++;
        }
        if (!has_policy || strcmp(policy, "1;w=60") != 0) {
            fprintf(stderr, "FAIL: RateLimit-Policy = %s, want \"1;w=60\"\n",
                    has_policy ? policy : "(missing)");
            failures++;
        }
        if (!has_retry) {
            fprintf(stderr, "FAIL: Retry-After missing\n");
            failures++;
        }
    }

    wf_xrpc_server_free(server);
    wf_rate_limiter_free(rl);

    if (failures == 0) {
        printf("PASS: rate limit headers on 429\n");
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
    failures += test_server_route_rate_limit();
    failures += test_request_client_ip();
    failures += test_trusted_client_ip_header();
    failures += test_rate_limit_headers();

    return failures;
}
