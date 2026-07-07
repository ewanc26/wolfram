/*
 * thread.c — example using wf_agent_get_post_thread to fetch a conversation thread.
 *
 * Usage:
 *   thread <service-url> <handle> <password> <post-uri>
 */

#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>

#include "wolfram/agent.h"

static void print_thread(const wf_response *res) {
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
    cJSON *thread = cJSON_GetObjectItemCaseSensitive(root, "thread");
    if (!cJSON_IsObject(thread)) {
        char *raw = cJSON_PrintUnformatted(root);
        printf("%s\n", raw ? raw : "(parse error)");
        free(raw);
        cJSON_Delete(root);
        return;
    }
    char *pretty = cJSON_Print(thread);
    printf("%s\n", pretty ? pretty : "(print error)");
    free(pretty);
    cJSON_Delete(root);
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <service-url> <handle> <password> <post-uri>\n", argv[0]);
        return 1;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *pwd = argv[3];
    const char *uri = argv[4];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, pwd);
    if (s != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_response res = {0};
    s = wf_agent_get_post_thread(agent, uri, 0, &res);
    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "getPostThread failed: %d\n", (int)s);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 1;
    }

    print_thread(&res);
    wf_response_free(&res);
    wf_agent_free(agent);
    return s == WF_OK ? 0 : 1;
}
