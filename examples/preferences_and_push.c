/*
 * preferences_and_push.c — demonstrate updating preferences and push notification registration.
 *
 * Usage: prog <service_url> <handle> <password> <service_did> <push_token>
 *
 * Logs in, sends an empty preferences array, registers a push token, then unregisters it.
 */

#include "wolfram/agent.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <service_url> <handle> <password> <service_did> <push_token>\n", argv[0]);
        return 1;
    }
    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];
    const char *service_did = argv[4];
    const char *push_token = argv[5];

    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "Failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, identifier, password);
    if (s != WF_OK) {
        fprintf(stderr, "Login failed: %d\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    /* Example preferences: empty array (no specific preferences) */
    const char *prefs_json = "[]";
    wf_response res = {0};
    s = wf_agent_put_preferences(agent, prefs_json, &res);
    if (s == WF_OK) {
        printf("Preferences updated: %s\n", res.body);
        wf_response_free(&res);
    } else {
        fprintf(stderr, "put_preferences failed: %d\n", (int)s);
    }

    /* Register push notification */
    res.body = NULL; /* reuse struct */
    s = wf_agent_register_push(agent, service_did, push_token, &res);
    if (s == WF_OK) {
        printf("Push registered: %s\n", res.body);
        wf_response_free(&res);
    } else {
        fprintf(stderr, "register_push failed: %d\n", (int)s);
    }

    /* Unregister push notification */
    res.body = NULL;
    s = wf_agent_unregister_push(agent, service_did, push_token, &res);
    if (s == WF_OK) {
        printf("Push unregistered: %s\n", res.body);
        wf_response_free(&res);
    } else {
        fprintf(stderr, "unregister_push failed: %d\n", (int)s);
    }

    wf_agent_free(agent);
    return s == WF_OK ? 0 : 1;
}
