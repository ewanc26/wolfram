#include "cli_prefs.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/actor_prefs_typed.h"
#include "wolfram/actor_typed.h"
#include "wolfram/repo_typed.h"
#include "wolfram/chat_typed.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_preferences(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "get") == 0) {
        if (argc < 5) {
            usage_stream(stderr);
            return 0;
        }
        wf_agent *agent = agent_login_or_err(argv[2], argv[3], argv[4]);
        if (!agent) return 1;

        char *prefs_json = NULL;
        wf_status s = wf_agent_get_preferences(agent, &prefs_json);
        if (s != WF_OK) {
            fprintf(stderr, "error: getPreferences failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("%s\n", prefs_json ? prefs_json : "(none)");
        free(prefs_json);
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "put") == 0) {
        const char *prefs_json_str = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--json") == 0 && i + 1 < argc)
                prefs_json_str = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !prefs_json_str) {
            fprintf(stderr, "error: usage: wolfram preferences put <service> "
                            "<handle> <password> --json <prefs-json>\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_response res = {0};
        wf_status s =
            wf_agent_put_preferences(agent, prefs_json_str, &res);
        return finish_agent_response(agent, s, &res);
    }

    fprintf(stderr, "error: unknown preferences subcommand '%s' (try "
                    "get/put)\n",
            sub);
    return 1;
}

int cmd_register_push(int argc, char **argv) {
    const char *service_did = NULL, *token = NULL, *platform = NULL,
               *app_id = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--service-did") == 0 && i + 1 < argc)
            service_did = argv[++i];
        else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc)
            token = argv[++i];
        else if (strcmp(argv[i], "--platform") == 0 && i + 1 < argc)
            platform = argv[++i];
        else if (strcmp(argv[i], "--app-id") == 0 && i + 1 < argc)
            app_id = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !service_did || !token) {
        fprintf(stderr, "error: usage: wolfram register-push <service> "
                        "<handle> <password> --service-did <did> --token "
                        "<token> [--platform <p>] [--app-id <id>]\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s;
    if (platform || app_id) {
        s = wf_agent_register_push_ext(agent, service_did, token, platform,
                                       app_id, &res);
    } else {
        s = wf_agent_register_push(agent, service_did, token, &res);
    }
    return finish_agent_response(agent, s, &res);
}

int cmd_unregister_push(int argc, char **argv) {
    const char *service_did = NULL, *token = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--service-did") == 0 && i + 1 < argc)
            service_did = argv[++i];
        else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc)
            token = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !service_did || !token) {
        fprintf(stderr, "error: usage: wolfram unregister-push <service> "
                        "<handle> <password> --service-did <did> --token "
                        "<token>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_unregister_push(agent, service_did, token, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_update_profile(int argc, char **argv) {
    wf_agent_profile_update update = {0};
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--display-name") == 0 && i + 1 < argc)
            update.display_name = argv[++i];
        else if (strcmp(argv[i], "--description") == 0 && i + 1 < argc)
            update.description = argv[++i];
        else if (strcmp(argv[i], "--avatar-cid") == 0 && i + 1 < argc)
            update.avatar_cid = argv[++i];
        else if (strcmp(argv[i], "--banner-cid") == 0 && i + 1 < argc)
            update.banner_cid = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3) {
        usage_stream(stderr);
        return 0;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s = wf_agent_update_profile(agent, &update);
    if (s != WF_OK) {
        fprintf(stderr, "error: updateProfile failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("profile updated\n");
    wf_agent_free(agent);
    return 0;
}

int cmd_put_actor_status(int argc, char **argv) {
    const char *status = NULL, *embed_json = NULL;
    int duration = 0;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
            duration = atoi(argv[++i]);
        else if (strcmp(argv[i], "--embed") == 0 && i + 1 < argc)
            embed_json = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
        else if (!status)
            status = argv[i];
    }
    if (pi < 3 || !status) {
        fprintf(stderr, "error: usage: wolfram put-actor-status <service> "
                        "<handle> <password> <status> [--duration <minutes>] "
                        "[--embed <json>]\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_agent_post_result out = {0};
    wf_status s = wf_agent_put_actor_status(agent, status, duration,
                                             embed_json, &out);
    if (s != WF_OK) {
        fprintf(stderr, "error: putActorStatus failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }
    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

int cmd_upload_blob(int argc, char **argv) {
    const char *content_type = NULL;
    const char *pos[3];
    const char *file_path = NULL;
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--content-type") == 0 && i + 1 < argc)
            content_type = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
        else if (!file_path)
            file_path = argv[i];
    }
    if (pi < 3 || !file_path) {
        fprintf(stderr, "error: usage: wolfram upload-blob <service> <handle> "
                        "<password> <file> [--content-type <type>]\n");
        return 1;
    }

    char *data = read_text_file(pos[2] ? pos[2] : file_path);
    /* Re-read the actual file, not the password */
    data = read_text_file(file_path);
    if (!data) {
        fprintf(stderr, "error: could not read file '%s'\n", file_path);
        return 1;
    }

    /* Determine file size */
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open file '%s'\n", file_path);
        free(data);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fclose(f);

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) {
        free(data);
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_upload_blob(agent, data, (size_t)file_len,
                                       content_type, &res);
    free(data);
    return finish_agent_response(agent, s, &res);
}

int cmd_apply_writes(int argc, char **argv) {
    const char *repo = NULL, *writes_json = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--repo") == 0 && i + 1 < argc)
            repo = argv[++i];
        else if (strcmp(argv[i], "--writes-json") == 0 && i + 1 < argc)
            writes_json = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !repo || !writes_json) {
        fprintf(stderr, "error: usage: wolfram apply-writes <service> <handle> "
                        "<password> --repo <repo> --writes-json <json>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    /* Parse the writes JSON into wf_agent_write array */
    cJSON *writes_arr = cJSON_Parse(writes_json);
    if (!writes_arr || !cJSON_IsArray(writes_arr)) {
        fprintf(stderr, "error: --writes-json must be a JSON array\n");
        cJSON_Delete(writes_arr);
        wf_agent_free(agent);
        return 1;
    }

    int n_writes = cJSON_GetArraySize(writes_arr);
    wf_agent_write *writes = calloc(n_writes, sizeof(wf_agent_write));
    if (!writes) {
        cJSON_Delete(writes_arr);
        wf_agent_free(agent);
        return 1;
    }

    for (int i = 0; i < n_writes; ++i) {
        cJSON *w = cJSON_GetArrayItem(writes_arr, i);
        cJSON *type = cJSON_GetObjectItemCaseSensitive(w, "$type");
        cJSON *collection = cJSON_GetObjectItemCaseSensitive(w, "collection");
        cJSON *rkey = cJSON_GetObjectItemCaseSensitive(w, "rkey");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(w, "value");
        if (cJSON_IsString(type) && type->valuestring) {
            if (strcmp(type->valuestring, "create") == 0)
                writes[i].type = WF_AGENT_WRITE_CREATE;
            else if (strcmp(type->valuestring, "update") == 0)
                writes[i].type = WF_AGENT_WRITE_UPDATE;
            else if (strcmp(type->valuestring, "delete") == 0)
                writes[i].type = WF_AGENT_WRITE_DELETE;
        }
        if (cJSON_IsString(collection) && collection->valuestring)
            writes[i].collection = collection->valuestring;
        if (cJSON_IsString(rkey) && rkey->valuestring)
            writes[i].rkey = rkey->valuestring;
        if (value) {
            writes[i].value_json = cJSON_PrintUnformatted(value);
        }
    }

    wf_response res = {0};
    wf_status s = wf_agent_apply_writes(agent, writes, n_writes, &res);

    /* Free allocated values */
    for (int i = 0; i < n_writes; ++i) {
        free((void *)writes[i].value_json);
    }
    free(writes);
    cJSON_Delete(writes_arr);

    return finish_agent_response(agent, s, &res);
}

int cmd_send_interactions(int argc, char **argv) {
    const char *feed_uri = NULL, *interactions_json = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--feed") == 0 && i + 1 < argc)
            feed_uri = argv[++i];
        else if (strcmp(argv[i], "--interactions-json") == 0 && i + 1 < argc)
            interactions_json = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !interactions_json) {
        fprintf(stderr, "error: usage: wolfram send-interactions <service> "
                        "<handle> <password> [--feed <feed-uri>] "
                        "--interactions-json <json>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_send_interactions(agent, feed_uri,
                                              interactions_json, &res);
    return finish_agent_response(agent, s, &res);
}
