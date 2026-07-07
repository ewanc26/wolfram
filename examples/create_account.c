/*
 * create_account.c — create a new AT Protocol account.
 *
 * Demonstrates wf_server_create_account against a PDS. A service URL and a
 * handle are required; email, password, and invite code are optional.
 *
 * Usage:
 *   create_account <service-url> <handle> [email] [password] [invite-code]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/wolfram.h"
#include "wolfram/server.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> [email] [password] [invite-code]\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *handle = argv[2];
    const char *email = argc > 3 ? argv[3] : NULL;
    const char *password = argc > 4 ? argv[4] : NULL;
    const char *invite_code = argc > 5 ? argv[5] : NULL;

    wf_xrpc_client *client = wf_xrpc_client_new(service_url);
    if (!client) {
        fprintf(stderr, "failed to create XRPC client\n");
        return 1;
    }

    wf_server_create_account_input input = {
        .handle = handle,
        .email = email,
        .password = password,
        .invite_code = invite_code,
    };

    wf_server_create_account_result result = {0};
    wf_status status = wf_server_create_account(client, &input, &result);
    if (status != WF_OK) {
        fprintf(stderr, "createAccount failed: status code %d\n", (int)status);
        wf_xrpc_client_free(client);
        return 1;
    }

    printf("Created account:\n");
    printf("  handle: %s\n", result.handle ? result.handle : "(null)");
    printf("  did:    %s\n", result.did ? result.did : "(null)");
    printf("  access_jwt:  %s\n", result.access_jwt ? "(present)" : "(null)");
    printf("  refresh_jwt: %s\n", result.refresh_jwt ? "(present)" : "(null)");

    wf_server_create_account_result_free(&result);
    wf_xrpc_client_free(client);
    return 0;
}
