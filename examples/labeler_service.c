/*
 * labeler_service.c — create and read an app.bsky.labeler.service record.
 *
 * Demonstrates:
 *   1. High-level agent login with wf_agent
 *   2. Building an app.bsky.labeler.service record value with cJSON, including
 *      a `policies` object whose `labelValueDefinitions` reference atproto
 *      label values (com.atproto.label.defs#labelValueDefinition)
 *   3. Record creation via wf_agent_create_record
 *   4. Reading the record back with wf_agent_get_record
 *
 * Usage (all network gated behind argv — no args prints usage, exit 0):
 *   labeler_service <service-url> <handle-or-email> <password> <labeler-did>
 *
 * The <labeler-did> is the DID of the account that will run the labeler
 * (usually your own).
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wolfram/agent.h"

static int wf_make_rfc3339_timestamp(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm *tmp = gmtime(&now);
    if (!tmp) {
        return 0;
    }

    tm_utc = *tmp;
    return strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 0;
}

/* Extract the record key (rkey) from an at:// URI. Caller must free. */
static char *wf_rkey_from_aturi(const char *uri) {
    if (!uri) {
        return NULL;
    }

    size_t len = strlen(uri);
    const char *last_slash = NULL;
    for (size_t i = 0; i < len; ++i) {
        if (uri[i] == '/') {
            last_slash = uri + i;
        }
    }

    if (!last_slash || last_slash == uri + len - 1) {
        return NULL;
    }

    const char *rkey = last_slash + 1;
    size_t rkey_len = strlen(rkey);
    char *out = malloc(rkey_len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, rkey, rkey_len);
    out[rkey_len] = '\0';
    return out;
}

/* One label value definition entry for policies.labelValueDefinitions. */
static void add_label_value_def(cJSON *defs, const char *identifier,
                                const char *severity, const char *blurs,
                                int adult_only, const char *default_setting) {
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

static void print_usage(const char *prog) {
    printf("usage: %s <service-url> <handle-or-email> <password> <labeler-did>\n",
           prog);
    printf("  (no arguments: print usage and exit 0 — offline safe)\n");
}

int main(int argc, char **argv) {
    if (argc < 5) {
        print_usage(argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *labeler_did = argv[4];

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

    const char *handle = wf_agent_get_handle(agent);
    const char *did    = wf_agent_get_did(agent);
    printf("Logged in as %s (%s)\n", handle ? handle : identifier, did);

    /* Build the app.bsky.labeler.service record value. */
    cJSON *record = cJSON_CreateObject();
    if (!record) {
        fprintf(stderr, "failed to allocate record JSON\n");
        wf_agent_free(agent);
        return 1;
    }

    cJSON_AddStringToObject(record, "$type", "app.bsky.labeler.service");
    cJSON_AddStringToObject(record, "labelerDid", labeler_did);

    char created_at[32];
    if (!wf_make_rfc3339_timestamp(created_at, sizeof(created_at))) {
        fprintf(stderr, "failed to create timestamp\n");
        cJSON_Delete(record);
        wf_agent_free(agent);
        return 1;
    }
    cJSON_AddStringToObject(record, "createdAt", created_at);

    /* policies.labelValueDefinitions — advertise which labels this labeler
     * can issue, referencing atproto label values. */
    cJSON *policies = cJSON_CreateObject();
    cJSON *defs = cJSON_CreateArray();

    add_label_value_def(defs, "!no-unauthenticated", "inform", "none", 0,
                        "warn");
    add_label_value_def(defs, "!hide", "inform", "none", 0, "warn");
    add_label_value_def(defs, "spam", "alert", "content", 0, "warn");
    add_label_value_def(defs, "porn", "alert", "content", 1, "hide");

    cJSON_AddItemToObject(policies, "labelValueDefinitions", defs);
    cJSON_AddItemToObject(record, "policies", policies);

    char *record_json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!record_json) {
        fprintf(stderr, "failed to serialize record JSON\n");
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result res = {0};
    status = wf_agent_create_record(agent, "app.bsky.labeler.service",
                                    record_json, &res);
    free(record_json);

    if (status != WF_OK) {
        fprintf(stderr, "createRecord failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    printf("Created app.bsky.labeler.service record:\n");
    printf("  uri: %s\n", res.uri);
    printf("  cid: %s\n", res.cid);

    /* Read it back with getRecord. */
    char *rkey = wf_rkey_from_aturi(res.uri);
    wf_agent_post_result_free(&res);
    if (!rkey) {
        fprintf(stderr, "failed to parse rkey from created uri\n");
        wf_agent_free(agent);
        return 1;
    }

    wf_response get_res = {0};
    status = wf_agent_get_record(agent, "app.bsky.labeler.service", rkey,
                                 &get_res);
    if (status != WF_OK) {
        fprintf(stderr, "getRecord failed: %d\n", (int)status);
        free(rkey);
        wf_response_free(&get_res);
        wf_agent_free(agent);
        return 1;
    }

    printf("Read back record:\n");
    if (get_res.body && get_res.body_len) {
        cJSON *root = cJSON_ParseWithLength(get_res.body, get_res.body_len);
        if (root) {
            cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
            cJSON *v = value ? value : root;
            cJSON *ld = cJSON_GetObjectItemCaseSensitive(v, "labelerDid");
            cJSON *pol = cJSON_GetObjectItemCaseSensitive(v, "policies");
            cJSON *defs_out = pol ? cJSON_GetObjectItemCaseSensitive(
                                        pol, "labelValueDefinitions")
                                  : NULL;
            if (cJSON_IsString(ld)) {
                printf("  labelerDid: %s\n", ld->valuestring);
            }
            if (cJSON_IsArray(defs_out)) {
                printf("  labelValueDefinitions: %d\n",
                       cJSON_GetArraySize(defs_out));
                for (int i = 0; i < cJSON_GetArraySize(defs_out); ++i) {
                    cJSON *d = cJSON_GetArrayItem(defs_out, i);
                    cJSON *id = cJSON_GetObjectItemCaseSensitive(
                        d, "identifier");
                    if (cJSON_IsString(id)) {
                        printf("    - %s\n", id->valuestring);
                    }
                }
            }
            cJSON_Delete(root);
        } else {
            printf("  (could not parse response body)\n");
        }
    }
    wf_response_free(&get_res);
    free(rkey);

    wf_agent_free(agent);
    return 0;
}
