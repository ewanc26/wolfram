/*
 * agent_moderate.c — agent moderation example.
 *
 * Logs in, fetches a profile or post, runs it through the moderation decision
 * engine, and prints the resulting UI decisions (filters / blurs / alerts /
 * informs).
 *
 * Usage:
 *   agent_moderate <service> <handle> <password> <actor-or-uri>
 *
 * <actor-or-uri> is treated as a post AT-URI when it begins with "at://",
 * otherwise as a profile actor (handle or DID).
 *
 * Offline-safe: with fewer than four arguments it prints usage and exits 0.
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"
#include "wolfram/moderation.h"

static const char *cause_type_str(wf_mod_cause_type t) {
    switch (t) {
        case WF_MOD_CAUSE_BLOCKING:   return "blocking";
        case WF_MOD_CAUSE_BLOCKED_BY: return "blockedBy";
        case WF_MOD_CAUSE_BLOCK_OTHER:return "blockOther";
        case WF_MOD_CAUSE_LABEL:      return "label";
        case WF_MOD_CAUSE_MUTED:      return "muted";
        case WF_MOD_CAUSE_MUTE_WORD:  return "muteWord";
        case WF_MOD_CAUSE_HIDDEN:     return "hidden";
        default:                      return "unknown";
    }
}

static void print_ui(const char *label, const wf_mod_ui *ui) {
    printf("  %s: filters=%zu blurs=%zu alerts=%zu informs=%zu%s\n",
           label,
           ui->filter_count, ui->blur_count, ui->alert_count, ui->inform_count,
           ui->no_override ? " (no-override)" : "");

    for (size_t i = 0; i < ui->blur_count; i++) {
        const wf_mod_cause *c = &ui->blurs[i];
        if (c->type == WF_MOD_CAUSE_LABEL && c->label.val) {
            printf("    blur  [%s] %s\n", cause_type_str(c->type), c->label.val);
        } else {
            printf("    blur  [%s]\n", cause_type_str(c->type));
        }
    }
    for (size_t i = 0; i < ui->alert_count; i++) {
        const wf_mod_cause *c = &ui->alerts[i];
        if (c->type == WF_MOD_CAUSE_LABEL && c->label.val) {
            printf("    alert [%s] %s\n", cause_type_str(c->type), c->label.val);
        } else {
            printf("    alert [%s]\n", cause_type_str(c->type));
        }
    }
    for (size_t i = 0; i < ui->inform_count; i++) {
        const wf_mod_cause *c = &ui->informs[i];
        printf("    inform[%s]\n", cause_type_str(c->type));
    }
}

static void print_decision(const wf_mod_decision *d) {
    printf("Decision: did=%s%s causes=%zu\n",
           d->did ? d->did : "?",
           d->is_me ? " (is_me)" : "", d->cause_count);
    for (size_t i = 0; i < d->cause_count; i++) {
        const wf_mod_cause *c = &d->causes[i];
        printf("  cause[%zu] type=%s priority=%d downgraded=%d\n",
               i, cause_type_str(c->type), c->priority, c->downgraded);
    }

    wf_mod_ui ui;
    wf_mod_decision_ui(d, WF_MOD_CTX_PROFILE_VIEW, &ui);
    print_ui("profile-view", &ui);
    wf_mod_ui_free(&ui);

    wf_mod_decision_ui(d, WF_MOD_CTX_CONTENT_VIEW, &ui);
    print_ui("content-view", &ui);
    wf_mod_ui_free(&ui);

    wf_mod_decision_ui(d, WF_MOD_CTX_CONTENT_LIST, &ui);
    print_ui("content-list", &ui);
    wf_mod_ui_free(&ui);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <service> <handle> <password> <actor-or-uri>\n",
                argv[0]);
        fprintf(stderr,
                "  <actor-or-uri>: post AT-URI (at://...) or profile actor\n");
        return 0;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *target      = argv[4];

    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        return 1;
    }

    wf_status status = wf_agent_login(agent, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }
    printf("Logged in.\n\n");

    wf_mod_decision *dec = NULL;
    if (strncmp(target, "at://", 5) == 0) {
        printf("Moderating post %s\n\n", target);
        status = wf_agent_moderate_post(agent, target, &dec);
    } else {
        printf("Moderating profile %s\n\n", target);
        status = wf_agent_moderate_profile(agent, target, &dec);
    }

    if (status != WF_OK || !dec) {
        fprintf(stderr, "moderation failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    print_decision(dec);

    wf_mod_decision_free(dec);
    wf_agent_free(agent);
    return 0;
}
