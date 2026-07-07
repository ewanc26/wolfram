/*
 * labeler_service.c — create an app.bsky.labeler.service record.
 *
 * Demonstrates:
 *   1. High-level agent login with wf_agent
 *   2. Building an app.bsky.labeler.service record value with cJSON
 *   3. Generic record creation via wf_agent_create_record
 *   4. Printing the returned uri/cid and freeing everything
 *
 * Usage:
 *   labeler_service <service-url> <handle-or-email> <password> <labeler-did>
 *
 * The <labeler-did> is the DID of the account that will run the labeler
 * (usually your own). The `labels` array references
 * com.atproto.label.defs#labelValue entries to advertise which labels the
 * labeler can issue.
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

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <service-url> <handle-or-email> <password> <labeler-did>\n",
                argv[0]);
        return 1;
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

    cJSON *labels = cJSON_CreateArray();
    if (!labels) {
        fprintf(stderr, "failed to allocate labels array\n");
        cJSON_Delete(record);
        wf_agent_free(agent);
        return 1;
    }

    /* A label value references com.atproto.label.defs#labelValue */
    const char *const sample_labels[] = {
        "!no-unauthenticated",
        "!hide",
        "spam",
    };

    for (size_t i = 0; i < sizeof(sample_labels) / sizeof(sample_labels[0]); ++i) {
        cJSON *label = cJSON_CreateObject();
        if (!label) {
            fprintf(stderr, "failed to allocate label value\n");
            cJSON_Delete(labels);
            cJSON_Delete(record);
            wf_agent_free(agent);
            return 1;
        }
        cJSON_AddStringToObject(label, "$type", "com.atproto.label.defs#labelValue");
        cJSON_AddStringToObject(label, "val", sample_labels[i]);
        cJSON_AddBoolToObject(label, "neg", sample_labels[i][0] == '!');
        cJSON_AddItemToArray(labels, label);
    }

    cJSON_AddItemToObject(record, "labels", labels);

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

    wf_agent_post_result_free(&res);
    wf_agent_free(agent);
    return 0;
}
