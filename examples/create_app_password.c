/*
 * create_app_password.c — create an app password for the logged-in account.
 *
 * Demonstrates wf_session login followed by wf_server_create_app_password,
 * which issues a scoped credential that can be used in place of the account
 * password for app-specific sessions.
 *
 * Usage:
 *   create_app_password <service-url> <handle-or-email> <password> <app-password-name>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/wolfram.h"
#include "wolfram/server.h"

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr,
                "usage: %s <service-url> <handle-or-email> <password> <app-password-name>\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];
    const char *name = argv[4];

    wf_session *session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "failed to create session\n");
        return 1;
    }

    wf_status status = wf_session_login(session, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: status code %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    wf_server_create_app_password_input input = {
        .name = name,
        .privileged = 0,
    };
    wf_server_app_password pwd = {0};
    status = wf_server_create_app_password(session->client, &input, &pwd);
    if (status != WF_OK) {
        fprintf(stderr, "createAppPassword failed: status code %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    printf("Created app password '%s'\n", pwd.name ? pwd.name : name);
    if (pwd.password) {
        printf("  password: %s\n", pwd.password);
    }
    printf("  created_at: %s\n", pwd.created_at ? pwd.created_at : "(null)");
    printf("  privileged: %s\n",
           pwd.privileged < 0 ? "unknown"
                              : (pwd.privileged ? "yes" : "no"));

    wf_server_app_password_free(&pwd);
    wf_session_free(session);
    return 0;
}
