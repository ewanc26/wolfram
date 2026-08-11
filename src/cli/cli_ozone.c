#include "cli_ozone.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/ozone_typed.h"

#include <cJSON.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_subject_status(
    const wf_lex_tools_ozone_moderation_defs_subject_status_view *st,
    size_t idx) {
    printf("status %zu: id=%" PRId64 " review_state=%s subject_repo_handle=%s "
           "created_at=%s updated_at=%s\n",
           idx, st->id, st->review_state ? st->review_state : "?",
           st->subject_repo_handle ? st->subject_repo_handle : "?",
           st->created_at ? st->created_at : "?",
           st->updated_at ? st->updated_at : "?");
    if (st->has_priority_score)
        printf("  priority_score: %" PRId64 "\n", st->priority_score);
    if (st->has_comment && st->comment) printf("  comment: %s\n", st->comment);
    if (st->has_takendown)
        printf("  takendown: %s\n", st->takendown ? "true" : "false");
    if (st->has_appealed)
        printf("  appealed: %s\n", st->appealed ? "true" : "false");
    if (st->has_tags && st->tags.count > 0) {
        printf("  tags:");
        for (size_t j = 0; j < st->tags.count; ++j)
            printf(" %s", st->tags.items[j]);
        printf("\n");
    }
    if (st->has_subject_blob_cids && st->subject_blob_cids.count > 0) {
        printf("  blob_cids:");
        for (size_t j = 0; j < st->subject_blob_cids.count; ++j)
            printf(" %s", st->subject_blob_cids.items[j]);
        printf("\n");
    }
    if (st->has_last_reviewed_by && st->last_reviewed_by)
        printf("  last_reviewed_by: %s\n", st->last_reviewed_by);
    if (st->has_last_reviewed_at && st->last_reviewed_at)
        printf("  last_reviewed_at: %s\n", st->last_reviewed_at);
    if (st->has_mute_until && st->mute_until)
        printf("  mute_until: %s\n", st->mute_until);
    if (st->has_age_assurance_state && st->age_assurance_state)
        printf("  age_assurance_state: %s\n", st->age_assurance_state);
}

static void print_team_member(const wf_lex_tools_ozone_team_defs_member *m,
                              size_t idx) {
    printf("member %zu: did=%s role=%s disabled=%s created_at=%s "
           "updated_at=%s\n",
           idx, m->did ? m->did : "?", m->role ? m->role : "?",
           m->has_disabled ? (m->disabled ? "true" : "false") : "?",
           m->created_at ? m->created_at : "?",
           m->updated_at ? m->updated_at : "?");
    if (m->has_profile) {
        printf("  profile_handle: %s\n",
               m->profile->handle ? m->profile->handle : "?");
    }
    if (m->last_updated_by)
        printf("  last_updated_by: %s\n", m->last_updated_by);
}

static void
print_template(const wf_lex_tools_ozone_communication_defs_template_view *t,
               size_t idx) {
    printf("template %zu: id=%s name=%s disabled=%s lang=%s "
           "created_at=%s updated_at=%s\n",
           idx, t->id ? t->id : "?", t->name ? t->name : "?",
           t->disabled ? "true" : "false",
           t->has_lang && t->lang ? t->lang : "?",
           t->created_at ? t->created_at : "?",
           t->updated_at ? t->updated_at : "?");
    if (t->has_subject && t->subject) printf("  subject: %s\n", t->subject);
    if (t->content_markdown)
        printf("  content_markdown: %s\n", t->content_markdown);
    if (t->last_updated_by)
        printf("  last_updated_by: %s\n", t->last_updated_by);
}

static void print_option(const wf_lex_tools_ozone_setting_defs_option *o,
                         size_t idx) {
    printf("option %zu: key=%s scope=%s description=%s created_at=%s "
           "updated_at=%s\n",
           idx, o->key ? o->key : "?", o->scope ? o->scope : "?",
           o->has_description && o->description ? o->description : "?",
           o->has_created_at && o->created_at ? o->created_at : "?",
           o->has_updated_at && o->updated_at ? o->updated_at : "?");
    if (o->did) printf("  did: %s\n", o->did);
    if (o->has_manager_role && o->manager_role)
        printf("  manager_role: %s\n", o->manager_role);
    if (o->created_by) printf("  created_by: %s\n", o->created_by);
    if (o->last_updated_by)
        printf("  last_updated_by: %s\n", o->last_updated_by);
}

static void print_account(const wf_lex_com_atproto_admin_defs_account_view *a,
                          size_t idx) {
    printf("account %zu: did=%s handle=%s email=%s indexed_at=%s\n", idx,
           a->did ? a->did : "?", a->handle ? a->handle : "?",
           a->has_email && a->email ? a->email : "?",
           a->indexed_at ? a->indexed_at : "?");
    if (a->has_invites_disabled)
        printf("  invites_disabled: %s\n",
               a->invites_disabled ? "true" : "false");
    if (a->has_deactivated_at && a->deactivated_at)
        printf("  deactivated_at: %s\n", a->deactivated_at);
    if (a->has_email_confirmed_at && a->email_confirmed_at)
        printf("  email_confirmed_at: %s\n", a->email_confirmed_at);
}

static void print_report(const wf_lex_tools_ozone_report_defs_report_view *r,
                         size_t idx) {
    printf("report %zu: id=%" PRId64 " status=%s report_type=%s "
           "reported_by=%s created_at=%s\n",
           idx, r->id, r->status ? r->status : "?",
           r->report_type ? r->report_type : "?",
           r->reported_by ? r->reported_by : "?",
           r->created_at ? r->created_at : "?");
    if (r->has_comment && r->comment) printf("  comment: %s\n", r->comment);
    if (r->has_updated_at && r->updated_at)
        printf("  updated_at: %s\n", r->updated_at);
    if (r->has_related_report_count)
        printf("  related_report_count: %" PRId64 "\n",
               r->related_report_count);
    if (r->has_is_muted)
        printf("  is_muted: %s\n", r->is_muted ? "true" : "false");
    if (r->has_is_automated)
        printf("  is_automated: %s\n", r->is_automated ? "true" : "false");
}

static void print_queue(const wf_lex_tools_ozone_queue_defs_queue_view *q,
                        size_t idx) {
    printf("queue %zu: id=%" PRId64 " name=%s enabled=%s "
           "created_by=%s created_at=%s updated_at=%s\n",
           idx, q->id, q->name ? q->name : "?", q->enabled ? "true" : "false",
           q->created_by ? q->created_by : "?",
           q->created_at ? q->created_at : "?",
           q->updated_at ? q->updated_at : "?");
    if (q->has_description && q->description)
        printf("  description: %s\n", q->description);
    if (q->has_subject_types && q->subject_types.count > 0) {
        printf("  subject_types:");
        for (size_t j = 0; j < q->subject_types.count; ++j)
            printf(" %s", q->subject_types.items[j]);
        printf("\n");
    }
    if (q->has_collection && q->collection)
        printf("  collection: %s\n", q->collection);
    if (q->has_report_types && q->report_types.count > 0) {
        printf("  report_types:");
        for (size_t j = 0; j < q->report_types.count; ++j)
            printf(" %s", q->report_types.items[j]);
        printf("\n");
    }
    if (q->has_deleted_at && q->deleted_at)
        printf("  deleted_at: %s\n", q->deleted_at);
    if (q->stats) {
        printf("  stats:");
        if (q->stats->has_pending_count)
            printf(" pending=%" PRId64, q->stats->pending_count);
        if (q->stats->has_actioned_count)
            printf(" actioned=%" PRId64, q->stats->actioned_count);
        if (q->stats->has_escalated_count)
            printf(" escalated=%" PRId64, q->stats->escalated_count);
        if (q->stats->has_inbound_count)
            printf(" inbound=%" PRId64, q->stats->inbound_count);
        printf("\n");
    }
}

int cmd_ozone(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }

    const char *group = argv[1];
    const char *sub = argv[2];

    /* moderation query-statuses <service> <handle> <password> [--limit N]
     * [--cursor C] */
    if (strcmp(group, "moderation") == 0 &&
        strcmp(sub, "query-statuses") == 0) {
        const char *cursor = NULL;
        int limit = 50;
        const char *pos[3];
        int pi = 0;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3) {
            fprintf(stderr,
                    "error: usage: wolfram ozone moderation query-statuses "
                    "<service> <handle> <password> [--limit N] [--cursor C]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_lex_tools_ozone_moderation_query_statuses_main_params params = {0};
        params.has_limit = true;
        params.limit = limit;
        if (cursor) {
            params.has_cursor = true;
            params.cursor = cursor;
        }

        wf_lex_tools_ozone_moderation_query_statuses_main_output *out = NULL;
        wf_status s = wf_ozone_moderation_queryStatuses(agent, &params, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: queryStatuses failed (status %d)\n",
                    (int)s);
            wf_lex_tools_ozone_moderation_query_statuses_main_output_free(out);
            wf_agent_free(agent);
            return 1;
        }

        if (out->has_cursor && out->cursor) printf("cursor: %s\n", out->cursor);
        printf("count: %zu\n", out->subject_statuses.count);
        for (size_t i = 0; i < out->subject_statuses.count; ++i)
            print_subject_status(out->subject_statuses.items[i], i);
        if (out->subject_statuses.count == 0) printf("(no subject statuses)\n");

        wf_lex_tools_ozone_moderation_query_statuses_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    /* moderation get-suggestions <service> <handle> <password>
     * [--ignore-subjects uri]... [--limit N] */
    if (strcmp(group, "moderation") == 0 &&
        strcmp(sub, "get-suggestions") == 0) {
        const char *ignore_subjects[256];
        size_t ignore_count = 0;
        int limit = 50;
        const char *cursor = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--ignore-subjects") == 0 && i + 1 < argc)
                ignore_subjects[ignore_count++] = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3) {
            fprintf(stderr,
                    "error: usage: wolfram ozone moderation get-suggestions "
                    "<service> <handle> <password> [--ignore-subjects uri]... "
                    "[--limit N] [--cursor C]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        char *out_json = NULL;
        wf_status s = wf_ozone_moderation_get_suggestions(
            agent, ignore_subjects, ignore_count, limit, cursor, &out_json);
        if (s != WF_OK) {
            fprintf(stderr, "error: getSuggestions failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        if (out_json) printf("%s\n", out_json);
        free(out_json);
        wf_agent_free(agent);
        return 0;
    }

    /* moderation get-label-definitions <service> <handle> <password> [--uri
     * uri]... */
    if (strcmp(group, "moderation") == 0 &&
        strcmp(sub, "get-label-definitions") == 0) {
        const char *uris[256];
        size_t uri_count = 0;
        const char *pos[3];
        int pi = 0;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--uri") == 0 && i + 1 < argc)
                uris[uri_count++] = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3) {
            fprintf(
                stderr,
                "error: usage: wolfram ozone moderation get-label-definitions "
                "<service> <handle> <password> [--uri uri]...\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        char *out_json = NULL;
        wf_status s = wf_ozone_moderation_get_label_definitions(
            agent, uris, uri_count, &out_json);
        if (s != WF_OK) {
            fprintf(stderr, "error: getLabelDefinitions failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        if (out_json) printf("%s\n", out_json);
        free(out_json);
        wf_agent_free(agent);
        return 0;
    }

    /* team list-members <service> <handle> <password> [--limit N] [--cursor C]
     */
    if (strcmp(group, "team") == 0 && strcmp(sub, "list-members") == 0) {
        const char *cursor = NULL;
        int limit = 50;
        const char *pos[3];
        int pi = 0;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3) {
            fprintf(stderr,
                    "error: usage: wolfram ozone team list-members <service> "
                    "<handle> <password> [--limit N] [--cursor C]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_lex_tools_ozone_team_list_members_main_params params = {0};
        params.has_limit = true;
        params.limit = limit;
        if (cursor) {
            params.has_cursor = true;
            params.cursor = cursor;
        }

        wf_lex_tools_ozone_team_list_members_main_output *out = NULL;
        wf_status s = wf_ozone_team_listMembers(agent, &params, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: listMembers failed (status %d)\n", (int)s);
            wf_lex_tools_ozone_team_list_members_main_output_free(out);
            wf_agent_free(agent);
            return 1;
        }

        if (out->has_cursor && out->cursor) printf("cursor: %s\n", out->cursor);
        printf("count: %zu\n", out->members.count);
        for (size_t i = 0; i < out->members.count; ++i)
            print_team_member(out->members.items[i], i);
        if (out->members.count == 0) printf("(no team members)\n");

        wf_lex_tools_ozone_team_list_members_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    /* server get-config <service> <handle> <password> */
    if (strcmp(group, "server") == 0 && strcmp(sub, "get-config") == 0) {
        if (argc < 6) {
            fprintf(stderr, "error: usage: wolfram ozone server get-config "
                            "<service> <handle> <password>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[3], argv[4], argv[5]);
        if (!agent) return 1;

        wf_lex_tools_ozone_server_get_config_main_output *out = NULL;
        wf_status s = wf_ozone_server_getConfig(agent, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: getConfig failed (status %d)\n", (int)s);
            wf_lex_tools_ozone_server_get_config_main_output_free(out);
            wf_agent_free(agent);
            return 1;
        }

        if (out->has_appview && out->appview && out->appview->has_url)
            printf("appview: %s\n", out->appview->url);
        if (out->has_pds && out->pds && out->pds->has_url)
            printf("pds: %s\n", out->pds->url);
        if (out->has_blob_divert && out->blob_divert &&
            out->blob_divert->has_url)
            printf("blob_divert: %s\n", out->blob_divert->url);
        if (out->has_chat && out->chat && out->chat->has_url)
            printf("chat: %s\n", out->chat->url);
        if (out->has_viewer && out->viewer && out->viewer->has_role)
            printf("viewer_role: %s\n", out->viewer->role);
        if (out->has_verifier_did && out->verifier_did)
            printf("verifier_did: %s\n", out->verifier_did);

        wf_lex_tools_ozone_server_get_config_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    /* communication list-templates <service> <handle> <password> */
    if (strcmp(group, "communication") == 0 &&
        strcmp(sub, "list-templates") == 0) {
        if (argc < 6) {
            fprintf(stderr,
                    "error: usage: wolfram ozone communication list-templates "
                    "<service> <handle> <password>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(argv[3], argv[4], argv[5]);
        if (!agent) return 1;

        wf_lex_tools_ozone_communication_list_templates_main_output *out = NULL;
        wf_status s = wf_ozone_communication_listTemplates(agent, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: listTemplates failed (status %d)\n",
                    (int)s);
            wf_lex_tools_ozone_communication_list_templates_main_output_free(
                out);
            wf_agent_free(agent);
            return 1;
        }

        printf("count: %zu\n", out->communication_templates.count);
        for (size_t i = 0; i < out->communication_templates.count; ++i)
            print_template(out->communication_templates.items[i], i);
        if (out->communication_templates.count == 0)
            printf("(no communication templates)\n");

        wf_lex_tools_ozone_communication_list_templates_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    /* setting list-options <service> <handle> <password> [--key key] [--scope
     * scope] [--limit N] */
    if (strcmp(group, "setting") == 0 && strcmp(sub, "list-options") == 0) {
        const char *keys[256];
        size_t key_count = 0;
        const char *scope = NULL;
        int limit = 50;
        const char *cursor = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--key") == 0 && i + 1 < argc)
                keys[key_count++] = argv[++i];
            else if (strcmp(argv[i], "--scope") == 0 && i + 1 < argc)
                scope = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3) {
            fprintf(stderr,
                    "error: usage: wolfram ozone setting list-options "
                    "<service> <handle> <password> [--key key] [--scope scope] "
                    "[--limit N] [--cursor C]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_lex_tools_ozone_setting_list_options_main_params params = {0};
        params.has_limit = true;
        params.limit = limit;
        if (key_count > 0) {
            params.has_keys = true;
            params.keys.items = keys;
            params.keys.count = key_count;
        }
        if (scope) {
            params.has_scope = true;
            params.scope = scope;
        }
        if (cursor) {
            params.has_cursor = true;
            params.cursor = cursor;
        }

        wf_lex_tools_ozone_setting_list_options_main_output *out = NULL;
        wf_status s = wf_ozone_setting_listOptions(agent, &params, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: listOptions failed (status %d)\n", (int)s);
            wf_lex_tools_ozone_setting_list_options_main_output_free(out);
            wf_agent_free(agent);
            return 1;
        }

        if (out->has_cursor && out->cursor) printf("cursor: %s\n", out->cursor);
        printf("count: %zu\n", out->options.count);
        for (size_t i = 0; i < out->options.count; ++i)
            print_option(out->options.items[i], i);
        if (out->options.count == 0) printf("(no settings options)\n");

        wf_lex_tools_ozone_setting_list_options_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    /* signature search-accounts <service> <handle> <password> <did> [--limit N]
     */
    if (strcmp(group, "signature") == 0 &&
        strcmp(sub, "search-accounts") == 0) {
        if (argc < 7) {
            fprintf(stderr,
                    "error: usage: wolfram ozone signature search-accounts "
                    "<service> <handle> <password> <did> [--limit N]\n");
            return 1;
        }

        const char *service = argv[3];
        const char *handle = argv[4];
        const char *password = argv[5];
        const char *did = argv[6];
        int limit = 50;
        const char *cursor = NULL;
        for (int i = 7; i < argc; ++i) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
        }

        wf_agent *agent = agent_login_or_err(service, handle, password);
        if (!agent) return 1;

        const char *values[] = {did};
        wf_lex_tools_ozone_signature_search_accounts_main_params params = {0};
        params.values.items = values;
        params.values.count = 1;
        params.has_limit = true;
        params.limit = limit;
        if (cursor) {
            params.has_cursor = true;
            params.cursor = cursor;
        }

        wf_lex_tools_ozone_signature_search_accounts_main_output *out = NULL;
        wf_status s = wf_ozone_signature_searchAccounts(agent, &params, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: searchAccounts failed (status %d)\n",
                    (int)s);
            wf_lex_tools_ozone_signature_search_accounts_main_output_free(out);
            wf_agent_free(agent);
            return 1;
        }

        if (out->has_cursor && out->cursor) printf("cursor: %s\n", out->cursor);
        printf("count: %zu\n", out->accounts.count);
        for (size_t i = 0; i < out->accounts.count; ++i)
            print_account(out->accounts.items[i], i);
        if (out->accounts.count == 0) printf("(no accounts)\n");

        wf_lex_tools_ozone_signature_search_accounts_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    /* report query-reports <service> <handle> <password> [--limit N] [--cursor
     * C] */
    if (strcmp(group, "report") == 0 && strcmp(sub, "query-reports") == 0) {
        const char *cursor = NULL;
        int limit = 50;
        const char *pos[3];
        int pi = 0;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3) {
            fprintf(stderr,
                    "error: usage: wolfram ozone report query-reports "
                    "<service> <handle> <password> [--limit N] [--cursor C]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_lex_tools_ozone_report_query_reports_main_params params = {0};
        params.has_limit = true;
        params.limit = limit;
        if (cursor) {
            params.has_cursor = true;
            params.cursor = cursor;
        }

        wf_lex_tools_ozone_report_query_reports_main_output *out = NULL;
        wf_status s = wf_ozone_report_queryReports(agent, &params, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: queryReports failed (status %d)\n", (int)s);
            wf_lex_tools_ozone_report_query_reports_main_output_free(out);
            wf_agent_free(agent);
            return 1;
        }

        if (out->has_cursor && out->cursor) printf("cursor: %s\n", out->cursor);
        printf("count: %zu\n", out->reports.count);
        for (size_t i = 0; i < out->reports.count; ++i)
            print_report(out->reports.items[i], i);
        if (out->reports.count == 0) printf("(no reports)\n");

        wf_lex_tools_ozone_report_query_reports_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    /* queue list-queues <service> <handle> <password> [--limit N] [--cursor C]
     */
    if (strcmp(group, "queue") == 0 && strcmp(sub, "list-queues") == 0) {
        const char *cursor = NULL;
        int limit = 50;
        const char *pos[3];
        int pi = 0;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3) {
            fprintf(stderr,
                    "error: usage: wolfram ozone queue list-queues "
                    "<service> <handle> <password> [--limit N] [--cursor C]\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_lex_tools_ozone_queue_list_queues_main_params params = {0};
        params.has_limit = true;
        params.limit = limit;
        if (cursor) {
            params.has_cursor = true;
            params.cursor = cursor;
        }

        wf_lex_tools_ozone_queue_list_queues_main_output *out = NULL;
        wf_status s = wf_ozone_queue_listQueues(agent, &params, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: listQueues failed (status %d)\n", (int)s);
            wf_lex_tools_ozone_queue_list_queues_main_output_free(out);
            wf_agent_free(agent);
            return 1;
        }

        if (out->has_cursor && out->cursor) printf("cursor: %s\n", out->cursor);
        printf("count: %zu\n", out->queues.count);
        for (size_t i = 0; i < out->queues.count; ++i)
            print_queue(out->queues.items[i], i);
        if (out->queues.count == 0) printf("(no queues)\n");

        wf_lex_tools_ozone_queue_list_queues_main_output_free(out);
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr,
            "error: unknown ozone subcommand '%s %s' (try moderation, team, "
            "server, communication, setting, signature, report, queue)\n",
            group, sub);
    return 1;
}
