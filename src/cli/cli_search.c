#include "cli_search.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/feed_typed.h"
#include "wolfram/moderation.h"
#include "wolfram/moderation_report_typed.h"
#include "wolfram/label.h"
#include "wolfram/server_typed.h"
#include "wolfram/temp_typed.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <inttypes.h>

#define CLI_LABEL_MAX_EVENTS 100

static volatile sig_atomic_t g_label_stop = 0;
static int g_label_count = 0;
static time_t g_label_start = 0;
static int g_label_seconds = 30;
static wf_label_subscribe_handle **g_label_handle_ptr = NULL;

static void label_on_sigint(int sig) {
    (void)sig;
    g_label_stop = 1;
    if (g_label_handle_ptr && *g_label_handle_ptr) {
        wf_label_subscribe_stop(*g_label_handle_ptr);
    }
}

static void label_on_label(const wf_label *label, void *userdata) {
    (void)userdata;
    g_label_count++;
    printf("label: uri=%s cid=%s val=%s src=%s exp=%s neg=%d\n",
           label->uri ? label->uri : "", label->cid ? label->cid : "",
           label->val ? label->val : "", label->src ? label->src : "",
           label->exp ? label->exp : "", label->neg);
    fflush(stdout);
    if (g_label_stop ||
        (g_label_seconds > 0 &&
         (time(NULL) - g_label_start) >= g_label_seconds) ||
        (g_label_handle_ptr && *g_label_handle_ptr &&
         g_label_count >= CLI_LABEL_MAX_EVENTS)) {
        if (g_label_handle_ptr && *g_label_handle_ptr) {
            wf_label_subscribe_stop(*g_label_handle_ptr);
        }
    }
}

static void label_on_info(const wf_label_info *info, void *userdata) {
    (void)userdata;
    printf("info: name=%s message=%s\n", info->name ? info->name : "",
           info->message ? info->message : "(none)");
    fflush(stdout);
}

static void label_on_error(wf_status status, const char *msg, void *userdata) {
    (void)userdata;
    fprintf(stderr, "error: label stream status=%d %s\n", (int)status,
            msg ? msg : "");
}

int cmd_moderation(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }

    if (strcmp(argv[1], "report") == 0) {
        const char *subject = NULL, *reason = NULL, *reason_type = NULL,
                   *cid = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--subject") == 0 && i + 1 < argc)
                subject = argv[++i];
            else if (strcmp(argv[i], "--reason") == 0 && i + 1 < argc)
                reason = argv[++i];
            else if (strcmp(argv[i], "--reason-type") == 0 && i + 1 < argc)
                reason_type = argv[++i];
            else if (strcmp(argv[i], "--cid") == 0 && i + 1 < argc)
                cid = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !subject || !reason) {
            fprintf(stderr,
                    "error: usage: wolfram moderation report <service> "
                    "<handle> <password> --subject <uri> --reason <reason> "
                    "[--reason-type <type>] [--cid <cid>]\n");
            return 1;
        }

        const char *rt = reason_type ? reason_type : reason;
        const char *free_reason = reason_type ? reason : NULL;

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        const char *subject_did = NULL;
        const char *subject_uri = NULL;
        const char *subject_cid = NULL;
        if (strncmp(subject, "did:", 4) == 0) {
            subject_did = subject;
        } else {
            subject_uri = subject;
            subject_cid = cid;
        }

        wf_moderation_report_record out = {0};
        wf_status s =
            wf_agent_report_typed(agent, subject_did, subject_uri, subject_cid,
                                  rt, free_reason, NULL, NULL, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: createReport failed (status %d)\n", (int)s);
            wf_moderation_report_record_free(&out);
            wf_agent_free(agent);
            return 1;
        }
        printf("report id=%" PRId64 "\n", out.id);
        if (out.reason_type) printf("reasonType: %s\n", out.reason_type);
        if (out.reported_by) printf("reportedBy: %s\n", out.reported_by);
        wf_moderation_report_record_free(&out);
        wf_agent_free(agent);
        return 0;
    }

    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    char *did = NULL;
    wf_status rs = resolve_actor_to_did(agent, actor, &did);
    if (rs != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)rs);
        wf_agent_free(agent);
        return 1;
    }

    wf_mod_decision *decision = NULL;
    wf_status s = wf_agent_moderate_profile(agent, did, &decision);
    free(did);
    if (s != WF_OK || !decision) {
        fprintf(stderr, "error: moderation failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_mod_ui ui = {0};
    s = wf_mod_decision_ui(decision, WF_MOD_CTX_PROFILE_VIEW, &ui);
    if (s != WF_OK) {
        fprintf(stderr, "error: decision UI failed (status %d)\n", (int)s);
        wf_mod_decision_free(decision);
        wf_agent_free(agent);
        return 1;
    }

    printf("moderation for %s: alerts=%zu blurs=%zu informs=%zu\n",
           decision->did ? decision->did : actor, ui.alert_count, ui.blur_count,
           ui.inform_count);

    wf_mod_ui_free(&ui);
    wf_mod_decision_free(decision);
    wf_agent_free(agent);
    return 0;
}

int cmd_search(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *query = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 25;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s =
        wf_agent_search_posts(agent, query, limit, NULL, NULL, NULL, NULL, NULL,
                              &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_search_actors(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *query = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 25;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s =
        wf_agent_search_actors(agent, query, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_search_typeahead(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *query = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 10;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s =
        wf_agent_search_actors_typeahead(agent, query, limit, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_search_starter_packs(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *query = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 25;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s =
        wf_agent_search_starter_packs(agent, query, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_labels(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "subscribe") != 0) {
        fprintf(stderr,
                "error: unknown labels subcommand '%s' (try 'subscribe')\n",
                sub);
        return 1;
    }

    const char *service = argv[2];

    int64_t cursor = 0;
    int has_cursor = 0;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) {
            cursor = (int64_t)strtoll(argv[++i], NULL, 10);
            has_cursor = 1;
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            g_label_seconds = atoi(argv[++i]);
        } else {
            fprintf(stderr, "error: unexpected labels argument '%s'\n",
                    argv[i]);
            return 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = label_on_sigint;
    sigaction(SIGINT, &sa, NULL);

    g_label_stop = 0;
    g_label_count = 0;
    g_label_start = time(NULL);

    wf_label_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = service;
    opts.cursor = cursor;
    opts.has_cursor = has_cursor;
    opts.reconnect_delay_ms = 1000;
    opts.on_label = label_on_label;
    opts.on_neg = label_on_label;
    opts.on_info = label_on_info;
    opts.on_error = label_on_error;

    printf(
        "subscribing to label stream at %s (cursor=%s, max %d events, %ds)\n",
        service, has_cursor ? argv[2] : "none", CLI_LABEL_MAX_EVENTS,
        g_label_seconds);

    wf_label_subscribe_handle *handle = NULL;
    g_label_handle_ptr = &handle;
    wf_status s = wf_label_subscribe_start(&opts, &handle);
    g_label_handle_ptr = NULL;

    if (s != WF_OK) {
        fprintf(stderr, "error: label subscription ended (status %d)\n",
                (int)s);
        return 1;
    }
    printf("label subscription ended cleanly (%d labels)\n", g_label_count);
    return 0;
}

int cmd_revoke_account_credentials(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *account = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_status s = wf_agent_revoke_account_credentials(agent, account);
    if (s != WF_OK) {
        fprintf(stderr, "error: revokeAccountCredentials failed (status %d)\n",
                (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("revoked credentials for %s\n", account);
    wf_agent_free(agent);
    return 0;
}
