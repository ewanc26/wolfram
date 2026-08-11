#include "cli_repo.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/repo_typed.h"
#include "wolfram/sync_typed.h"
#include "wolfram/xrpc.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_repo(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    /* wolfram repo put-record <service> <handle> <password> --collection <nsid>
     *   --rkey <rkey> --json <record|file> */
    if (strcmp(sub, "put-record") == 0) {
        const char *collection = NULL, *rkey = NULL, *json = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--collection") == 0 && i + 1 < argc)
                collection = argv[++i];
            else if (strcmp(argv[i], "--rkey") == 0 && i + 1 < argc)
                rkey = argv[++i];
            else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc)
                json = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !collection || !rkey || !json) {
            fprintf(stderr,
                    "error: usage: wolfram repo put-record <service> "
                    "<handle> <password> --collection <nsid> --rkey <rkey> "
                    "--json <record|file>\n");
            return 1;
        }
        const char *record_json = json;
        char *file_buf = NULL;
        if (json[0] != '{') {
            file_buf = read_text_file(json);
            if (!file_buf) {
                fprintf(stderr, "error: cannot read record file '%s'\n", json);
                return 1;
            }
            record_json = file_buf;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) {
            free(file_buf);
            return 1;
        }
        wf_session_data sd = {0};
        wf_agent_get_session_data(agent, &sd);
        const char *repo = sd.did ? sd.did : pos[1];
        wf_repo_write_record_result out = {0};
        wf_status s = wf_agent_put_record_typed(
            agent, repo, collection, rkey, -1, record_json, NULL, NULL, &out);
        free(file_buf);
        wf_agent_session_data_free(&sd);
        if (s != WF_OK) {
            fprintf(stderr, "error: putRecord failed (status %d)\n", (int)s);
            wf_repo_write_record_result_free(&out);
            wf_agent_free(agent);
            return 1;
        }
        printf("%s\n", out.uri ? out.uri : "(no uri returned)");
        wf_repo_write_record_result_free(&out);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram repo delete-record <service> <handle> <password> --collection
     * <nsid>
     *   --rkey <rkey> */
    if (strcmp(sub, "delete-record") == 0) {
        const char *collection = NULL, *rkey = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--collection") == 0 && i + 1 < argc)
                collection = argv[++i];
            else if (strcmp(argv[i], "--rkey") == 0 && i + 1 < argc)
                rkey = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !collection || !rkey) {
            fprintf(stderr,
                    "error: usage: wolfram repo delete-record <service> "
                    "<handle> <password> --collection <nsid> --rkey <rkey>\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        wf_session_data sd = {0};
        wf_agent_get_session_data(agent, &sd);
        const char *repo = sd.did ? sd.did : pos[1];
        wf_status s = wf_agent_delete_record_typed(agent, repo, collection,
                                                   rkey, NULL, NULL);
        if (s != WF_OK) {
            fprintf(stderr, "error: deleteRecord failed (status %d)\n", (int)s);
            wf_agent_session_data_free(&sd);
            wf_agent_free(agent);
            return 1;
        }
        printf("deleted %s/%s/%s\n", repo, collection, rkey);
        wf_agent_session_data_free(&sd);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram repo list-records <service> <handle> <password> --collection
     * <nsid>
     *   [--limit N] [--cursor C] */
    if (strcmp(sub, "list-records") == 0) {
        const char *collection = NULL, *cursor = NULL;
        int limit = 50;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--collection") == 0 && i + 1 < argc)
                collection = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !collection) {
            fprintf(stderr,
                    "error: usage: wolfram repo list-records <service> "
                    "<handle> <password> --collection <nsid> [--limit N] "
                    "[--cursor C]\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        wf_session_data sd = {0};
        wf_agent_get_session_data(agent, &sd);
        const char *repo = sd.did ? sd.did : pos[1];
        if (g_json) {
            wf_response res = {0};
            wf_status s =
                wf_agent_list_records(agent, collection, limit, cursor, &res);
            wf_agent_session_data_free(&sd);
            return finish_agent_response(agent, s, &res);
        }
        wf_repo_record_list list = {0};
        wf_status s = wf_agent_list_records_typed(agent, repo, collection,
                                                  limit, cursor, 0, &list);
        wf_agent_session_data_free(&sd);
        if (s != WF_OK) {
            fprintf(stderr, "error: listRecords failed (status %d)\n", (int)s);
            wf_repo_record_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        for (size_t i = 0; i < list.count; ++i) {
            const wf_repo_record *r = &list.items[i];
            printf("%s (cid=%s)\n", r->uri ? r->uri : "?",
                   r->has_cid && r->cid ? r->cid : "?");
        }
        if (list.cursor) printf("cursor: %s\n", list.cursor);
        if (list.count == 0) printf("(no records)\n");
        wf_repo_record_list_free(&list);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram repo describe <service> <handle> <password> --repo
     * <did-or-handle> */
    if (strcmp(sub, "describe") == 0) {
        const char *repo = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--repo") == 0 && i + 1 < argc)
                repo = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !repo) {
            fprintf(stderr, "error: usage: wolfram repo describe <service> "
                            "<handle> <password> --repo <did-or-handle>\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        if (g_json) {
            wf_response res = {0};
            wf_status s = wf_agent_describe_repo(agent, repo, &res);
            return finish_agent_response(agent, s, &res);
        }
        wf_repo_description desc = {0};
        wf_status s = wf_agent_describe_repo_typed(agent, repo, &desc);
        if (s != WF_OK) {
            fprintf(stderr, "error: describeRepo failed (status %d)\n", (int)s);
            wf_repo_description_free(&desc);
            wf_agent_free(agent);
            return 1;
        }
        printf("handle: %s\n", desc.handle ? desc.handle : "?");
        printf("did: %s\n", desc.did ? desc.did : "?");
        printf("handleIsCorrect: %d\n", desc.handle_is_correct);
        printf("collections (%zu):\n", desc.collection_count);
        for (size_t i = 0; i < desc.collection_count; ++i) {
            printf("  %s\n", desc.collections[i] ? desc.collections[i] : "?");
        }
        wf_repo_description_free(&desc);
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr,
            "error: unknown repo subcommand '%s' "
            "(try put-record/delete-record/list-records/describe)\n",
            sub);
    return 1;
}

int cmd_get_record(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *repo = argv[2];
    const char *collection = argv[3];
    const char *rkey = argv[4];

    wf_xrpc_client *client = wf_xrpc_client_new(service);
    if (!client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        return 1;
    }

    wf_xrpc_param params[] = {
        {"repo", repo},
        {"collection", collection},
        {"rkey", rkey},
    };

    wf_response res = {0};
    wf_status s = wf_xrpc_query_params(client, "com.atproto.repo.getRecord",
                                       params, 3, &res);
    wf_xrpc_client_free(client);

    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getRecord failed (status %d)\n", (int)s);
        wf_response_free(&res);
        return 1;
    }

    if (res.body && res.body_len > 0) {
        printf("%s\n", res.body);
    } else {
        printf("(empty response, HTTP %ld)\n", res.status);
    }

    wf_response_free(&res);
    return 0;
}