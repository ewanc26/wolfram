#include "wolfram/agent.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <service_url> [identifier password]\n", argv[0]);
        return 1;
    }
    const char *service_url = argv[1];
    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "Failed to create agent\n");
        return 1;
    }
    /* Optional login if identifier and password are provided */
    if (argc >= 4) {
        const char *identifier = argv[2];
        const char *password = argv[3];
        wf_status s = wf_agent_login(agent, identifier, password);
        if (s != WF_OK) {
            fprintf(stderr, "Login failed: %d\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
    }
    wf_response res = {0};
    /* List notifications */
    wf_status s = wf_agent_list_notifications(agent, 10, NULL, &res);
    if (s == WF_OK) {
        printf("Notifications JSON:\n%s\n", res.body);
        wf_response_free(&res);
    } else {
        fprintf(stderr, "list_notifications failed: %d\n", (int)s);
    }
    /* Update seen timestamp (example) */
    s = wf_agent_update_seen_notifications(agent, "2024-01-01T00:00:00Z");
    if (s != WF_OK) {
        fprintf(stderr, "update_seen failed: %d\n", (int)s);
    }
    /* Get unread count */
    res.body = NULL; // reuse struct
    s = wf_agent_get_unread_count(agent, &res);
    if (s == WF_OK) {
        printf("Unread count JSON:\n%s\n", res.body);
        wf_response_free(&res);
    } else {
        fprintf(stderr, "get_unread_count failed: %d\n", (int)s);
    }
    wf_agent_free(agent);
    return s == WF_OK ? 0 : 1;
}
