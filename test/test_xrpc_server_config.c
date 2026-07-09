/*
 * test_xrpc_server_config.c — offline tests for the XRPC server JSON
 * configuration loader.
 *
 * Covers: parsing valid config (host/port/CORS/rate-limit/route count),
 * malformed JSON (clear error, no crash/leak), and an optional round-trip
 * against an XRPC client start from config.
 */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Parse a valid config                                                */
/* ------------------------------------------------------------------ */
static int test_parse_valid(void) {
    static const char *json =
        "{"
        "  \"host\": \"127.0.0.1\","
        "  \"port\": 9876,"
        "  \"cors\": { \"enabled\": true, \"allowed_origin\": \"https://app.example\" },"
        "  \"rate_limit\": { \"max_tokens\": 100, \"refill_per_second\": 10 },"
        "  \"routes\": ["
        "    { \"nsid\": \"io.example.ping\", \"method\": \"query\" },"
        "    { \"nsid\": \"io.example.echo\", \"method\": \"procedure\" },"
        "    { \"nsid\": \"io.example.noop\" }"
        "  ]"
        "}";

    wf_xrpc_server_config *cfg = NULL;
    wf_status s = wf_xrpc_server_config_parse(json, strlen(json), &cfg);

    if (s != WF_OK) {
        fprintf(stderr, "FAIL: parse valid returned %d\n", (int)s);
        return 1;
    }
    if (!cfg) {
        fprintf(stderr, "FAIL: parse valid returned NULL cfg\n");
        return 1;
    }

    int failures = 0;
    if (strcmp(cfg->host, "127.0.0.1") != 0) {
        fprintf(stderr, "FAIL: host = %s\n",
                cfg->host ? cfg->host : "NULL");
        failures++;
    }
    if (cfg->port != 9876) {
        fprintf(stderr, "FAIL: port = %u\n", (unsigned)cfg->port);
        failures++;
    }
    if (!cfg->cors_enabled) {
        fprintf(stderr, "FAIL: cors_enabled should be true\n");
        failures++;
    }
    if (!cfg->cors_origin || strcmp(cfg->cors_origin, "https://app.example") != 0) {
        fprintf(stderr, "FAIL: cors_origin = %s\n",
                cfg->cors_origin ? cfg->cors_origin : "NULL");
        failures++;
    }
    if (cfg->rate_limit.max_tokens != 100) {
        fprintf(stderr, "FAIL: rate_limit.max_tokens = %u\n",
                cfg->rate_limit.max_tokens);
        failures++;
    }
    if (cfg->rate_limit.refill_per_second != 10) {
        fprintf(stderr, "FAIL: rate_limit.refill_per_second = %u\n",
                cfg->rate_limit.refill_per_second);
        failures++;
    }
    /* 3 route entries; the third defaults to query. */
    if (cfg->route_count != 3) {
        fprintf(stderr, "FAIL: route_count = %zu\n", cfg->route_count);
        failures++;
    }
    if (cfg->route_count >= 3) {
        if (cfg->routes[0].method != WF_XRPC_CONFIG_METHOD_QUERY) {
            fprintf(stderr, "FAIL: route0 method (%d)\n", cfg->routes[0].method);
            failures++;
        }
        if (cfg->routes[1].method != WF_XRPC_CONFIG_METHOD_PROCEDURE) {
            fprintf(stderr, "FAIL: route1 method (%d)\n", cfg->routes[1].method);
            failures++;
        }
        if (cfg->routes[2].method != WF_XRPC_CONFIG_METHOD_QUERY) {
            fprintf(stderr, "FAIL: route2 default method (%d)\n",
                    cfg->routes[2].method);
            failures++;
        }
    }

    wf_xrpc_server_config_free(cfg);

    if (failures == 0) {
        printf("PASS: config parse valid\n");
    }
    return failures;
}

/* ------------------------------------------------------------------ */
/* Defaults when fields are missing                                     */
/* ------------------------------------------------------------------ */
static int test_parse_defaults(void) {
    static const char *json = "{}";
    wf_xrpc_server_config *cfg = NULL;
    wf_status s = wf_xrpc_server_config_parse(json, strlen(json), &cfg);
    if (s != WF_OK || !cfg) {
        fprintf(stderr, "FAIL: parse defaults returned %d / %p\n",
                (int)s, (void *)cfg);
        return 1;
    }
    int failures = 0;
    if (strcmp(cfg->host, "0.0.0.0") != 0) {
        fprintf(stderr, "FAIL: default host = %s\n",
                cfg->host ? cfg->host : "NULL");
        failures++;
    }
    if (cfg->port != 8080) {
        fprintf(stderr, "FAIL: default port = %u\n", (unsigned)cfg->port);
        failures++;
    }
    if (!cfg->cors_enabled) {
        fprintf(stderr, "FAIL: default cors_enabled should be true\n");
        failures++;
    }
    if (!cfg->cors_origin || strcmp(cfg->cors_origin, "*") != 0) {
        fprintf(stderr, "FAIL: default cors_origin = %s\n",
                cfg->cors_origin ? cfg->cors_origin : "NULL");
        failures++;
    }
    if (cfg->rate_limit.max_tokens != 0 || cfg->rate_limit.refill_per_second != 0) {
        fprintf(stderr, "FAIL: default rate_limit should be 0/0\n");
        failures++;
    }
    if (cfg->route_count != 0 || cfg->routes != NULL) {
        fprintf(stderr, "FAIL: default routes should be empty\n");
        failures++;
    }
    wf_xrpc_server_config_free(cfg);
    if (failures == 0) {
        printf("PASS: config parse defaults\n");
    }
    return failures;
}

/* ------------------------------------------------------------------ */
/* Malformed JSON returns an error, no crash                            */
/* ------------------------------------------------------------------ */
static int test_parse_malformed(void) {
    static const char *bad[] = {
        "{ this is not json",
        "{\"port\": }",
        "[1, 2, 3]",                 /* valid JSON but not an object */
        NULL
    };
    /* Valid JSON with wrong-typed sub-fields: ignored, falls back to defaults
     * (no error, no crash, no leak). */
    static const char *ignored[] = {
        "{\"routes\": \"not-an-array\"}",
        "{\"cors\": 42}",
        "{\"rate_limit\": \"x\"}",
        "{\"extra_unknown_field\": 123}",
        NULL
    };
    int failures = 0;

    for (int i = 0; bad[i]; i++) {
        wf_xrpc_server_config *cfg = (wf_xrpc_server_config *)0x1; /* poison */
        wf_status s = wf_xrpc_server_config_parse(bad[i], strlen(bad[i]), &cfg);
        if (s == WF_OK) {
            fprintf(stderr, "FAIL: malformed[%d] should error, got WF_OK\n", i);
            failures++;
        }
        if (cfg != NULL) {
            /* On error the out pointer must be reset (and never leaked). */
            fprintf(stderr, "FAIL: malformed[%d] left non-NULL cfg\n", i);
            wf_xrpc_server_config_free(cfg);
            failures++;
        }
    }

    /* Wrong-typed sub-fields are ignored and fall back to defaults. */
    for (int i = 0; ignored[i]; i++) {
        wf_xrpc_server_config *cfg = NULL;
        wf_status s = wf_xrpc_server_config_parse(ignored[i],
                                                   strlen(ignored[i]), &cfg);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: ignored[%d] should parse, got %d\n", i,
                    (int)s);
            failures++;
        } else if (!cfg) {
            fprintf(stderr, "FAIL: ignored[%d] returned NULL cfg\n", i);
            failures++;
        } else {
            wf_xrpc_server_config_free(cfg); /* must not leak */
        }
    }

    /* NULL inputs are invalid args, not crashes. */
    wf_xrpc_server_config *cfg = NULL;
    if (wf_xrpc_server_config_parse(NULL, 0, &cfg) != WF_ERR_INVALID_ARG) {
        fprintf(stderr, "FAIL: NULL json should be WF_ERR_INVALID_ARG\n");
        failures++;
    }
    if (wf_xrpc_server_config_parse("{}", 2, NULL) != WF_ERR_INVALID_ARG) {
        fprintf(stderr, "FAIL: NULL out should be WF_ERR_INVALID_ARG\n");
        failures++;
    }

    if (failures == 0) {
        printf("PASS: config parse malformed\n");
    }
    return failures;
}

/* ------------------------------------------------------------------ */
/* Live round-trip: start a server from config, query it              */
/* ------------------------------------------------------------------ */
static int test_round_trip(void) {
    /* Use port 0 (ephemeral) and an explicit CORS origin. */
    static const char *json =
        "{"
        "  \"host\": \"127.0.0.1\","
        "  \"port\": 0,"
        "  \"cors\": { \"enabled\": true, \"allowed_origin\": \"https://x.example\" },"
        "  \"rate_limit\": { \"max_tokens\": 50, \"refill_per_second\": 25 },"
        "  \"routes\": ["
        "    { \"nsid\": \"io.example.config.ping\", \"method\": \"query\" },"
        "    { \"nsid\": \"io.example.config.echo\", \"method\": \"procedure\" }"
        "  ]"
        "}";

    wf_xrpc_server_config *cfg = NULL;
    wf_xrpc_server *server = NULL;
    wf_xrpc_client *client = NULL;
    wf_response res = {0};
    int failures = 0;

    if (wf_xrpc_server_config_parse(json, strlen(json), &cfg) != WF_OK ||
        !cfg) {
        fprintf(stderr, "FAIL: round-trip config parse\n");
        return 1;
    }

    wf_status s = wf_xrpc_server_new_with_config(cfg, &server);
    if (s != WF_OK || !server) {
        fprintf(stderr, "FAIL: new_with_config returned %d / %p\n",
                (int)s, (void *)server);
        wf_xrpc_server_config_free(cfg);
        return 1;
    }

    uint16_t port = wf_xrpc_server_port(server);
    if (port == 0) {
        fprintf(stderr, "FAIL: server port is 0\n");
        failures++;
        goto cleanup;
    }

    {
        char base_url[64];
        snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u",
                 (unsigned)port);
        client = wf_xrpc_client_new(base_url);
        if (!client) {
            fprintf(stderr, "FAIL: client new\n");
            failures++;
            goto cleanup;
        }
    }

    /* Query the configured route — built-in echo handler replies 200. */
    {
        wf_xrpc_param params[] = {{"msg", "hello"}};
        wf_response_free(&res);
        s = wf_xrpc_query_params(client, "io.example.config.ping",
                                  params, 1, &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: config ping status=%d\n", (int)s);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: config ping http=%ld\n", res.status);
            failures++;
        } else {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            if (!root) {
                fprintf(stderr, "FAIL: config ping parse\n");
                failures++;
            } else {
                cJSON *nsid = cJSON_GetObjectItemCaseSensitive(root, "nsid");
                cJSON *params_obj =
                    cJSON_GetObjectItemCaseSensitive(root, "params");
                cJSON *msg = params_obj
                                 ? cJSON_GetObjectItemCaseSensitive(params_obj, "msg")
                                 : NULL;
                if (!nsid || !cJSON_IsString(nsid) ||
                    strcmp(nsid->valuestring, "io.example.config.ping") != 0) {
                    fprintf(stderr, "FAIL: config ping nsid mismatch\n");
                    failures++;
                }
                if (!msg || !cJSON_IsString(msg) ||
                    strcmp(msg->valuestring, "hello") != 0) {
                    fprintf(stderr, "FAIL: config ping param echo mismatch\n");
                    failures++;
                }
                cJSON_Delete(root);
            }
        }
        wf_response_free(&res);
    }

    /* Procedure route. */
    {
        wf_response_free(&res);
        s = wf_xrpc_procedure(client, "io.example.config.echo",
                                "{\"custom\":\"data\"}", &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: config echo status=%d\n", (int)s);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: config echo http=%ld\n", res.status);
            failures++;
        }
        wf_response_free(&res);
    }

    /* Unknown NSID still 404s (rate limiter does not block this request). */
    {
        wf_response_free(&res);
        s = wf_xrpc_query(client, "io.example.config.missing", NULL, &res);
        if (s != WF_ERR_HTTP || res.status != 404) {
            fprintf(stderr, "FAIL: config missing expected 404, got %d/%ld\n",
                    (int)s, res.status);
            failures++;
        }
        wf_response_free(&res);
    }

cleanup:
    if (client) wf_xrpc_client_free(client);
    if (server) wf_xrpc_server_free(server);
    if (cfg) wf_xrpc_server_config_free(cfg);

    if (failures == 0) {
        printf("PASS: config round-trip\n");
    }
    return failures;
}

int main(void) {
    int failures = 0;
    failures += test_parse_valid();
    failures += test_parse_defaults();
    failures += test_parse_malformed();
    failures += test_round_trip();
    return failures;
}
