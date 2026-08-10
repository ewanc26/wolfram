/*
 * cli_auth.c — the `login` / `whoami` / `describe-server` subcommands, split
 * out of main.c as their own self-contained concern.
 */

#include "cli_auth.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/server_typed.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int cmd_login(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    wf_agent *agent = wf_agent_new(service);
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

    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    printf("logged in as %s (%s)\n", sd.handle ? sd.handle : "?",
           sd.did ? sd.did : "?");
    wf_agent_session_data_free(&sd);
    wf_agent_free(agent);
    return 0;
}

int cmd_whoami(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    printf("did: %s\n", sd.did ? sd.did : "?");
    printf("handle: %s\n", sd.handle ? sd.handle : "?");
    wf_agent_session_data_free(&sd);
    wf_agent_free(agent);
    return 0;
}

int cmd_describe_server(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_agent_server_description desc = {0};
    wf_status s = wf_agent_describe_server(agent, &desc);
    if (s != WF_OK) {
        fprintf(stderr, "error: describeServer failed (status %d)\n", (int)s);
        wf_agent_server_description_free(&desc);
        wf_agent_free(agent);
        return 1;
    }
    printf("did: %s\n", desc.did ? desc.did : "?");
    printf("inviteCodeRequired: %d\n", desc.invite_code_required);
    printf("phoneVerificationRequired: %d\n", desc.phone_verification_required);
    printf("availableUserDomains (%zu):\n", desc.available_user_domain_count);
    for (size_t i = 0; i < desc.available_user_domain_count; ++i) {
        printf("  %s\n", desc.available_user_domains[i]
                             ? desc.available_user_domains[i]
                             : "?");
    }
    printf("privacyPolicy: %s\n",
           desc.privacy_policy ? desc.privacy_policy : "?");
    printf("termsOfService: %s\n",
           desc.terms_of_service ? desc.terms_of_service : "?");
    printf("contactEmail: %s\n",
           desc.contact_email ? desc.contact_email : "?");
    wf_agent_server_description_free(&desc);
    wf_agent_free(agent);
    return 0;
}
