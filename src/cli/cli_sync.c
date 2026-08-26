#include "cli_sync.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/sync_typed.h"
#include "wolfram/repo_typed.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_sync_get_blob(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *did = argv[2];
    const char *cid = argv[3];

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_sync_get_blob(agent, did, cid, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_sync_get_blocks(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *did = argv[2];
    const char *const *cids = (const char *const *)(argv + 3);
    int n_cids = argc - 3;

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_sync_get_blocks(agent, did, cids, n_cids, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_sync_get_record(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *did = argv[2];
    const char *collection = argv[3];
    const char *rkey = argv[4];

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_sync_get_record(agent, did, collection, rkey, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_sync_list_blobs(int argc, char **argv) {
    const char *since = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--since") == 0 && i + 1 < argc)
            since = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 2) {
        usage_stream(stderr);
        return 0;
    }
    int limit = (pi >= 3) ? atoi(pos[2]) : 500;

    wf_agent *agent = cli_agent_new(pos[0]);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s =
        wf_agent_sync_list_blobs(agent, pos[1], limit, NULL, since, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_import_repo(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *car_path = argv[4];

    char *car_data = read_text_file(car_path);
    if (!car_data) {
        fprintf(stderr, "error: could not read CAR file '%s'\n", car_path);
        return 1;
    }

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        free(car_data);
        return 1;
    }

    size_t car_len = strlen(car_data);
    wf_status s = wf_agent_import_repo_typed(agent, car_data, car_len);
    free(car_data);
    if (s != WF_OK) {
        fprintf(stderr, "error: importRepo failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("repo imported\n");
    wf_agent_free(agent);
    return 0;
}

int cmd_list_missing_blobs(int argc, char **argv) {
    const char *cursor = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
            cursor = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3) {
        usage_stream(stderr);
        return 0;
    }
    int limit = 500;

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_repo_missing_blob_list out = {0};
    wf_status s = wf_agent_list_missing_blobs_typed(agent, limit, cursor, &out);
    if (s != WF_OK) {
        fprintf(stderr, "error: listMissingBlobs failed (status %d)\n", (int)s);
        wf_repo_missing_blob_list_free(&out);
        wf_agent_free(agent);
        return 1;
    }
    printf("missing blobs:\n");
    for (size_t i = 0; i < out.count; ++i) {
        printf("  cid=%s uri=%s\n", out.items[i].cid ? out.items[i].cid : "?",
               out.items[i].record_uri ? out.items[i].record_uri : "?");
    }
    if (out.cursor) printf("cursor: %s\n", out.cursor);
    wf_repo_missing_blob_list_free(&out);
    wf_agent_free(agent);
    return 0;
}
