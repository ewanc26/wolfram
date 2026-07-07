/*
 * agent_feed_search.c — agent API feed search example.
 *
 * Demonstrates:
 *   1. Agent login/session management with wf_agent
 *   2. Feed search with wf_agent_search_posts (query, limit, sort, author filter)
 *   3. Parsing the JSON response
 *
 * Usage:
 *   agent_feed_search <service-url> <handle-or-email> <password> <query> [limit]
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"

static void print_search_results(const wf_response *res) {
    if (!res || !res->body || res->body_len == 0) {
        printf("(empty response)\n");
        return;
    }

    cJSON *root = cJSON_ParseWithLength(res->body, res->body_len);
    if (!root) {
        printf("HTTP %ld\n%.*s\n", res->status,
               (int)(res->body_len < 1024 ? res->body_len : 1024), res->body);
        return;
    }

    cJSON *posts = cJSON_GetObjectItemCaseSensitive(root, "posts");
    if (!cJSON_IsArray(posts)) {
        char *raw = cJSON_PrintUnformatted(root);
        printf("%s\n", raw ? raw : "(parse error)");
        free(raw);
        cJSON_Delete(root);
        return;
    }

    int count = cJSON_GetArraySize(posts);
    printf("Found %d posts:\n", count);

    cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
    if (cJSON_IsString(cursor)) {
        printf("Cursor: %s\n", cursor->valuestring);
    }

    cJSON *post;
    cJSON_ArrayForEach(post, posts) {
        cJSON *author = cJSON_GetObjectItemCaseSensitive(post, "author");
        cJSON *record = cJSON_GetObjectItemCaseSensitive(post, "record");
        cJSON *like_count = cJSON_GetObjectItemCaseSensitive(post, "likeCount");
        cJSON *repost_count = cJSON_GetObjectItemCaseSensitive(post, "repostCount");
        cJSON *reply_count = cJSON_GetObjectItemCaseSensitive(post, "replyCount");

        const char *handle = "";
        const char *display = "";
        const char *text = "";
        if (cJSON_IsObject(author)) {
            cJSON *h = cJSON_GetObjectItemCaseSensitive(author, "handle");
            cJSON *dn = cJSON_GetObjectItemCaseSensitive(author, "displayName");
            if (cJSON_IsString(h)) handle = h->valuestring;
            if (cJSON_IsString(dn)) display = dn->valuestring;
        }
        if (cJSON_IsObject(record)) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(record, "text");
            if (cJSON_IsString(t)) text = t->valuestring;
        }

        printf("\n--- @%s", handle);
        if (display[0]) printf(" (%s)", display);
        printf("\n");
        printf("    %s\n", text);
        if (cJSON_IsNumber(like_count))
            printf("    likes: %d", (int)like_count->valuedouble);
        if (cJSON_IsNumber(repost_count))
            printf(" reposts: %d", (int)repost_count->valuedouble);
        if (cJSON_IsNumber(reply_count))
            printf(" replies: %d", (int)reply_count->valuedouble);
        printf("\n");
    }

    cJSON_Delete(root);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <service-url> <handle-or-email> <password> <query> [limit]\n", argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *query       = argv[4];
    int limit = 10;
    if (argc > 5) {
        long n = strtol(argv[5], NULL, 10);
        if (n > 0 && n <= 100) limit = (int)n;
    }

    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        return 1;
    }

    wf_status status = wf_agent_login(agent, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    {
        wf_session_data sd = {0};
        if (wf_agent_get_session_data(agent, &sd) == WF_OK) {
            printf("Logged in as %s (%s)\n", sd.handle, sd.did);
            wf_agent_session_data_free(&sd);
        }
    }

    wf_response res = {0};
    status = wf_agent_search_posts(agent, query, limit, NULL, NULL, NULL, NULL, NULL, &res);
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "searchPosts failed: %d\n", (int)status);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 1;
    }

    print_search_results(&res);
    wf_response_free(&res);
    wf_agent_free(agent);
    return status == WF_OK ? 0 : 1;
}
