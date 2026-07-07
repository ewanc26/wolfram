/*
 * list_notifications.c — list the authenticated account's notifications
 * (app.bsky.notification.listNotifications).
 *
 * Demonstrates:
 *   1. High-level agent login with wf_agent
 *   2. Fetching notifications via the typed wrapper
 *      wf_agent_list_notifications_typed
 *   3. Printing each notification's reason, author, and (optional) text from
 *      the owned record subtree.
 *
 * Usage:
 *   list_notifications <service-url> <handle> <password> [limit]
 *
 * [limit] is the maximum number of notifications to fetch (defaults to 50).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"
#include <cJSON.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> <password> [limit]\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    int limit = (argc > 4) ? (int)strtol(argv[4], NULL, 10) : 50;
    if (limit <= 0) {
        limit = 50;
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

    const char *handle = wf_agent_get_handle(agent);
    printf("Logged in as %s\n", handle ? handle : identifier);

    wf_agent_notification_list list = {0};
    status = wf_agent_list_notifications_typed(agent, limit, NULL, &list);
    if (status != WF_OK) {
        fprintf(stderr, "listNotifications failed: %d\n", (int)status);
        wf_agent_notification_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }

    printf("Notifications (%zu):\n", list.notification_count);
    for (size_t i = 0; i < list.notification_count; ++i) {
        const wf_agent_notification *n = &list.notifications[i];
        const char *author = n->author.handle ? n->author.handle
                            : (n->author.did ? n->author.did : "?");
        const char *text = "";
        if (n->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(n->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("  [%zu] reason=%s%s author=%s read=%d\n", i,
               n->reason ? n->reason : "?",
               n->reason_subject ? n->reason_subject : "",
               author, n->is_read);
        if (text[0]) {
            printf("        \"%.120s\"\n", text);
        }
        if (n->uri) {
            printf("        uri=%s\n", n->uri);
        }
    }

    if (list.seen_at) {
        printf("seenAt: %s\n", list.seen_at);
    }
    if (list.cursor) {
        printf("cursor: %s\n", list.cursor);
    }

    wf_agent_notification_list_free(&list);
    wf_agent_free(agent);
    return 0;
}
