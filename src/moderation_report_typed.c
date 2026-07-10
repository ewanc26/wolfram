/*
 * moderation_report_typed.c — typed parser + agent wrapper for
 * com.atproto.moderation.createReport. See include/wolfram/moderation_report_typed.h
 * for the public API and ownership rules. Follows the conventions of
 * actor_typed.c / chat_typed.c: static strdup/set_string/reset helpers, owned
 * strings, full cleanup on the first error. The wrapper calls the generated
 * lexicon procedure wrapper on the agent's primary XRPC client after syncing
 * auth.
 */

#include "wolfram/moderation_report_typed.h"

#include "wolfram/agent.h"
#include "wolfram/xrpc.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

#include "agent/_internal.h"

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_moderation_report_record_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_moderation_report_record_set_string(char **dst, const char *src) {
    char *copy = wf_moderation_report_record_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_moderation_report_record_reset(wf_moderation_report_record *r) {
    if (!r) {
        return;
    }
    free(r->reason_type);
    free(r->reason);
    cJSON_Delete(r->subject);
    free(r->reported_by);
    free(r->created_at);
    memset(r, 0, sizeof(*r));
}

wf_status wf_moderation_report_record_parse(const char *json, size_t len,
                                     wf_moderation_report_record *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *reason_type = cJSON_GetObjectItemCaseSensitive(root, "reasonType");
    cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
    cJSON *subject = cJSON_GetObjectItemCaseSensitive(root, "subject");
    cJSON *reported_by = cJSON_GetObjectItemCaseSensitive(root, "reportedBy");
    cJSON *created_at = cJSON_GetObjectItemCaseSensitive(root, "createdAt");
    if (!cJSON_IsNumber(id) || id->valuedouble != (double)id->valueint ||
        !cJSON_IsString(reason_type) || !reason_type->valuestring ||
        !cJSON_IsObject(subject) || !cJSON_IsString(reported_by) ||
        !reported_by->valuestring || !cJSON_IsString(created_at) ||
        !created_at->valuestring || (reason && !cJSON_IsString(reason))) {
        status = WF_ERR_PARSE;
    } else {
        out->id = (int64_t)id->valuedouble;
        status = wf_moderation_report_record_set_string(
            &out->reason_type, reason_type->valuestring);
        if (status == WF_OK && reason)
            status = wf_moderation_report_record_set_string(
                &out->reason, reason->valuestring);
        if (status == WF_OK) {
            out->subject = cJSON_Duplicate(subject, true);
            if (!out->subject) status = WF_ERR_ALLOC;
        }
        if (status == WF_OK)
            status = wf_moderation_report_record_set_string(
                &out->reported_by, reported_by->valuestring);
        if (status == WF_OK)
            status = wf_moderation_report_record_set_string(
                &out->created_at, created_at->valuestring);
    }

    cJSON_Delete(root);

    if (status != WF_OK) {
        wf_moderation_report_record_reset(out);
    }
    return status;
}

void wf_moderation_report_record_free(wf_moderation_report_record *r) {
    wf_moderation_report_record_reset(r);
}

wf_status wf_agent_report(wf_agent *agent, const char *subject_uri,
                          const char *subject_cid, const char *reason_type,
                          const char *reason_subject_uri,
                          wf_moderation_report_record *out) {
    if (!subject_uri || !subject_uri[0] || reason_subject_uri) {
        return WF_ERR_INVALID_ARG;
    }
    if (strncmp(subject_uri, "did:", 4) == 0)
        return wf_agent_report_typed(agent, subject_uri, NULL, NULL,
                                     reason_type, NULL, NULL, NULL, out);
    return wf_agent_report_typed(agent, NULL, subject_uri, subject_cid,
                                 reason_type, NULL, NULL, NULL, out);
}

wf_status wf_agent_report_typed(wf_agent *agent,
                                const char *subject_did,
                                const char *subject_uri,
                                const char *subject_cid,
                                const char *reason_type,
                                const char *reason,
                                const char *mod_tool_name,
                                const char *mod_tool_meta_json,
                                wf_moderation_report_record *out) {
    int repo_subject = subject_did && subject_did[0];
    int record_subject = subject_uri && subject_uri[0] &&
                         subject_cid && subject_cid[0];
    if (!agent || !out || !reason_type || !reason_type[0] ||
        repo_subject == record_subject ||
        (subject_uri && subject_uri[0] && !record_subject) ||
        (subject_cid && subject_cid[0] && !record_subject) ||
        (reason && strlen(reason) > 20000) ||
        (mod_tool_meta_json && (!mod_tool_name || !mod_tool_name[0]))) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    wf_agent_sync_auth(agent);

    cJSON *subject = cJSON_CreateObject();
    if (!subject) {
        return WF_ERR_ALLOC;
    }
    const char *subject_type = repo_subject
        ? "com.atproto.admin.defs#repoRef"
        : "com.atproto.repo.strongRef";
    if (!cJSON_AddStringToObject(subject, "$type", subject_type) ||
        !cJSON_AddStringToObject(subject, repo_subject ? "did" : "uri",
                                 repo_subject ? subject_did : subject_uri)) {
        cJSON_Delete(subject);
        return WF_ERR_ALLOC;
    }
    if (record_subject) {
        if (!cJSON_AddStringToObject(subject, "cid", subject_cid)) {
            cJSON_Delete(subject);
            return WF_ERR_ALLOC;
        }
    }
    char *subject_json = cJSON_PrintUnformatted(subject);
    cJSON_Delete(subject);
    if (!subject_json) {
        return WF_ERR_ALLOC;
    }

    wf_lex_com_atproto_moderation_create_report_main_input input;
    memset(&input, 0, sizeof(input));
    input.reason_type = reason_type;
    if (reason) {
        input.has_reason = true;
        input.reason = reason;
    }
    input.subject.kind = -1;
    input.subject.data = subject_json;
    input.subject.length = strlen(subject_json);

    wf_lex_com_atproto_moderation_create_report_mod_tool mod_tool = {0};
    if (mod_tool_name && mod_tool_name[0]) {
        input.has_mod_tool = true;
        mod_tool.name = mod_tool_name;
        if (mod_tool_meta_json) {
            cJSON *meta = cJSON_Parse(mod_tool_meta_json);
            if (!meta) {
                wf_lex_com_atproto_moderation_create_report_main_json_free(
                    subject_json);
                return WF_ERR_PARSE;
            }
            cJSON_Delete(meta);
            mod_tool.has_meta = true;
            mod_tool.meta.data = mod_tool_meta_json;
            mod_tool.meta.length = strlen(mod_tool_meta_json);
        }
        input.mod_tool = &mod_tool;
    }

    wf_response res = {0};
    wf_status status =
        wf_lex_com_atproto_moderation_create_report_main_call(agent->client,
                                                              &input, &res);
    if (status == WF_OK) {
        status = wf_moderation_report_record_parse(res.body, res.body_len, out);
    }

    wf_response_free(&res);
    wf_lex_com_atproto_moderation_create_report_main_json_free(subject_json);

    if (status != WF_OK) {
        wf_moderation_report_record_reset(out);
    }
    return status;
}
