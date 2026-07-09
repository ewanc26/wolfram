/*
 * test_feedgen_skeleton.c — integration test for the feed generator server
 * helper.
 *
 * Starts the skeleton server, registers a callback returning a couple of
 * fixture AT-URIs, queries app.bsky.feed.getFeedSkeleton and
 * app.bsky.feed.getFeedGenerator via wf_xrpc_client, and asserts the
 * returned feed URIs and generator metadata.
 */

#include "wolfram/feedgen_server.h"
#include "wolfram/xrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FIXTURE_FEED =
    "at://did:plc:abc123/app.bsky.feed.generator/myfeed";

/* Our skeleton callback: return two fixture posts. */
static wf_status fixture_skeleton_cb(void *ctx, const char *feed,
                                      const char *cursor, size_t limit,
                                      cJSON **out_feed,
                                      char **out_cursor) {
    cJSON *arr;
    (void)ctx;
    (void)cursor;

    /* Sanity: caller passed the expected feed and a sane limit. */
    if (strcmp(feed, FIXTURE_FEED) != 0 || limit == 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    arr = cJSON_CreateArray();
    if (!arr) {
        return WF_ERR_ALLOC;
    }
    cJSON_AddItemToArray(
        arr, cJSON_CreateString(
                 "at://did:plc:abc123/app.bsky.feed.post/aaa111"));
    cJSON_AddItemToArray(
        arr, cJSON_CreateString(
                 "at://did:plc:abc123/app.bsky.feed.post/bbb222"));

    *out_feed = arr;
    *out_cursor = NULL;
    return WF_OK;
}

static const char *feed_item_at(cJSON *root, size_t idx) {
    cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feed");
    cJSON *item;
    if (!feed || !cJSON_IsArray(feed)) {
        return NULL;
    }
    item = cJSON_GetArrayItem(feed, (int)idx);
    if (!item || !cJSON_IsString(item)) {
        return NULL;
    }
    return item->valuestring;
}

static int run_test(void) {
    wf_feedgen_server_config cfg = WF_FEEDGEN_SERVER_CONFIG_INIT;
    wf_feedgen_server *fg = NULL;
    wf_xrpc_client *client = NULL;
    wf_response res = {0};
    int failures = 0;
    char base_url[64];

    cfg.feed_uri = strdup(FIXTURE_FEED);
    cfg.did = strdup("did:plc:abc123");
    cfg.handle = strdup("feed.example.com");
    cfg.display_name = strdup("My Fixture Feed");
    cfg.description = strdup("A test feed generator");
    cfg.avatar = strdup("https://example.com/avatar.png");
    cfg.is_online = 1;
    cfg.is_valid = 1;

    fg = wf_feedgen_server_new(&cfg, fixture_skeleton_cb, NULL);
    if (!fg) {
        fprintf(stderr, "FAIL: wf_feedgen_server_new returned NULL\n");
        wf_feedgen_server_config_free(&cfg);
        return 1;
    }
    wf_feedgen_server_config_free(&cfg); /* helper took a deep copy */

    if (wf_feedgen_server_start(fg, "127.0.0.1", 0, 1) != WF_OK) {
        fprintf(stderr, "FAIL: wf_feedgen_server_start\n");
        wf_feedgen_server_free(fg);
        return 1;
    }

    uint16_t port = wf_feedgen_server_port(fg);
    if (port == 0) {
        fprintf(stderr, "FAIL: feedgen port is 0\n");
        wf_feedgen_server_free(fg);
        return 1;
    }
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u",
             (unsigned)port);

    client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "FAIL: wf_xrpc_client_new\n");
        wf_feedgen_server_free(fg);
        return 1;
    }

    /* Test 1: getFeedSkeleton */
    {
        wf_xrpc_param params[] = {
            {"feed", FIXTURE_FEED},
            {"limit", "50"},
        };
        wf_response_free(&res);
        wf_status s = wf_xrpc_query_params(
            client, "app.bsky.feed.getFeedSkeleton", params, 2, &res);
        if (s != WF_OK || res.status != 200) {
            fprintf(stderr, "FAIL: getFeedSkeleton status=%d http=%ld\n",
                    (int)s, res.status);
            failures++;
        } else {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            if (!root) {
                fprintf(stderr, "FAIL: getFeedSkeleton parse\n");
                failures++;
            } else {
                const char *p0 = feed_item_at(root, 0);
                const char *p1 = feed_item_at(root, 1);
                if (!p0 || strcmp(p0,
                                  "at://did:plc:abc123/app.bsky.feed.post/"
                                  "aaa111") != 0) {
                    fprintf(stderr, "FAIL: skeleton post[0] = %s\n",
                            p0 ? p0 : "NULL");
                    failures++;
                }
                if (!p1 || strcmp(p1,
                                  "at://did:plc:abc123/app.bsky.feed.post/"
                                  "bbb222") != 0) {
                    fprintf(stderr, "FAIL: skeleton post[1] = %s\n",
                            p1 ? p1 : "NULL");
                    failures++;
                }
                cJSON_Delete(root);
            }
        }
        wf_response_free(&res);
    }

    /* Test 2: getFeedGenerator metadata */
    {
        wf_xrpc_param params[] = {{"feed", FIXTURE_FEED}};
        wf_response_free(&res);
        wf_status s = wf_xrpc_query_params(
            client, "app.bsky.feed.getFeedGenerator", params, 1, &res);
        if (s != WF_OK || res.status != 200) {
            fprintf(stderr, "FAIL: getFeedGenerator status=%d http=%ld\n",
                    (int)s, res.status);
            failures++;
        } else {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            if (!root) {
                fprintf(stderr, "FAIL: getFeedGenerator parse\n");
                failures++;
            } else {
                cJSON *view =
                    cJSON_GetObjectItemCaseSensitive(root, "view");
                cJSON *name = cJSON_GetObjectItemCaseSensitive(view,
                                                              "displayName");
                cJSON *desc = cJSON_GetObjectItemCaseSensitive(view,
                                                               "description");
                cJSON *online =
                    cJSON_GetObjectItemCaseSensitive(root, "isOnline");
                cJSON *valid =
                    cJSON_GetObjectItemCaseSensitive(root, "isValid");
                cJSON *uri =
                    cJSON_GetObjectItemCaseSensitive(view, "uri");
                if (!view || !name || !cJSON_IsString(name) ||
                    strcmp(name->valuestring, "My Fixture Feed") != 0) {
                    fprintf(stderr, "FAIL: generator displayName\n");
                    failures++;
                }
                if (!desc || !cJSON_IsString(desc) ||
                    strcmp(desc->valuestring, "A test feed generator") != 0) {
                    fprintf(stderr, "FAIL: generator description\n");
                    failures++;
                }
                if (!uri || !cJSON_IsString(uri) ||
                    strcmp(uri->valuestring, FIXTURE_FEED) != 0) {
                    fprintf(stderr, "FAIL: generator uri\n");
                    failures++;
                }
                if (!online || !cJSON_IsBool(online) ||
                    !cJSON_IsTrue(online)) {
                    fprintf(stderr, "FAIL: generator isOnline\n");
                    failures++;
                }
                if (!valid || !cJSON_IsBool(valid) ||
                    !cJSON_IsTrue(valid)) {
                    fprintf(stderr, "FAIL: generator isValid\n");
                    failures++;
                }
                cJSON_Delete(root);
            }
        }
        wf_response_free(&res);
    }

    /* Test 3: missing feed param -> 400 UnknownFeed */
    {
        wf_response_free(&res);
        wf_status s = wf_xrpc_query(client,
                                     "app.bsky.feed.getFeedSkeleton",
                                     NULL, &res);
        if (s != WF_ERR_HTTP || res.status != 400) {
            fprintf(stderr,
                    "FAIL: missing feed expected 400, got status=%d "
                    "http=%ld\n",
                    (int)s, res.status);
            failures++;
        }
        wf_response_free(&res);
    }

    wf_xrpc_client_free(client);
    wf_feedgen_server_free(fg);

    if (failures == 0) {
        printf("PASS: feedgen skeleton server round-trip\n");
        return 0;
    }
    return 1;
}

int main(void) {
    return run_test();
}
