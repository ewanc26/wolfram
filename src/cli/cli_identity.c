/*
 * cli_identity.c — the `resolve-did` / `check-handle` /
 * `get-recommended-did-credentials` / `rotate-handle` / `plc` subcommands,
 * split out of main.c as their own self-contained concern.
 */

#include "cli_identity.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/identity_typed.h"
#include "wolfram/plc.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_resolve_did(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *did = argv[2];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_resolve_did(agent, did, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_check_handle(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_identity_check_handle_input input = {.handle = handle};
    wf_identity_check_handle_result result = {0};
    wf_status s = wf_agent_check_handle(agent, &input, &result);
    if (s != WF_OK) {
        fprintf(stderr, "error: checkHandle failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("handle %s: %s\n", handle,
           result.valid == 1 ? "valid" : "not available");
    wf_agent_free(agent);
    return 0;
}

int cmd_get_recommended_did_credentials(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_recommended_did_credentials(agent, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_rotate_handle(int argc, char **argv) {
    const char *new_handle = NULL, *token = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--token") == 0 && i + 1 < argc)
            token = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
        else if (!new_handle)
            new_handle = argv[i];
    }
    if (pi < 3 || !new_handle) {
        fprintf(stderr, "error: usage: wolfram rotate-handle <service> "
                        "<handle> <password> <new-handle> [--token "
                        "<plc-token>]\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s;
    if (token) {
        s = wf_agent_request_plc_operation_signature_typed(
            agent, wf_agent_get_did(agent));
        if (s == WF_OK) {
            s = wf_agent_update_handle_typed(agent, new_handle);
        }
    } else {
        s = wf_agent_update_handle(agent, new_handle);
    }
    if (s != WF_OK) {
        fprintf(stderr, "error: rotateHandle failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("handle rotated to %s\n", new_handle);
    wf_agent_free(agent);
    return 0;
}

int cmd_plc(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "error: usage: wolfram plc <request-signature|sign|"
                        "submit> ...\n");
        return 1;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "request-signature") == 0) {
        if (argc < 4) {
            fprintf(stderr, "error: usage: wolfram plc request-signature "
                            "<service> <did>\n");
            return 1;
        }
        const char *service = argv[2];
        const char *did = argv[3];

        wf_agent *agent = wf_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }
        wf_status s =
            wf_agent_request_plc_operation_signature_typed(agent, did);
        wf_agent_free(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: requestSignature failed (status %d)\n",
                    (int)s);
            return 1;
        }
        printf("PLC signature requested for %s\n", did);
        return 0;
    }

    if (strcmp(sub, "sign") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "error: usage: wolfram plc sign <service> "
                    "<token> [--rotation-key <key>]... [--also-known-as "
                    "<handle>]...\n");
            return 1;
        }
        const char *service = argv[2];
        const char *token = argv[3];

        const char *rotation_keys[16];
        int n_rot = 0;
        const char *aka[16];
        int n_aka = 0;
        for (int i = 4; i < argc; ++i) {
            if (strcmp(argv[i], "--rotation-key") == 0 && i + 1 < argc &&
                n_rot < 16)
                rotation_keys[n_rot++] = argv[++i];
            else if (strcmp(argv[i], "--also-known-as") == 0 && i + 1 < argc &&
                     n_aka < 16)
                aka[n_aka++] = argv[++i];
        }

        wf_agent *agent = wf_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }

        wf_identity_signed_operation out = {0};
        wf_status s = wf_agent_sign_plc_operation_typed(
            agent, token, rotation_keys, n_rot, aka, n_aka, NULL, NULL, &out);
        wf_agent_free(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: signPlcOperation failed (status %d)\n",
                    (int)s);
            wf_identity_signed_operation_free(&out);
            return 1;
        }
        printf("signed operation: %s\n",
               out.operation_json ? out.operation_json : "(none)");
        wf_identity_signed_operation_free(&out);
        return 0;
    }

    if (strcmp(sub, "submit") == 0) {
        if (argc < 4) {
            fprintf(stderr, "error: usage: wolfram plc submit <service> "
                            "<operation-json>\n");
            return 1;
        }
        const char *service = argv[2];
        const char *op_json = argv[3];

        wf_agent *agent = wf_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }

        wf_status s = wf_agent_submit_plc_operation_typed(agent, op_json);
        wf_agent_free(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: submitPlcOperation failed (status %d)\n",
                    (int)s);
            return 1;
        }
        printf("PLC operation submitted\n");
        return 0;
    }

    fprintf(stderr,
            "error: unknown plc subcommand '%s' (try "
            "request-signature/sign/submit)\n",
            sub);
    return 1;
}
