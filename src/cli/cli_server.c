#include "cli_server.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/server.h"
#include "wolfram/server_typed.h"
#include "wolfram/identity_typed.h"
#include "wolfram/plc.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_create_account(int argc, char **argv) {
    const char *handle = NULL, *password = NULL, *email = NULL,
               *invite_code = NULL;
    const char *service = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--handle") == 0 && i + 1 < argc)
            handle = argv[++i];
        else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc)
            password = argv[++i];
        else if (strcmp(argv[i], "--email") == 0 && i + 1 < argc)
            email = argv[++i];
        else if (strcmp(argv[i], "--invite-code") == 0 && i + 1 < argc)
            invite_code = argv[++i];
        else if (!service)
            service = argv[i];
    }
    if (!service || !handle || !password || !email) {
        fprintf(stderr, "error: usage: wolfram create-account <service> "
                        "--handle <handle> --password <password> --email "
                        "<email> [--invite-code <code>]\n");
        return 1;
    }

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_server_create_account_input input = {
        .handle = handle,
        .email = email,
        .password = password,
        .invite_code = invite_code,
    };
    wf_server_create_account_result result = {0};
    wf_status s = wf_agent_create_account_typed(agent, &input, &result);
    if (s != WF_OK) {
        fprintf(stderr, "error: createAccount failed (status %d)\n", (int)s);
        wf_server_create_account_result_free(&result);
        wf_agent_free(agent);
        return 1;
    }
    printf("account created: %s (%s)\n", handle, result.did ? result.did : "?");
    wf_server_create_account_result_free(&result);
    wf_agent_free(agent);
    return 0;
}

int cmd_app_password(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *sub = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    if (strcmp(sub, "create") == 0) {
        if (argc < 6) {
            fprintf(stderr, "error: usage: wolfram app-password <service> "
                            "<handle> <password> create <name>\n");
            wf_agent_free(agent);
            return 1;
        }
        wf_server_app_password out = {0};
        wf_status s =
            wf_agent_create_app_password_typed(agent, argv[5], 0, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: createAppPassword failed (status %d)\n",
                    (int)s);
            wf_server_app_password_free(&out);
            wf_agent_free(agent);
            return 1;
        }
        printf("name=%s password=%s\n", out.name ? out.name : "?",
               out.password ? out.password : "?");
        wf_server_app_password_free(&out);
    } else if (strcmp(sub, "list") == 0) {
        wf_server_app_password_list list = {0};
        wf_status s = wf_agent_list_app_passwords_typed(agent, &list);
        if (s != WF_OK) {
            fprintf(stderr, "error: listAppPasswords failed (status %d)\n",
                    (int)s);
            wf_server_app_password_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        for (size_t i = 0; i < list.password_count; ++i) {
            printf("name=%s created=%s\n",
                   list.passwords[i].name ? list.passwords[i].name : "?",
                   list.passwords[i].created_at ? list.passwords[i].created_at
                                                : "?");
        }
        if (list.password_count == 0) printf("(no app passwords)\n");
        wf_server_app_password_list_free(&list);
    } else if (strcmp(sub, "revoke") == 0) {
        if (argc < 6) {
            fprintf(stderr, "error: usage: wolfram app-password <service> "
                            "<handle> <password> revoke <name>\n");
            wf_agent_free(agent);
            return 1;
        }
        wf_status s = wf_agent_revoke_app_password(agent, argv[5]);
        if (s != WF_OK) {
            fprintf(stderr, "error: revokeAppPassword failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("revoked app password '%s'\n", argv[5]);
    } else {
        fprintf(stderr,
                "error: unknown app-password subcommand '%s' (try "
                "create/list/revoke)\n",
                sub);
        wf_agent_free(agent);
        return 1;
    }
    wf_agent_free(agent);
    return 0;
}

int cmd_invite_codes(int argc, char **argv) {
    const char *pos[3];
    int pi = 0;
    int create_count = 0;
    int use_count = 1;
    bool do_create = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--create") == 0 && i + 1 < argc) {
            create_count = atoi(argv[++i]);
            do_create = true;
        } else if (strcmp(argv[i], "--use-count") == 0 && i + 1 < argc) {
            use_count = atoi(argv[++i]);
        } else if (pi < 3) {
            pos[pi++] = argv[i];
        }
    }
    if (pi < 3) {
        usage_stream(stderr);
        return 0;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    if (do_create) {
        for (int i = 0; i < create_count; ++i) {
            wf_response res = {0};
            wf_status s = wf_agent_create_invite_code(agent, use_count, &res);
            if (s != WF_OK) {
                fprintf(stderr, "error: createInviteCode failed (status %d)\n",
                        (int)s);
                wf_response_free(&res);
                wf_agent_free(agent);
                return 1;
            }
            printf("%s\n", res.body ? res.body : "(no result)");
            wf_response_free(&res);
        }
        printf("created %d invite codes (use-count %d)\n", create_count,
               use_count);
    } else {
        wf_response res = {0};
        wf_status s = wf_agent_get_account_invite_codes(agent, 50, 0, &res);
        return finish_agent_response(agent, s, &res);
    }
    wf_agent_free(agent);
    return 0;
}

int cmd_activate(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_activate_account(agent, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_deactivate(int argc, char **argv) {
    const char *delete_after = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--delete-after") == 0 && i + 1 < argc)
            delete_after = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3) {
        usage_stream(stderr);
        return 0;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s = wf_agent_deactivate_account_typed(agent, delete_after);
    if (s != WF_OK) {
        fprintf(stderr, "error: deactivateAccount failed (status %d)\n",
                (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("account deactivated\n");
    wf_agent_free(agent);
    return 0;
}

int cmd_check_status(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_check_account_status(agent, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_email(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[4];

    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    if (strcmp(sub, "confirm") == 0) {
        if (argc < 7) {
            fprintf(stderr, "error: usage: wolfram email <service> <handle> "
                            "<password> confirm <email> <token>\n");
            wf_agent_free(agent);
            return 1;
        }
        wf_status s = wf_agent_confirm_email_typed(agent, argv[5], argv[6]);
        if (s != WF_OK) {
            fprintf(stderr, "error: confirmEmail failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("email confirmed\n");
    } else if (strcmp(sub, "update") == 0) {
        if (argc < 6) {
            fprintf(stderr, "error: usage: wolfram email <service> <handle> "
                            "<password> update <email>\n");
            wf_agent_free(agent);
            return 1;
        }
        wf_status s = wf_agent_update_email_typed(agent, argv[5], NULL, 0);
        if (s != WF_OK) {
            fprintf(stderr, "error: updateEmail failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("email update requested\n");
    } else {
        fprintf(stderr,
                "error: unknown email subcommand '%s' (try "
                "confirm/update)\n",
                sub);
        wf_agent_free(agent);
        return 1;
    }
    wf_agent_free(agent);
    return 0;
}

int cmd_password_reset(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *email = argv[2];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_request_password_reset_typed(agent, email);
    if (s != WF_OK) {
        fprintf(stderr, "error: requestPasswordReset failed (status %d)\n",
                (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("password reset requested for %s\n", email);
    wf_agent_free(agent);
    return 0;
}

int cmd_reserve_signing_key(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    const char *did = (argc >= 5) ? argv[4] : NULL;
    wf_server_auth_token tok = {0};
    wf_status s = wf_agent_reserve_signing_key_typed(agent, did, &tok);
    if (s != WF_OK) {
        fprintf(stderr, "error: reserveSigningKey failed (status %d)\n",
                (int)s);
        wf_server_auth_token_free(&tok);
        wf_agent_free(agent);
        return 1;
    }
    printf("signing key reserved: %s\n", tok.token ? tok.token : "?");
    wf_server_auth_token_free(&tok);
    wf_agent_free(agent);
    return 0;
}

int cmd_get_service_auth(int argc, char **argv) {
    const char *aud = NULL, *lxm = NULL;
    int exp = 0;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--aud") == 0 && i + 1 < argc)
            aud = argv[++i];
        else if (strcmp(argv[i], "--exp") == 0 && i + 1 < argc)
            exp = atoi(argv[++i]);
        else if (strcmp(argv[i], "--lxm") == 0 && i + 1 < argc)
            lxm = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !aud) {
        fprintf(stderr, "error: usage: wolfram get-service-auth <service> "
                        "<handle> <password> --aud <aud> [--exp <seconds>] "
                        "[--lxm <lxm>]\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_server_auth_token tok = {0};
    wf_status s = wf_agent_get_service_auth_typed(agent, aud, exp, lxm, &tok);
    if (s != WF_OK) {
        fprintf(stderr, "error: getServiceAuth failed (status %d)\n", (int)s);
        wf_server_auth_token_free(&tok);
        wf_agent_free(agent);
        return 1;
    }
    printf("service auth token: %s\n", tok.token ? tok.token : "?");
    wf_server_auth_token_free(&tok);
    wf_agent_free(agent);
    return 0;
}

int cmd_request_account_delete(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_status s = wf_agent_request_account_delete_typed(agent);
    if (s != WF_OK) {
        fprintf(stderr, "error: requestAccountDelete failed (status %d)\n",
                (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("account deletion requested\n");
    wf_agent_free(agent);
    return 0;
}

int cmd_request_email_update(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_server_email_update_request req = {0};
    wf_status s = wf_agent_request_email_update_typed(agent, &req);
    if (s != WF_OK) {
        fprintf(stderr, "error: requestEmailUpdate failed (status %d)\n",
                (int)s);
        wf_server_email_update_request_free(&req);
        wf_agent_free(agent);
        return 1;
    }
    printf("email update request: tokenRequired=%s\n",
           req.token_required ? "true" : "false");
    wf_server_email_update_request_free(&req);
    wf_agent_free(agent);
    return 0;
}

int cmd_request_email_confirmation(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_status s = wf_agent_request_email_confirmation_typed(agent);
    if (s != WF_OK) {
        fprintf(stderr, "error: requestEmailConfirmation failed (status %d)\n",
                (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("email confirmation requested\n");
    wf_agent_free(agent);
    return 0;
}
