#include "cli_thread.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/thread_typed.h"
#include "wolfram/notification_typed.h"
#include "wolfram/moderation.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Recursively print a thread node and its replies (indented by depth). */
static void print_thread_node(const wf_agent_thread_node *node, int depth) {
    if (!node) {
        return;
    }
    for (int i = 0; i < depth; ++i) {
        fputc(' ', stdout);
        fputc(' ', stdout);
    }

    if (node->kind == WF_AGENT_THREAD_KIND_POST) {
        const char *handle =
            node->post.author.handle
                ? node->post.author.handle
                : (node->post.author.did ? node->post.author.did : "?");
        const char *did = node->post.author.did ? node->post.author.did : "?";
        const char *text = "";
        if (node->post.record) {
            cJSON *t =
                cJSON_GetObjectItemCaseSensitive(node->post.record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("[%s] %s: %s\n", did, handle, text);
    } else {
        printf("(not found/blocked: %s)\n", node->uri ? node->uri : "?");
    }

    for (size_t i = 0; i < node->replies_count; ++i) {
        print_thread_node(&node->replies[i], depth + 1);
    }
}

int cmd_thread(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];
    int depth = (argc >= 6) ? atoi(argv[5]) : 6;

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
        s = wf_agent_get_post_thread(agent, at_uri, depth, 0, &res);
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

    wf_agent_thread thread = {0};
    s = wf_agent_get_post_thread_typed(agent, at_uri, depth, &thread);
    if (s != WF_OK) {
        fprintf(stderr, "error: getPostThread failed (status %d)\n", (int)s);
        wf_agent_thread_free(&thread);
        wf_agent_free(agent);
        return 1;
    }

    print_thread_node(&thread.root, 0);
    wf_agent_thread_free(&thread);
    wf_agent_free(agent);
    return 0;
}

int cmd_notifications(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }

    /* notifications update-seen [--seen-at <iso>] <service> <handle> <password>
     */
    if (strcmp(argv[1], "update-seen") == 0) {
        const char *seen_at = NULL;
        const char *pos[4];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--seen-at") == 0 && i + 1 < argc) {
                seen_at = argv[++i];
            } else if (pi < 4) {
                pos[pi++] = argv[i];
            }
        }
        if (pi < 3) {
            fprintf(stderr,
                    "error: usage: wolfram notifications update-seen "
                    "[--seen-at <iso>] <service> <handle> <password>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_status s = wf_agent_update_seen_typed(agent, seen_at);
        if (s != WF_OK) {
            fprintf(stderr, "error: updateSeen failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("notifications marked seen%s%s\n", seen_at ? " at " : "",
               seen_at ? seen_at : "");
        wf_agent_free(agent);
        return 0;
    }

    /* notifications <service> <handle> <password> [limit] */
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

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
        s = wf_agent_list_notifications(agent, limit, NULL, &res);
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

    wf_agent_notification_list list = {0};
    s = wf_agent_list_notifications_typed(agent, limit, NULL, &list);
    if (s != WF_OK) {
        fprintf(stderr, "error: listNotifications failed (status %d)\n",
                (int)s);
        wf_agent_notification_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }

    for (size_t i = 0; i < list.notification_count; ++i) {
        const wf_agent_notification *n = &list.notifications[i];
        const char *author = n->author.handle
                                 ? n->author.handle
                                 : (n->author.did ? n->author.did : "?");
        const char *did = n->author.did ? n->author.did : "?";
        const char *text = "";
        if (n->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(n->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("reason=%s author=%s (%s) read=%d\n  %s\n",
               n->reason ? n->reason : "?", author, did, n->is_read, text);
    }
    if (list.notification_count == 0) {
        printf("(no notifications)\n");
    }

    wf_agent_notification_list_free(&list);
    wf_agent_free(agent);
    return 0;
}
