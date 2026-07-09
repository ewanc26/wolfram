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
        wf_xrpc_client_set_auth(client, "Bearer test-token");
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client, "io.example.auth", NULL, &res);
        wf_xrpc_client_set_auth(client, NULL);
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
                if (!auth || !cJSON_IsString(auth) || !strstr(auth->valuestring, "test-token")) {
                    fprintf(stderr, "FAIL: auth mismatch: %s\n",
                            auth && auth->valuestring ? auth->valuestring : "NULL");
                    failures++;
                }
                cJSON_Delete(root);
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

    /* Test 4: Missing handler returns 404 */
    {
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client, "io.example.missing", NULL, &res);
        if (s != WF_ERR_HTTP) {
            fprintf(stderr, "FAIL: missing expected WF_ERR_HTTP, got %d\n", (int)s);
            failures++;
        } else if (res.status != 404) {
            fprintf(stderr, "FAIL: missing expected 404, got %ld\n", res.status);
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

int main(void) {
    return run_test();
}
