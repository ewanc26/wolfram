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
    free(r->uri);
    free(r->cid);
    if (r->value) {
        cJSON_Delete(r->value);
    }
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
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(root, "uri");
    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_moderation_report_record_set_string(&out->uri, uri->valuestring);
    }

    if (status == WF_OK) {
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
        if (cJSON_IsString(cid) && cid->valuestring) {
            status =
                wf_moderation_report_record_set_string(&out->cid, cid->valuestring);
        }
    }

    if (status == WF_OK) {
        /* Detach the arbitrary record subtree verbatim and take ownership. */
        cJSON *value = cJSON_DetachItemFromObject(root, "value");
        if (value) {
            out->value = value;
        }
    }

    cJSON_Delete(root);

    if (status != WF_OK) {
        wf_moderation_report_record_reset(out);
    }
    return status;
}

void wf_moderation_report_record_free(wf_moderation_report_record *r) {
    if (!r) {
        return;
    }
    free(r->uri);
    free(r->cid);
    if (r->value) {
        cJSON_Delete(r->value);
    }
    memset(r, 0, sizeof(*r));
}

wf_status wf_agent_report(wf_agent *agent, const char *subject_uri,
                          const char *subject_cid, const char *reason_type,
                          const char *reason_subject_uri,
                          wf_moderation_report_record *out) {
    if (!agent || !subject_uri || !*subject_uri || !reason_type ||
        !*reason_type) {
        return WF_ERR_INVALID_ARG;
    }
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    /* The current com.atproto.moderation.createReport lexicon carries the
     * reason as a flat `reasonType` token plus an optional free-text `reason`
     * and a union `subject`; there is no nested reason subject, so the
     * historical `reason_subject_uri` argument has no wire slot here. */
    (void)reason_subject_uri;

    wf_agent_sync_auth(agent);

    /* Build the subject union member (strongRef: {uri, cid?}) as a JSON blob,
     * mirroring the lexicon input encoder. */
    cJSON *subject = cJSON_CreateObject();
    if (!subject) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(subject, "uri", subject_uri)) {
        cJSON_Delete(subject);
        return WF_ERR_ALLOC;
    }
    if (subject_cid && *subject_cid) {
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
    input.subject.kind = -1;
    input.subject.data = subject_json;
    input.subject.length = strlen(subject_json);

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
