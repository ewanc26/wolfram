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

    add_label_value_def(defs, "!no-unauthenticated", "inform", "none", 0, "warn");
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

wf_status wf_ozone_report_subject(wf_xrpc_client *client,
                                  const char *repo_did,
                                  const char *collection,
                                  const char *rkey,
                                  const char *reason_type,
                                  const char *reason,
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
        cJSON_AddStringToObject(subject, "$type",
                                "com.atproto.repo.strongRef");
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

wf_status wf_ozone_query_statuses(wf_xrpc_client *client,
                                  const char **subjects,
                                  size_t n,
                                  wf_response *out) {
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

    wf_status status = wf_xrpc_query_params(client, WF_OZONE_QUERY_STATUSES_NSID,
                                            params, n, out);
    free(params);
    return status;
}

wf_status wf_ozone_get_label_defs(wf_xrpc_client *client,
                                  const char **uris,
                                  size_t n,
                                  wf_response *out) {
    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param *params = NULL;
    if (uris && n > 0) {
        params = (wf_xrpc_param *)calloc(n, sizeof(wf_xrpc_param));
        if (!params) {
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < n; ++i) {
            params[i].name = "uris";
            params[i].value = uris[i];
        }
    }

    /* Prefer getLabelDefinitions; fall back to getSuggestions when it is not
     * served by the instance (transport-level failure only — an HTTP error
     * from getLabelDefinitions is propagated as-is). */
    wf_status status = wf_xrpc_query_params(
        client, WF_OZONE_GET_LABEL_DEFS_NSID, params, n, out);
    if (status != WF_OK && status != WF_ERR_HTTP) {
        status = wf_xrpc_query_params(client, WF_OZONE_GET_SUGGESTIONS_NSID,
                                      params, n, out);
    }
    free(params);
    return status;
}
