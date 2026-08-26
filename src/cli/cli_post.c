/*
 * cli_post.c — the `post` / `reply` / `delete` / `timeline` / `get-post`
 * subcommands, split out of main.c as their own self-contained concern.
 */

#include "cli_post.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/feed_typed.h"
#include "wolfram/thread_typed.h"
#include "wolfram/repo_typed.h"
#include "wolfram/moderation.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int cmd_post(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    char *text = join_args(argc, argv, 4);
    if (!text) {
        fprintf(stderr, "error: failed to assemble post text\n");
        return 1;
    }

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        free(text);
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        free(text);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result result = {0};
    s = wf_agent_post(agent, text, &result);
    free(text);
    if (s != WF_OK) {
        cli_agent_error("post", s, agent);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", result.uri ? result.uri : "(no uri returned)");
    wf_agent_post_result_free(&result);
    wf_agent_free(agent);
    return 0;
}

int cmd_delete(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    wf_status s = wf_agent_delete_post(agent, at_uri);
    if (s != WF_OK) {
        cli_agent_error("delete", s, agent);
        wf_agent_free(agent);
        return 1;
    }
    printf("deleted %s\n", at_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_reply(int argc, char **argv) {
    if (argc < 6) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *parent_uri = argv[4];

    char *text = join_args(argc, argv, 5);
    if (!text) {
        fprintf(stderr, "error: failed to assemble reply text\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        free(text);
        return 1;
    }

    char *parent_cid = NULL, *root_uri = NULL, *root_cid = NULL;
    wf_status s = resolve_post_for_reply(agent, parent_uri, &parent_cid,
                                         &root_uri, &root_cid);
    if (s != WF_OK || !parent_cid || !root_uri || !root_cid) {
        fprintf(stderr,
                "error: could not resolve reply refs for %s (status %d)\n",
                parent_uri, (int)s);
        free(parent_cid);
        free(root_uri);
        free(root_cid);
        free(text);
        wf_agent_free(agent);
        return 1;
    }

    char created_at[32];
    now_rfc3339(created_at, sizeof(created_at));

    cJSON *rec = cJSON_CreateObject();
    cJSON_AddStringToObject(rec, "$type", "app.bsky.feed.post");
    cJSON_AddStringToObject(rec, "text", text);
    cJSON_AddStringToObject(rec, "createdAt", created_at);

    cJSON *reply = cJSON_CreateObject();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "uri", root_uri);
    cJSON_AddStringToObject(root, "cid", root_cid);
    cJSON *parent = cJSON_CreateObject();
    cJSON_AddStringToObject(parent, "uri", parent_uri);
    cJSON_AddStringToObject(parent, "cid", parent_cid);
    cJSON_AddItemToObject(reply, "root", root);
    cJSON_AddItemToObject(reply, "parent", parent);
    cJSON_AddItemToObject(rec, "reply", reply);

    char *record_json = cJSON_PrintUnformatted(rec);
    cJSON_Delete(rec);
    free(parent_cid);
    free(root_uri);
    free(root_cid);
    free(text);
    if (!record_json) {
        fprintf(stderr, "error: failed to serialize reply record\n");
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_create_record(agent, "app.bsky.feed.post", record_json, &out);
    free(record_json);
    if (s != WF_OK) {
        cli_agent_error("reply", s, agent);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

typedef struct {
    int printed;
} timeline_ctx;

static wf_status timeline_on_page(wf_agent *agent,
                                  const wf_agent_feed_list *feed,
                                  const char *cursor, void *ud) {
    (void)agent;
    (void)cursor;
    timeline_ctx *ctx = (timeline_ctx *)ud;

    for (size_t i = 0; i < feed->item_count; ++i) {
        const wf_agent_post_view *post = &feed->items[i].post;
        const char *author = post->author.handle
                                 ? post->author.handle
                                 : (post->author.did ? post->author.did : "?");
        const char *text = "";
        if (post->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(post->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("%s: %s\n", author, text);
        ctx->printed++;
    }
    return WF_OK;
}

int cmd_timeline(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int max_pages = (argc >= 5) ? atoi(argv[4]) : 0; /* 0 = until exhausted */

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    if (g_json) {
        wf_response res = {0};
        s = wf_agent_get_timeline(agent, 50, NULL, NULL, &res);
        if (s != WF_OK)
            fprintf(stderr, "error: request failed (status %d)\n", (int)s);
        else if (res.body && res.body_len > 0)
            printf("%s\n", res.body);
        else
            printf("(empty response, HTTP %ld)\n", res.status);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 0;
    }

    timeline_ctx ctx = {0};
    char *last_cursor = NULL;
    s = wf_agent_get_timeline_paged(agent, 10, max_pages, timeline_on_page,
                                    &ctx, &last_cursor);
    if (s != WF_OK) {
        fprintf(stderr, "error: timeline failed (status %d)\n", (int)s);
        free(last_cursor);
        wf_agent_free(agent);
        return 1;
    }

    if (ctx.printed == 0) {
        printf("(timeline empty)\n");
    }
    free(last_cursor);
    wf_agent_free(agent);
    return 0;
}

int cmd_get_post(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *uri = argv[2];

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed) || !parsed.authority ||
        !parsed.collection || !parsed.record_key) {
        fprintf(stderr, "error: invalid at-uri: %s\n", uri);
        wf_syntax_aturi_free(&parsed);
        return 1;
    }

    wf_xrpc_client *client = wf_xrpc_client_new(service);
    if (!client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        wf_syntax_aturi_free(&parsed);
        return 1;
    }

    wf_xrpc_param params[] = {
        {"repo", parsed.authority},
        {"collection", parsed.collection},
        {"rkey", parsed.record_key},
    };

    wf_response res = {0};
    wf_status s = wf_xrpc_query_params(client, "com.atproto.repo.getRecord",
                                       params, 3, &res);
    wf_syntax_aturi_free(&parsed);

    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getRecord failed (status %d)\n", (int)s);
        wf_response_free(&res);
        wf_xrpc_client_free(client);
        return 1;
    }

    if (res.body && res.body_len > 0) {
        printf("%s\n", res.body);
    } else {
        printf("(empty response, HTTP %ld)\n", res.status);
    }

    wf_response_free(&res);
    wf_xrpc_client_free(client);
    return 0;
}
