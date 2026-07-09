/*
 * xrpc_server_config.c — JSON configuration loader for the XRPC server.
 *
 * Parses a JSON document into an owned wf_xrpc_server_config, and starts a
 * fully configured wf_xrpc_server from it (host/port, CORS, a global
 * per-IP rate limiter, and a set of route entries registered with a built-in
 * echo handler). The JSON is parsed with cJSON; no hand-rolled parser.
 *
 * Requires libmicrohttpd (built only when WOLFRAM_BUILD_SERVER=ON).
 */

#include "wolfram/xrpc_server.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Config parsing                                                       */
/* ------------------------------------------------------------------ */

/* Read an optional boolean from a JSON object, leaving *out unchanged
 * if the key is absent or not a boolean. */
static void wf_cfg_opt_bool(const cJSON *obj, const char *key, bool *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v && cJSON_IsBool(v)) {
        *out = cJSON_IsTrue(v);
    }
}

/* Read an optional string from a JSON object into a freshly allocated copy,
 * replacing *out (which is freed first). *out is left untouched when the key
 * is absent or not a string. */
static void wf_cfg_opt_str(const cJSON *obj, const char *key, char **out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v && cJSON_IsString(v) && v->valuestring) {
        char *dup = strdup(v->valuestring);
        if (dup) {
            free(*out);
            *out = dup;
        }
    }
}

/* Read an optional unsigned integer from a JSON object. *out is untouched
 * when the key is absent or not a number. */
static void wf_cfg_opt_uint(const cJSON *obj, const char *key,
                             unsigned int *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v && cJSON_IsNumber(v)) {
        /* Clamp negative JSON numbers to 0 rather than wrapping. */
        double d = v->valuedouble;
        *out = (d < 0.0) ? 0u : (unsigned int)(d + 0.5);
    }
}

/* Map a "method" string to the config method enum. Accepts "query"/"GET"
 * and "procedure"/"POST" case-insensitively; defaults to query. */
static int wf_cfg_parse_method(const char *method) {
    if (!method) {
        return WF_XRPC_CONFIG_METHOD_QUERY;
    }
    if (strcasecmp(method, "procedure") == 0 ||
        strcasecmp(method, "post") == 0) {
        return WF_XRPC_CONFIG_METHOD_PROCEDURE;
    }
    return WF_XRPC_CONFIG_METHOD_QUERY;
}

void wf_xrpc_server_config_free(wf_xrpc_server_config *cfg) {
    if (!cfg) {
        return;
    }
    free(cfg->host);
    free(cfg->cors_origin);
    for (size_t i = 0; i < cfg->route_count; i++) {
        free(cfg->routes[i].nsid);
    }
    free(cfg->routes);
    free(cfg);
}

wf_status wf_xrpc_server_config_parse(const char *json, size_t len,
                                       wf_xrpc_server_config **out) {
    wf_xrpc_server_config *cfg = NULL;
    cJSON *root = NULL;
    cJSON *routes = NULL;
    wf_status rc = WF_OK;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!json) {
        return WF_ERR_INVALID_ARG;
    }

    cfg = (wf_xrpc_server_config *)calloc(1, sizeof(*cfg));
    if (!cfg) {
        return WF_ERR_ALLOC;
    }

    /* Documented defaults. */
    cfg->host = strdup("0.0.0.0");
    cfg->port = 8080;
    cfg->cors_enabled = true;
    cfg->cors_origin = strdup("*");
    cfg->rate_limit.max_tokens = 0;
    cfg->rate_limit.refill_per_second = 0;
    cfg->routes = NULL;
    cfg->route_count = 0;
    if (!cfg->host || !cfg->cors_origin) {
        rc = WF_ERR_ALLOC;
        goto out;
    }

    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) {
        /* Malformed JSON (or not an object) — report clearly, no leak. */
        rc = WF_ERR_CONFIG;
        goto out;
    }

    /* Top-level scalars. */
    wf_cfg_opt_str(root, "host", &cfg->host);
    {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "port");
        if (v && cJSON_IsNumber(v)) {
            double d = v->valuedouble;
            cfg->port = (d < 0.0 || d > 65535.0) ? 0u : (uint16_t)(d + 0.5);
        }
    }

    /* CORS sub-object. */
    {
        cJSON *cors = cJSON_GetObjectItemCaseSensitive(root, "cors");
        if (cors && cJSON_IsObject(cors)) {
            wf_cfg_opt_bool(cors, "enabled", &cfg->cors_enabled);
            wf_cfg_opt_str(cors, "allowed_origin", &cfg->cors_origin);
        }
    }

    /* Rate limit sub-object. */
    {
        cJSON *rl = cJSON_GetObjectItemCaseSensitive(root, "rate_limit");
        if (rl && cJSON_IsObject(rl)) {
            wf_cfg_opt_uint(rl, "max_tokens", &cfg->rate_limit.max_tokens);
            wf_cfg_opt_uint(rl, "refill_per_second",
                             &cfg->rate_limit.refill_per_second);
        }
    }

    /* Routes array. */
    routes = cJSON_GetObjectItemCaseSensitive(root, "routes");
    if (routes && cJSON_IsArray(routes)) {
        size_t n = (size_t)cJSON_GetArraySize(routes);
        if (n > 0) {
            cfg->routes = (wf_xrpc_server_config_route *)calloc(
                n, sizeof(*cfg->routes));
            if (!cfg->routes) {
                rc = WF_ERR_ALLOC;
                goto out;
            }
            size_t j = 0;
            cJSON *entry;
            cJSON_ArrayForEach(entry, routes) {
                if (!cJSON_IsObject(entry)) {
                    continue;
                }
                cJSON *nsid =
                    cJSON_GetObjectItemCaseSensitive(entry, "nsid");
                if (!(nsid && cJSON_IsString(nsid) && nsid->valuestring &&
                      nsid->valuestring[0] != '\0')) {
                    /* Skip route entries without a usable NSID; unknown
                     * fields are ignored but a route must name an NSID. */
                    continue;
                }
                cJSON *method =
                    cJSON_GetObjectItemCaseSensitive(entry, "method");
                cfg->routes[j].nsid = strdup(nsid->valuestring);
                if (!cfg->routes[j].nsid) {
                    rc = WF_ERR_ALLOC;
                    cfg->route_count = j;
                    goto out;
                }
                cfg->routes[j].method =
                    wf_cfg_parse_method(method && cJSON_IsString(method)
                                            ? method->valuestring
                                            : NULL);
                j++;
            }
            cfg->route_count = j;
        }
    }

    *out = cfg;
    cfg = NULL;

out:
    if (root) {
        cJSON_Delete(root);
    }
    if (cfg) {
        wf_xrpc_server_config_free(cfg);
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Built-in echo handler for config-registered routes                   */
/* ------------------------------------------------------------------ */

/* Used when a route is declared in config but no application handler was
 * supplied. Replies 200 with the NSID/method and a duplicate of the request
 * params/body (the original params stay owned by the server). */
static wf_status wf_cfg_default_handler(void *ctx, const wf_xrpc_request *req,
                                         wf_xrpc_response *resp) {
    (void)ctx;
    char *json = NULL;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return WF_ERR_ALLOC;
    }
    cJSON_AddStringToObject(obj, "status", "ok");
    if (req->nsid) {
        cJSON_AddStringToObject(obj, "nsid", req->nsid);
    }
    if (req->method) {
        cJSON_AddStringToObject(obj, "method", req->method);
    }
    if (req->params) {
        cJSON *dup = cJSON_Duplicate(req->params, 1);
        if (dup) {
            cJSON_AddItemToObject(obj, "params", dup);
        }
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

/* ------------------------------------------------------------------ */
/* Server creation from config                                          */
/* ------------------------------------------------------------------ */

wf_status wf_xrpc_server_new_with_config(const wf_xrpc_server_config *cfg,
                                          wf_xrpc_server **out) {
    wf_xrpc_server *server;
    const char *host;
    wf_status rc;

    if (!cfg || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;

    host = (cfg->host && cfg->host[0]) ? cfg->host : "0.0.0.0";

    server = wf_xrpc_server_start(host, cfg->port, 0);
    if (!server) {
        return WF_ERR_INTERNAL;
    }

    /* CORS policy. */
    wf_xrpc_server_set_cors(server, cfg->cors_enabled, cfg->cors_origin);

    /* Global per-IP rate limiter derived from the configured burst/refill.
     * wf_rate_limiter_new(points, duration, 0) refills at
     * points/duration tokens per second, so choose duration so that the
     * achieved rate matches refill_per_second. */
    if (cfg->rate_limit.max_tokens > 0 &&
        cfg->rate_limit.refill_per_second > 0) {
        unsigned int points = cfg->rate_limit.max_tokens;
        unsigned int rps = cfg->rate_limit.refill_per_second;
        unsigned int duration = (points + rps - 1) / rps; /* ceil */
        if (duration == 0) {
            duration = 1;
        }
        wf_rate_limiter *rl = wf_rate_limiter_new(points, duration, 0);
        if (rl) {
            /* The server takes ownership of the limiter and frees it in
             * wf_xrpc_server_free, so no reference is left dangling if the
             * config is freed first. */
            wf_xrpc_server_set_rate_limiter_owned(server, rl);
        }
    }

    /* Register configured routes with the built-in echo handler. */
    for (size_t i = 0; i < cfg->route_count; i++) {
        const wf_xrpc_server_config_route *r = &cfg->routes[i];
        if (!r->nsid) {
            continue;
        }
        if (r->method == WF_XRPC_CONFIG_METHOD_PROCEDURE) {
            rc = wf_xrpc_server_register_procedure(server, r->nsid,
                                                   wf_cfg_default_handler, NULL);
        } else {
            rc = wf_xrpc_server_register_query(server, r->nsid,
                                               wf_cfg_default_handler, NULL);
        }
        if (rc != WF_OK) {
            wf_xrpc_server_free(server);
            return rc;
        }
    }

    *out = server;
    return WF_OK;
}
