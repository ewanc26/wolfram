/*
 * ozone.c — moderation-service / labeler helper.
 *
 * See wolfram/ozone.h. All network access is delegated to the low-level
 * XRPC client; this module only builds/decodes JSON and issues calls.
 */

#include "wolfram/ozone.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* One label value definition for policies.labelValueDefinitions. */
static void add_label_value_def(cJSON *defs, const char *identifier,
                                const char *severity, const char *blurs,
                                int adult_only, const char *default_setting) {
    if (!defs || !identifier) {
        return;
    }

    cJSON *def = cJSON_CreateObject();
    if (!def) {
        return;
    }

    cJSON_AddStringToObject(def, "identifier", identifier);
    cJSON_AddStringToObject(def, "severity", severity);
    cJSON_AddStringToObject(def, "blurs", blurs);
    cJSON_AddBoolToObject(def, "adultOnly", adult_only);
    cJSON_AddStringToObject(def, "defaultSetting", default_setting);

    cJSON *locales = cJSON_CreateArray();
    cJSON *locale = cJSON_CreateObject();
    cJSON_AddStringToObject(locale, "langs", "en");
    cJSON_AddStringToObject(locale, "name", identifier);
    cJSON_AddStringToObject(locale, "description",
                            "Label definition managed by this labeler.");
    cJSON_AddItemToArray(locales, locale);
    cJSON_AddItemToObject(def, "locales", locales);

    cJSON_AddItemToArray(defs, def);
}

static int make_rfc3339_timestamp(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm *tmp = gmtime(&now);
    if (!tmp) {
        return 0;
    }

    tm_utc = *tmp;
    return strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 0;
}

char *wf_labeler_build_service_record(const char *labeler_did,
                                      const char *creator,
                                      const char *created_at) {
    if (!labeler_did) {
        return NULL;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return NULL;
    }

    cJSON_AddStringToObject(record, "$type", "app.bsky.labeler.service");
    cJSON_AddStringToObject(record, "labelerDid", labeler_did);
    if (creator) {
        cJSON_AddStringToObject(record, "creator", creator);
    }

    if (created_at && created_at[0]) {
        cJSON_AddStringToObject(record, "createdAt", created_at);
    } else {
        char ts[32];
        if (make_rfc3339_timestamp(ts, sizeof(ts))) {
            cJSON_AddStringToObject(record, "createdAt", ts);
        }
    }

    /* policies.labelValueDefinitions — advertise which labels this labeler
     * can issue, referencing well-known atproto label values. */
    cJSON *policies = cJSON_CreateObject();
    cJSON *defs = cJSON_CreateArray();

    add_label_value_def(defs, "!no-unauthenticated", "inform", "none", 0,
                        "warn");
    add_label_value_def(defs, "!hide", "inform", "none", 0, "warn");
    add_label_value_def(defs, "spam", "alert", "content", 0, "warn");
    add_label_value_def(defs, "porn", "alert", "content", 1, "hide");
    add_label_value_def(defs, "sexual", "inform", "media", 0, "warn");
    add_label_value_def(defs, "nudity", "inform", "media", 1, "warn");

    cJSON_AddItemToObject(policies, "labelValueDefinitions", defs);
    cJSON_AddItemToObject(record, "policies", policies);

    char *json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    return json;
}

wf_status wf_ozone_report_subject(wf_xrpc_client *client, const char *repo_did,
                                  const char *collection, const char *rkey,
                                  const char *reason_type, const char *reason,
                                  wf_response *out) {
    if (!client || !repo_did || !reason_type || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    cJSON_AddStringToObject(root, "reasonType", reason_type);
    if (reason && reason[0]) {
        cJSON_AddStringToObject(root, "reason", reason);
    }

    cJSON *subject = cJSON_CreateObject();
    if (!subject) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (collection && rkey) {
        char uri[1024];
        int n = snprintf(uri, sizeof(uri), "at://%s/%s/%s", repo_did,
                         collection, rkey);
        if (n < 0 || (size_t)n >= sizeof(uri)) {
            cJSON_Delete(subject);
            cJSON_Delete(root);
            return WF_ERR_INVALID_ARG;
        }
        cJSON_AddStringToObject(subject, "$type", "com.atproto.repo.strongRef");
        cJSON_AddStringToObject(subject, "uri", uri);
        /* No cid is available to the caller; a verified record reference
         * should extend this object with the record CID before sending. */
    } else {
        cJSON_AddStringToObject(subject, "$type",
                                "com.atproto.admin.defs#repoRef");
        cJSON_AddStringToObject(subject, "did", repo_did);
    }
    cJSON_AddItemToObject(root, "subject", subject);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return WF_ERR_ALLOC;
    }

    wf_status status =
        wf_xrpc_procedure(client, WF_ATPROTO_CREATE_REPORT_NSID, body, out);
    free(body);
    return status;
}

wf_status wf_ozone_query_statuses(wf_xrpc_client *client, const char **subjects,
                                  size_t n, wf_response *out) {
    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param *params = NULL;
    if (subjects && n > 0) {
        params = (wf_xrpc_param *)calloc(n, sizeof(wf_xrpc_param));
        if (!params) {
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < n; ++i) {
            params[i].name = "subject";
            params[i].value = subjects[i];
        }
    }

    wf_status status = wf_xrpc_query_params(
        client, WF_OZONE_QUERY_STATUSES_NSID, params, n, out);
    free(params);
    return status;
}

/* --- Emit / query moderation events ------------------------------- */

char *wf_ozone_emit_event_input_serialize(
    const wf_lex_tools_ozone_moderation_emit_event_main_input *input) {
    if (!input) {
        return NULL;
    }
    char *json = NULL;
    if (wf_lex_tools_ozone_moderation_emit_event_main_input_encode_json(
            input, &json) != WF_OK) {
        return NULL;
    }
    return json;
}

void wf_ozone_emit_event_input_free(char *json) {
    wf_lex_tools_ozone_moderation_emit_event_main_json_free(json);
}

wf_status wf_ozone_emit_event(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_emit_event_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_emit_event_main_call(client, input,
                                                              out);
}

wf_status wf_ozone_query_events(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_query_events_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_query_events_main_call(client, params,
                                                                out);
}

wf_status wf_ozone_query_events_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_query_events_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_query_events_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_list_events(wf_xrpc_client *client, int64_t limit,
                               const char *cursor, wf_response *out) {
    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_tools_ozone_moderation_query_events_main_params params;
    memset(&params, 0, sizeof(params));
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    return wf_lex_tools_ozone_moderation_query_events_main_call(client, &params,
                                                                out);
}

wf_status wf_ozone_get_event(wf_xrpc_client *client, int64_t id,
                             wf_response *out) {
    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_tools_ozone_moderation_get_event_main_params params;
    memset(&params, 0, sizeof(params));
    params.id = id;
    return wf_lex_tools_ozone_moderation_get_event_main_call(client, &params,
                                                             out);
}

wf_status wf_ozone_get_reporter_stats(wf_xrpc_client *client, const char **dids,
                                      size_t n, wf_response *out) {
    if (!client || !dids || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_tools_ozone_moderation_get_reporter_stats_main_params params;
    memset(&params, 0, sizeof(params));
    params.dids.items = dids;
    params.dids.count = n;
    return wf_lex_tools_ozone_moderation_get_reporter_stats_main_call(
        client, &params, out);
}

wf_status wf_ozone_get_reporter_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_reporter_stats_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_reporter_stats_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Subjects --------------------------------------------------- */

wf_status wf_ozone_get_subjects(wf_xrpc_client *client, const char **subjects,
                                size_t n, wf_response *out) {
    if (!client || !subjects || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_tools_ozone_moderation_get_subjects_main_params params;
    memset(&params, 0, sizeof(params));
    params.subjects.items = subjects;
    params.subjects.count = n;
    return wf_lex_tools_ozone_moderation_get_subjects_main_call(client, &params,
                                                                out);
}

wf_status wf_ozone_get_subjects_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_subjects_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_subjects_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Communication templates -------------------------------------- */

wf_status wf_ozone_list_communication_templates(wf_xrpc_client *client,
                                                wf_response *out) {
    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_communication_list_templates_main_call(client,
                                                                     out);
}

wf_status wf_ozone_list_communication_templates_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_communication_list_templates_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_communication_list_templates_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_create_communication_template(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_communication_create_template_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_communication_create_template_main_call(
        client, input, out);
}

wf_status wf_ozone_update_communication_template(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_communication_update_template_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_communication_update_template_main_call(
        client, input, out);
}

wf_status wf_ozone_delete_communication_template(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_communication_delete_template_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_communication_delete_template_main_call(
        client, input, out);
}

/* --- Set values --------------------------------------------------- */

wf_status wf_ozone_set_add_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_add_values_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_set_add_values_main_call(client, input, out);
}

wf_status wf_ozone_set_delete_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_delete_values_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_set_delete_values_main_call(client, input, out);
}

wf_status wf_ozone_set_query_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_get_values_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_set_get_values_main_call(client, params, out);
}

wf_status wf_ozone_set_query_values_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_set_get_values_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_set_get_values_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_set_upsert_values(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_set_upsert_set_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_set_upsert_set_main_call(client, input, out);
}

/* --- Moderation — additional typed wrappers ------------------------ */

wf_status wf_ozone_get_account_timeline(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_account_timeline_main_params
        *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_account_timeline_main_call(
        client, params, out);
}

wf_status wf_ozone_get_account_timeline_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_account_timeline_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_account_timeline_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_get_records(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_records_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_records_main_call(client, params,
                                                               out);
}

wf_status wf_ozone_get_records_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_records_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_records_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_get_repo(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_repo_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_repo_main_call(client, params,
                                                            out);
}

wf_status wf_ozone_get_repos(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_get_repos_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_repos_main_call(client, params,
                                                             out);
}

wf_status wf_ozone_get_repos_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_get_repos_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_get_repos_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_search_repos(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_search_repos_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_search_repos_main_call(client, params,
                                                                out);
}

wf_status wf_ozone_search_repos_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_search_repos_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_search_repos_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_cancel_scheduled_actions(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_cancel_scheduled_actions_main_input
        *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_cancel_scheduled_actions_main_call(
        client, input, out);
}

wf_status wf_ozone_schedule_action(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_schedule_action_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_schedule_action_main_call(client,
                                                                   input, out);
}

wf_status wf_ozone_list_scheduled_actions(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_moderation_list_scheduled_actions_main_input
        *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_list_scheduled_actions_main_call(
        client, input, out);
}

wf_status wf_ozone_list_scheduled_actions_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_moderation_list_scheduled_actions_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_moderation_list_scheduled_actions_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Queue --------------------------------------------------------- */

wf_status wf_ozone_queue_assign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_assign_moderator_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_assign_moderator_main_call(client, input,
                                                               out);
}

wf_status wf_ozone_queue_create_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_create_queue_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_create_queue_main_call(client, input, out);
}

wf_status wf_ozone_queue_create_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_create_queue_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_create_queue_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_queue_delete_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_delete_queue_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_delete_queue_main_call(client, input, out);
}

wf_status wf_ozone_queue_delete_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_delete_queue_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_delete_queue_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_queue_get_assignments(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_get_assignments_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_get_assignments_main_call(client, params,
                                                              out);
}

wf_status wf_ozone_queue_get_assignments_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_get_assignments_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_get_assignments_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_queue_list_queues(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_list_queues_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_list_queues_main_call(client, params, out);
}

wf_status wf_ozone_queue_list_queues_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_list_queues_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_list_queues_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_queue_route_reports(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_route_reports_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_route_reports_main_call(client, input, out);
}

wf_status wf_ozone_queue_route_reports_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_route_reports_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_route_reports_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_queue_unassign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_unassign_moderator_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_unassign_moderator_main_call(client, input,
                                                                 out);
}

wf_status wf_ozone_queue_update_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_queue_update_queue_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_update_queue_main_call(client, input, out);
}

wf_status wf_ozone_queue_update_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_queue_update_queue_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_queue_update_queue_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Report -------------------------------------------------------- */

wf_status wf_ozone_report_assign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_assign_moderator_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_assign_moderator_main_call(client, input,
                                                                out);
}

wf_status wf_ozone_report_create_activity(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_create_activity_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_create_activity_main_call(client, input,
                                                               out);
}

wf_status wf_ozone_report_create_activity_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_create_activity_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_create_activity_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_get_assignments(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_assignments_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_assignments_main_call(client, params,
                                                               out);
}

wf_status wf_ozone_report_get_assignments_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_assignments_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_assignments_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_get_historical_stats(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_historical_stats_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_historical_stats_main_call(
        client, params, out);
}

wf_status wf_ozone_report_get_historical_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_historical_stats_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_historical_stats_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_get_latest_report(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_latest_report_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_latest_report_main_call(client, params,
                                                                 out);
}

wf_status wf_ozone_report_get_latest_report_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_latest_report_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_latest_report_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_get_live_stats(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_live_stats_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_live_stats_main_call(client, params,
                                                              out);
}

wf_status wf_ozone_report_get_live_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_get_live_stats_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_live_stats_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_get_report(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_get_report_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_get_report_main_call(client, params, out);
}

wf_status wf_ozone_report_list_activities(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_list_activities_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_list_activities_main_call(client, params,
                                                               out);
}

wf_status wf_ozone_report_list_activities_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_list_activities_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_list_activities_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_query_activities(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_query_activities_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_query_activities_main_call(client, params,
                                                                out);
}

wf_status wf_ozone_report_query_activities_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_query_activities_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_query_activities_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_query_reports(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_query_reports_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_query_reports_main_call(client, params,
                                                             out);
}

wf_status wf_ozone_report_query_reports_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_query_reports_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_query_reports_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_reassign_queue(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_reassign_queue_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_reassign_queue_main_call(client, input,
                                                              out);
}

wf_status wf_ozone_report_reassign_queue_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_reassign_queue_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_reassign_queue_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_refresh_stats(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_refresh_stats_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_refresh_stats_main_call(client, input,
                                                             out);
}

wf_status wf_ozone_report_refresh_stats_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_report_refresh_stats_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_refresh_stats_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_report_unassign_moderator(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_report_unassign_moderator_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_report_unassign_moderator_main_call(client, input,
                                                                  out);
}

/* --- Team ---------------------------------------------------------- */

wf_status wf_ozone_team_add_member(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_add_member_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_team_add_member_main_call(client, input, out);
}

wf_status wf_ozone_team_delete_member(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_delete_member_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_team_delete_member_main_call(client, input, out);
}

wf_status wf_ozone_team_list_members(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_list_members_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_team_list_members_main_call(client, params, out);
}

wf_status wf_ozone_team_list_members_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_team_list_members_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_team_list_members_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_team_update_member(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_team_update_member_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_team_update_member_main_call(client, input, out);
}

/* --- Verification -------------------------------------------------- */

wf_status wf_ozone_verification_grant(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_verification_grant_verifications_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_verification_grant_verifications_main_call(
        client, input, out);
}

wf_status wf_ozone_verification_grant_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_verification_grant_verifications_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_verification_grant_verifications_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_verification_list(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_verification_list_verifications_main_params
        *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_verification_list_verifications_main_call(
        client, params, out);
}

wf_status wf_ozone_verification_list_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_verification_list_verifications_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_verification_list_verifications_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_verification_revoke(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_verification_revoke_verifications_main_input
        *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_verification_revoke_verifications_main_call(
        client, input, out);
}

wf_status wf_ozone_verification_revoke_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_verification_revoke_verifications_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_verification_revoke_verifications_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Signature ----------------------------------------------------- */

wf_status wf_ozone_signature_find_correlation(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_signature_find_correlation_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_signature_find_correlation_main_call(client,
                                                                   params, out);
}

wf_status wf_ozone_signature_find_correlation_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_signature_find_correlation_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_signature_find_correlation_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_signature_find_related_accounts(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_signature_find_related_accounts_main_params
        *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_signature_find_related_accounts_main_call(
        client, params, out);
}

wf_status wf_ozone_signature_find_related_accounts_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_signature_find_related_accounts_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_signature_find_related_accounts_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_signature_search_accounts(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_signature_search_accounts_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_signature_search_accounts_main_call(client,
                                                                  params, out);
}

wf_status wf_ozone_signature_search_accounts_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_signature_search_accounts_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_signature_search_accounts_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Setting ------------------------------------------------------- */

wf_status wf_ozone_setting_list_options(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_setting_list_options_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_setting_list_options_main_call(client, params,
                                                             out);
}

wf_status wf_ozone_setting_list_options_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_setting_list_options_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_setting_list_options_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_setting_remove_options(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_setting_remove_options_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_setting_remove_options_main_call(client, input,
                                                               out);
}

wf_status wf_ozone_setting_remove_options_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_setting_remove_options_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_setting_remove_options_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_setting_upsert_option(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_setting_upsert_option_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_setting_upsert_option_main_call(client, input,
                                                              out);
}

wf_status wf_ozone_setting_upsert_option_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_setting_upsert_option_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_setting_upsert_option_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Hosting ------------------------------------------------------- */

wf_status wf_ozone_hosting_get_account_history(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_hosting_get_account_history_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_hosting_get_account_history_main_call(
        client, params, out);
}

wf_status wf_ozone_hosting_get_account_history_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_hosting_get_account_history_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_hosting_get_account_history_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Server -------------------------------------------------------- */

wf_status wf_ozone_server_get_config(wf_xrpc_client *client, wf_response *out) {
    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_server_get_config_main_call(client, out);
}

wf_status wf_ozone_server_get_config_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_server_get_config_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_server_get_config_main_output_decode_json(
        resp->body, resp->body_len, out);
}

/* --- Safelink ------------------------------------------------------ */

wf_status wf_ozone_safelink_add_rule(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_add_rule_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_safelink_add_rule_main_call(client, input, out);
}

wf_status wf_ozone_safelink_query_events(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_query_events_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_safelink_query_events_main_call(client, input,
                                                              out);
}

wf_status wf_ozone_safelink_query_events_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_safelink_query_events_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_safelink_query_events_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_safelink_query_rules(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_query_rules_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_safelink_query_rules_main_call(client, input,
                                                             out);
}

wf_status wf_ozone_safelink_query_rules_parse(
    const wf_response *resp,
    wf_lex_tools_ozone_safelink_query_rules_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_safelink_query_rules_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_ozone_safelink_remove_rule(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_remove_rule_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_safelink_remove_rule_main_call(client, input,
                                                             out);
}

wf_status wf_ozone_safelink_update_rule(
    wf_xrpc_client *client,
    const wf_lex_tools_ozone_safelink_update_rule_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_tools_ozone_safelink_update_rule_main_call(client, input,
                                                             out);
}
