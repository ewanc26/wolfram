#include "cli_admin.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/admin_typed.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_account_view_list(const wf_admin_account_view_list *list) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    if (list->cursor)
        cJSON_AddStringToObject(root, "cursor", list->cursor);
    cJSON *accounts = cJSON_CreateArray();
    if (accounts) {
        for (size_t i = 0; i < list->account_count; ++i) {
            const wf_admin_account_view *v = &list->accounts[i];
            cJSON *acc = cJSON_CreateObject();
            if (!acc) continue;
            if (v->did) cJSON_AddStringToObject(acc, "did", v->did);
            if (v->handle) cJSON_AddStringToObject(acc, "handle", v->handle);
            if (v->email) cJSON_AddStringToObject(acc, "email", v->email);
            if (v->indexed_at) cJSON_AddStringToObject(acc, "indexedAt", v->indexed_at);
            if (v->has_invites_disabled)
                cJSON_AddBoolToObject(acc, "invitesDisabled", v->invites_disabled);
            if (v->deactivated_at)
                cJSON_AddStringToObject(acc, "deactivatedAt", v->deactivated_at);
            if (v->extra)
                cJSON_AddItemToObject(acc, "extra", cJSON_Duplicate(v->extra, true));
            cJSON_AddItemToArray(accounts, acc);
        }
        cJSON_AddItemToObject(root, "accounts", accounts);
    }
    char *json = cJSON_Print(root);
    if (json) {
        printf("%s\n", json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static void print_account_view(const wf_admin_account_view *view) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    if (view->did) cJSON_AddStringToObject(root, "did", view->did);
    if (view->handle) cJSON_AddStringToObject(root, "handle", view->handle);
    if (view->email) cJSON_AddStringToObject(root, "email", view->email);
    if (view->indexed_at) cJSON_AddStringToObject(root, "indexedAt", view->indexed_at);
    if (view->has_invites_disabled)
        cJSON_AddBoolToObject(root, "invitesDisabled", view->invites_disabled);
    if (view->deactivated_at)
        cJSON_AddStringToObject(root, "deactivatedAt", view->deactivated_at);
    if (view->extra)
        cJSON_AddItemToObject(root, "extra", cJSON_Duplicate(view->extra, true));
    char *json = cJSON_Print(root);
    if (json) {
        printf("%s\n", json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static void print_subject_status(const wf_admin_subject_status *status) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    if (status->did) cJSON_AddStringToObject(root, "did", status->did);
    if (status->has_takedown)
        cJSON_AddBoolToObject(root, "takedownApplied", status->takedown_applied);
    if (status->takedown_ref)
        cJSON_AddStringToObject(root, "takedownRef", status->takedown_ref);
    if (status->has_deactivated)
        cJSON_AddBoolToObject(root, "deactivatedApplied", status->deactivated_applied);
    if (status->deactivated_ref)
        cJSON_AddStringToObject(root, "deactivatedRef", status->deactivated_ref);
    if (status->subject)
        cJSON_AddItemToObject(root, "subject", cJSON_Duplicate(status->subject, true));
    char *json = cJSON_Print(root);
    if (json) {
        printf("%s\n", json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static void print_invite_code_list(const wf_admin_invite_code_list *list) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    if (list->cursor)
        cJSON_AddStringToObject(root, "cursor", list->cursor);
    cJSON *codes = cJSON_CreateArray();
    if (codes) {
        for (size_t i = 0; i < list->code_count; ++i) {
            const wf_admin_invite_code *c = &list->codes[i];
            cJSON *code = cJSON_CreateObject();
            if (!code) continue;
            if (c->code) cJSON_AddStringToObject(code, "code", c->code);
            if (c->has_available)
                cJSON_AddNumberToObject(code, "available", c->available);
            if (c->has_disabled)
                cJSON_AddBoolToObject(code, "disabled", c->disabled);
            if (c->for_account)
                cJSON_AddStringToObject(code, "forAccount", c->for_account);
            if (c->created_by)
                cJSON_AddStringToObject(code, "createdBy", c->created_by);
            if (c->created_at)
                cJSON_AddStringToObject(code, "createdAt", c->created_at);
            if (c->extra)
                cJSON_AddItemToObject(code, "extra", cJSON_Duplicate(c->extra, true));
            cJSON_AddItemToArray(codes, code);
        }
        cJSON_AddItemToObject(root, "codes", codes);
    }
    char *json = cJSON_Print(root);
    if (json) {
        printf("%s\n", json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

int cmd_admin(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "search-accounts") == 0) {
        const char *service = NULL, *handle = NULL, *password = NULL, *email = NULL;
        int limit = 50;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (!service)
                service = argv[i];
            else if (!handle)
                handle = argv[i];
            else if (!password)
                password = argv[i];
            else if (!email)
                email = argv[i];
        }
        if (!service || !handle || !password) {
            fprintf(stderr, "error: usage: wolfram admin search-accounts "
                            "<service> <handle> <password> [email] "
                            "[--limit N]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(service, handle, password);
        if (!agent) return 1;

        wf_admin_account_view_list list = {0};
        wf_status s = wf_agent_admin_search_accounts(agent, email ? email : "",
                                                      limit, NULL, &list);
        if (s != WF_OK) {
            fprintf(stderr, "error: searchAccounts failed (status %d)\n",
                    (int)s);
            wf_admin_account_view_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        print_account_view_list(&list);
        wf_admin_account_view_list_free(&list);
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "get-account-info") == 0) {
        if (argc < 6) {
            fprintf(stderr, "error: usage: wolfram admin get-account-info "
                            "<service> <handle> <password> <did>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[2], argv[3], argv[4]);
        if (!agent) return 1;

        wf_admin_account_view view = {0};
        wf_status s = wf_agent_admin_get_account_info(agent, argv[5], &view);
        if (s != WF_OK) {
            fprintf(stderr, "error: getAccountInfo failed (status %d)\n",
                    (int)s);
            wf_admin_account_view_free(&view);
            wf_agent_free(agent);
            return 1;
        }
        print_account_view(&view);
        wf_admin_account_view_free(&view);
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "get-subject-status") == 0) {
        if (argc < 6) {
            fprintf(stderr, "error: usage: wolfram admin get-subject-status "
                            "<service> <handle> <password> <did>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[2], argv[3], argv[4]);
        if (!agent) return 1;

        wf_admin_subject_status status = {0};
        wf_status s = wf_agent_admin_get_subject_status(agent, argv[5], &status);
        if (s != WF_OK) {
            fprintf(stderr, "error: getSubjectStatus failed (status %d)\n",
                    (int)s);
            wf_admin_subject_status_free(&status);
            wf_agent_free(agent);
            return 1;
        }
        print_subject_status(&status);
        wf_admin_subject_status_free(&status);
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "get-invite-codes") == 0) {
        const char *service = NULL, *handle = NULL, *password = NULL;
        const char *cursor = NULL;
        int create_count = 0;
        int use_count = 1;
        bool do_create = false;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--create") == 0 && i + 1 < argc) {
                create_count = atoi(argv[++i]);
                do_create = true;
            } else if (strcmp(argv[i], "--use-count") == 0 && i + 1 < argc) {
                use_count = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) {
                cursor = argv[++i];
            } else if (!service)
                service = argv[i];
            else if (!handle)
                handle = argv[i];
            else if (!password)
                password = argv[i];
        }
        if (!service || !handle || !password) {
            fprintf(stderr, "error: usage: wolfram admin get-invite-codes "
                            "<service> <handle> <password> [--create N] "
                            "[--use-count N] [--cursor C]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(service, handle, password);
        if (!agent) return 1;

        if (do_create) {
            for (int i = 0; i < create_count; ++i) {
                wf_response res = {0};
                wf_status s = wf_agent_create_invite_code(agent, use_count, &res);
                if (s != WF_OK) {
                    fprintf(stderr, "error: createInviteCode failed "
                                    "(status %d)\n", (int)s);
                    wf_response_free(&res);
                    wf_agent_free(agent);
                    return 1;
                }
                printf("%s\n", res.body ? res.body : "(no result)");
                wf_response_free(&res);
            }
        } else {
            wf_admin_invite_code_list list = {0};
            wf_status s = wf_agent_admin_get_invite_codes(agent, cursor, &list);
            if (s != WF_OK) {
                fprintf(stderr, "error: getInviteCodes failed (status %d)\n",
                        (int)s);
                wf_admin_invite_code_list_free(&list);
                wf_agent_free(agent);
                return 1;
            }
            print_invite_code_list(&list);
            wf_admin_invite_code_list_free(&list);
        }
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "delete-account") == 0) {
        if (argc < 6) {
            fprintf(stderr, "error: usage: wolfram admin delete-account "
                            "<service> <handle> <password> <did>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[2], argv[3], argv[4]);
        if (!agent) return 1;

        wf_status s = wf_agent_admin_delete_account(agent, argv[5]);
        if (s != WF_OK) {
            fprintf(stderr, "error: deleteAccount failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("account deleted\n");
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "disable-account-invites") == 0) {
        if (argc < 5) {
            fprintf(stderr, "error: usage: wolfram admin "
                            "disable-account-invites <service> <handle> "
                            "<password>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[2], argv[3], argv[4]);
        if (!agent) return 1;

        wf_status s = wf_agent_admin_disable_account_invites(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: disableAccountInvites failed (status "
                            "%d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("account invites disabled\n");
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "enable-account-invites") == 0) {
        if (argc < 5) {
            fprintf(stderr, "error: usage: wolfram admin "
                            "enable-account-invites <service> <handle> "
                            "<password>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[2], argv[3], argv[4]);
        if (!agent) return 1;

        wf_status s = wf_agent_admin_enable_account_invites(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: enableAccountInvites failed (status "
                            "%d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("account invites enabled\n");
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "send-email") == 0) {
        if (argc < 8) {
            fprintf(stderr, "error: usage: wolfram admin send-email "
                            "<service> <handle> <password> <recipient-did> "
                            "<subject> <body>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[2], argv[3], argv[4]);
        if (!agent) return 1;

        const char *sender_did = wf_agent_get_did(agent);
        wf_status s = wf_agent_admin_send_email(agent, argv[5], argv[7], argv[6],
                                                sender_did);
        if (s != WF_OK) {
            fprintf(stderr, "error: sendEmail failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("email sent\n");
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr, "error: unknown admin subcommand '%s'\n", sub);
    return 1;
}
