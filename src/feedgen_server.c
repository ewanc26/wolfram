/*
 * feedgen_server.c — high-level feed-generator skeleton server helper.
 *
 * Wraps an wf_xrpc_server and registers the two endpoints a Bluesky feed
 * generator must serve. Requires WOLFRAM_BUILD_SERVER (libmicrohttpd).
 */

#include "wolfram/feedgen_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Helper state                                                         */
/* ------------------------------------------------------------------ */
struct wf_feedgen_server {
    wf_feedgen_server_config config;  /* deep copy; freed by _free */
    wf_feedgen_server_skeleton_cb skeleton_cb;
    void                          *cb_ctx;
    wf_xrpc_server               *server;
};

/* ------------------------------------------------------------------ */
/* Config helpers                                                       */
/* ------------------------------------------------------------------ */

static void feedgen_srv_strdup_or_null(char **dst, const char *src) {
    *dst = src ? strdup(src) : NULL;
}

void wf_feedgen_server_config_free(wf_feedgen_server_config *config) {
    if (!config) {
        return;
    }
    free(config->feed_uri);
    free(config->did);
    free(config->handle);
    free(config->display_name);
    free(config->description);
    free(config->avatar);
    free(config->cid);
    memset(config, 0, sizeof(*config));
}

/* ------------------------------------------------------------------ */
/* getFeedGenerator view construction                                   */
/* ------------------------------------------------------------------ */

static void feedgen_srv_add_str_opt(cJSON *obj, const char *key,
                                     const char *val) {
    if (val && val[0] != '\0') {
        cJSON_AddStringToObject(obj, key, val);
    }
}

/* Format the current UTC time as RFC 3339 (e.g. 2026-07-09T05:29:43Z). */
static void feedgen_srv_rfc3339_now(char *buf, size_t buflen) {
    time_t now = time(NULL);
    struct tm tm_now;
#if defined(_WIN32)
    gmtime_s(&tm_now, &now);
#else
    gmtime_r(&now, &tm_now);
#endif
    strftime(buf, buflen, "%Y-%m-%dT%H:%M:%SZ", &tm_now);
}

static cJSON *feedgen_srv_build_view(const wf_feedgen_server_config *cfg) {
    cJSON *view;
    cJSON *creator;
    char indexed_at[32];

    view = cJSON_CreateObject();
    if (!view) {
        return NULL;
    }

    feedgen_srv_add_str_opt(view, "uri", cfg->feed_uri);
    feedgen_srv_add_str_opt(view, "cid", cfg->cid);
    feedgen_srv_add_str_opt(view, "did", cfg->did);
    feedgen_srv_add_str_opt(view, "displayName", cfg->display_name);
    feedgen_srv_add_str_opt(view, "description", cfg->description);
    feedgen_srv_add_str_opt(view, "avatar", cfg->avatar);

    creator = cJSON_CreateObject();
    if (!creator) {
        cJSON_Delete(view);
        return NULL;
    }
    feedgen_srv_add_str_opt(creator, "did", cfg->did);
    feedgen_srv_add_str_opt(creator, "handle", cfg->handle);
    cJSON_AddItemToObject(view, "creator", creator);

    feedgen_srv_rfc3339_now(indexed_at, sizeof(indexed_at));
    cJSON_AddStringToObject(view, "indexedAt", indexed_at);

    return view;
}

/* ------------------------------------------------------------------ */
/* Handlers                                                             */
/* ------------------------------------------------------------------ */

static wf_status feedgen_srv_skeleton_handler(void *ctx,
                                               const wf_xrpc_request *req,
                                               wf_xrpc_response *resp) {
    wf_feedgen_server *fg = (wf_feedgen_server *)ctx;
    const char *feed = NULL;
    const char *cursor = NULL;
    size_t limit = 50;
    cJSON *out_feed = NULL;
    char *out_cursor = NULL;
    wf_status rc = WF_OK;
    cJSON *root = NULL;
    char *json = NULL;

    if (req->params) {
        cJSON *f = cJSON_GetObjectItemCaseSensitive(req->params, "feed");
        if (f && cJSON_IsString(f) && f->valuestring) {
            feed = f->valuestring;
        }
        cJSON *l = cJSON_GetObjectItemCaseSensitive(req->params, "limit");
        if (l && cJSON_IsString(l) && l->valuestring) {
            limit = (size_t)strtoul(l->valuestring, NULL, 10);
        } else if (l && cJSON_IsNumber(l)) {
            limit = (size_t)(l->valuedouble < 0 ? 0 : l->valuedouble);
        }
        cJSON *c = cJSON_GetObjectItemCaseSensitive(req->params, "cursor");
        if (c && cJSON_IsString(c) && c->valuestring) {
            cursor = c->valuestring;
        }
    }

    /* Clamp to the lexicon bounds (1..100, default 50). */
    if (limit < 1) {
        limit = 1;
    } else if (limit > 100) {
        limit = 100;
    }

    if (!feed || feed[0] == '\0') {
        wf_xrpc_response_set_error(resp, 400, "UnknownFeed",
                                   "Missing required 'feed' parameter");
        return WF_OK;
    }

    rc = fg->skeleton_cb(fg->cb_ctx, feed, cursor, limit, &out_feed,
                          &out_cursor);
    if (rc != WF_OK) {
        wf_xrpc_response_set_error(resp, 500, "InternalServerError",
                                   "Skeleton callback failed");
        goto cleanup;
    }
    if (!out_feed || !cJSON_IsArray(out_feed)) {
        wf_xrpc_response_set_error(resp, 500, "InternalServerError",
                                   "Skeleton callback returned no array");
        goto cleanup;
    }

    root = cJSON_CreateObject();
    if (!root) {
        wf_xrpc_response_set_error(resp, 500, "InternalServerError",
                                   "Allocation failed");
        goto cleanup;
    }
    if (out_cursor && out_cursor[0] != '\0') {
        cJSON_AddStringToObject(root, "cursor", out_cursor);
    }
    cJSON_AddItemToObject(root, "feed", out_feed);
    out_feed = NULL; /* ownership transferred to root */

    json = cJSON_PrintUnformatted(root);
    if (!json) {
        wf_xrpc_response_set_error(resp, 500, "InternalServerError",
                                   "Serialisation failed");
        goto cleanup;
    }
    wf_xrpc_response_set_body(resp, json, strlen(json));

cleanup:
    if (root) {
        cJSON_Delete(root);
    }
    if (out_feed) {
        cJSON_Delete(out_feed);
    }
    free(out_cursor);
    free(json);
    return WF_OK;
}

static wf_status feedgen_srv_generator_handler(void *ctx,
                                               const wf_xrpc_request *req,
                                               wf_xrpc_response *resp) {
    wf_feedgen_server *fg = (wf_feedgen_server *)ctx;
    cJSON *root = NULL;
    cJSON *view = NULL;
    char *json = NULL;
    (void)req;

    view = feedgen_srv_build_view(&fg->config);
    if (!view) {
        wf_xrpc_response_set_error(resp, 500, "InternalServerError",
                                   "Allocation failed");
        return WF_OK;
    }

    root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(view);
        wf_xrpc_response_set_error(resp, 500, "InternalServerError",
                                   "Allocation failed");
        return WF_OK;
    }
    cJSON_AddItemToObject(root, "view", view);
    cJSON_AddBoolToObject(root, "isOnline", fg->config.is_online ? 1 : 0);
    cJSON_AddBoolToObject(root, "isValid", fg->config.is_valid ? 1 : 0);

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        wf_xrpc_response_set_error(resp, 500, "InternalServerError",
                                   "Serialisation failed");
        return WF_OK;
    }
    wf_xrpc_response_set_body(resp, json, strlen(json));
    free(json);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

wf_feedgen_server *wf_feedgen_server_new(
    const wf_feedgen_server_config *config,
    wf_feedgen_server_skeleton_cb skeleton_cb,
    void *ctx) {
    wf_feedgen_server *fg;

    if (!config || !skeleton_cb) {
        return NULL;
    }
    fg = (wf_feedgen_server *)calloc(1, sizeof(*fg));
    if (!fg) {
        return NULL;
    }

    feedgen_srv_strdup_or_null(&fg->config.feed_uri, config->feed_uri);
    feedgen_srv_strdup_or_null(&fg->config.did, config->did);
    feedgen_srv_strdup_or_null(&fg->config.handle, config->handle);
    feedgen_srv_strdup_or_null(&fg->config.display_name, config->display_name);
    feedgen_srv_strdup_or_null(&fg->config.description, config->description);
    feedgen_srv_strdup_or_null(&fg->config.avatar, config->avatar);
    feedgen_srv_strdup_or_null(&fg->config.cid, config->cid);
    fg->config.is_online = config->is_online ? 1 : 0;
    fg->config.is_valid = config->is_valid ? 1 : 0;

    fg->skeleton_cb = skeleton_cb;
    fg->cb_ctx = ctx;
    fg->server = NULL;
    return fg;
}

wf_status wf_feedgen_server_start(wf_feedgen_server *fg, const char *address,
                                   uint16_t port,
                                   unsigned int thread_count) {
    if (!fg || !address) {
        return WF_ERR_INVALID_ARG;
    }
    if (fg->server) {
        return WF_ERR_INVALID_ARG; /* already started */
    }
    fg->server = wf_xrpc_server_start(address, port, thread_count);
    if (!fg->server) {
        return WF_ERR_INVALID_ARG;
    }

    if (wf_xrpc_server_register_query(
            fg->server, "app.bsky.feed.getFeedSkeleton",
            feedgen_srv_skeleton_handler, fg) != WF_OK) {
        wf_xrpc_server_free(fg->server);
        fg->server = NULL;
        return WF_ERR_INVALID_ARG;
    }
    if (wf_xrpc_server_register_query(
            fg->server, "app.bsky.feed.getFeedGenerator",
            feedgen_srv_generator_handler, fg) != WF_OK) {
        wf_xrpc_server_free(fg->server);
        fg->server = NULL;
        return WF_ERR_INVALID_ARG;
    }
    return WF_OK;
}

uint16_t wf_feedgen_server_port(const wf_feedgen_server *fg) {
    return fg ? wf_xrpc_server_port(fg->server) : 0;
}

void wf_feedgen_server_stop(wf_feedgen_server *fg) {
    if (fg && fg->server) {
        wf_xrpc_server_stop(fg->server);
    }
}

void wf_feedgen_server_free(wf_feedgen_server *fg) {
    if (!fg) {
        return;
    }
    if (fg->server) {
        wf_xrpc_server_free(fg->server);
    }
    wf_feedgen_server_config_free(&fg->config);
    free(fg);
}
