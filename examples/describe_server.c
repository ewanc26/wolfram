/**
 * describe_server.c — describe an AT Protocol server.
 *
 * Calls wf_agent_describe_server against a given service URL and prints the
 * available user domains, whether an invite code is required, and the contact
 * email advertised by the server.
 *
 * Usage:
 *   describe_server <service-url>
 *   describe_server https://bsky.social
 */

#include <stdio.h>
#include <stdlib.h>

#include "wolfram/wolfram.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <service-url>\n", argv[0]);
        return 1;
    }

    const char *service_url = argv[1];

    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        return 1;
    }

    wf_agent_server_description desc = {0};
    wf_status status = wf_agent_describe_server(agent, &desc);

    if (status != WF_OK) {
        fprintf(stderr, "describeServer failed: status code %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    printf("Server DID: %s\n", desc.did ? desc.did : "(unknown)");

    printf("Available user domains:\n");
    if (desc.available_user_domain_count == 0) {
        printf("  (none)\n");
    } else {
        for (size_t i = 0; i < desc.available_user_domain_count; i++) {
            printf("  %s\n", desc.available_user_domains[i]);
        }
    }

    if (desc.invite_code_required < 0) {
        printf("Invite code required: unknown\n");
    } else {
        printf("Invite code required: %s\n", desc.invite_code_required ? "yes" : "no");
    }

    printf("Contact email: %s\n", desc.contact_email ? desc.contact_email : "(none)");

    wf_agent_server_description_free(&desc);
    wf_agent_free(agent);
    return 0;
}
