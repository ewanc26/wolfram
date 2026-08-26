#include "cli_graph.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/graph_typed.h"
#include "wolfram/graph_social_typed.h"
#include "wolfram/graph_write.h"
#include "wolfram/repo_typed.h"
#include "wolfram/unspecced_typed.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parse_list_create_args(int argc, char **argv, const char *pos[3],
                           const char **name, const char **purpose,
                           const char **description) {
    *name = NULL;
    *purpose = NULL;
    *description = NULL;
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
            *name = argv[++i];
        else if (strcmp(argv[i], "--purpose") == 0 && i + 1 < argc)
            *purpose = argv[++i];
        else if (strcmp(argv[i], "--description") == 0 && i + 1 < argc)
            *description = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    return (pi >= 3 && *name != NULL) ? 0 : -1;
}

int cmd_follows(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_follows(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_followers(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_followers(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_blocks(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_blocks(agent, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_mutes(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_mutes(agent, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_list(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "create") == 0) {
        return cmd_list_create(argc - 1, argv + 1);
    }
    if (strcmp(sub, "update") == 0) {
        return cmd_list_update(argc - 1, argv + 1);
    }
    if (strcmp(sub, "delete") == 0) {
        return cmd_list_delete(argc - 1, argv + 1);
    }
    if (strcmp(sub, "add-item") == 0) {
        return cmd_list_add_item(argc - 1, argv + 1);
    }
    if (strcmp(sub, "remove-item") == 0) {
        return cmd_list_remove_item(argc - 1, argv + 1);
    }

    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *list_uri = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_list(agent, list_uri, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_lists(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_lists(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_list_create(int argc, char **argv) {
    const char *pos[3];
    const char *name, *purpose, *description;
    if (parse_list_create_args(argc, argv, pos, &name, &purpose,
                               &description) != 0) {
        fprintf(stderr,
                "error: usage: wolfram list create <service> <handle> "
                "<password> --name <name> [--purpose modlist|curatelist|"
                "referencelist] [--description <desc>]\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    const char *purpose_str =
        purpose ? purpose : "app.bsky.graph.defs#curatelist";
    if (purpose && strcmp(purpose, "modlist") == 0)
        purpose_str = "app.bsky.graph.defs#modlist";
    else if (purpose && strcmp(purpose, "curatelist") == 0)
        purpose_str = "app.bsky.graph.defs#curatelist";
    else if (purpose && strcmp(purpose, "referencelist") == 0)
        purpose_str = "app.bsky.graph.defs#referencelist";

    wf_agent_post_result out = {0};
    wf_status s =
        wf_agent_graph_create_list(agent, purpose_str, name, description, &out);
    if (s != WF_OK) {
        fprintf(stderr, "error: createList failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }
    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

int cmd_list_update(int argc, char **argv) {
    const char *list_uri = NULL, *name = NULL, *description = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
            list_uri = argv[++i];
        else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
            name = argv[++i];
        else if (strcmp(argv[i], "--description") == 0 && i + 1 < argc)
            description = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !list_uri) {
        fprintf(stderr, "error: usage: wolfram list update <service> <handle> "
                        "<password> --list <list-uri> [--name <name>] "
                        "[--description <desc>]\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    /* Extract rkey from list URI */
    const char *rkey = strrchr(list_uri, '/');
    if (!rkey) {
        fprintf(stderr, "error: invalid list URI '%s'\n", list_uri);
        wf_agent_free(agent);
        return 1;
    }
    rkey++;

    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    const char *repo = sd.did ? sd.did : pos[1];

    /* putRecord replaces the whole record, so fetch it first and merge the
     * requested fields into the existing value ($type/createdAt/purpose
     * must be preserved). */
    wf_repo_record rec = {0};
    wf_status s = wf_agent_get_record_typed(agent, repo, "app.bsky.graph.list",
                                            rkey, NULL, &rec);
    if (s != WF_OK) {
        fprintf(stderr, "error: getRecord failed (status %d)\n", (int)s);
        wf_agent_session_data_free(&sd);
        wf_agent_free(agent);
        return 1;
    }
    if (!rec.value) {
        fprintf(stderr, "error: record has no value\n");
        wf_repo_record_free(&rec);
        wf_agent_session_data_free(&sd);
        wf_agent_free(agent);
        return 1;
    }

    if (name) {
        cJSON_DeleteItemFromObject(rec.value, "name");
        if (!cJSON_AddStringToObject(rec.value, "name", name)) {
            fprintf(stderr, "error: allocation failure\n");
            wf_repo_record_free(&rec);
            wf_agent_session_data_free(&sd);
            wf_agent_free(agent);
            return 1;
        }
    }
    if (description) {
        cJSON_DeleteItemFromObject(rec.value, "description");
        if (!cJSON_AddStringToObject(rec.value, "description", description)) {
            fprintf(stderr, "error: allocation failure\n");
            wf_repo_record_free(&rec);
            wf_agent_session_data_free(&sd);
            wf_agent_free(agent);
            return 1;
        }
    }

    char *record_json = cJSON_PrintUnformatted(rec.value);
    if (!record_json) {
        fprintf(stderr, "error: allocation failure\n");
        wf_repo_record_free(&rec);
        wf_agent_session_data_free(&sd);
        wf_agent_free(agent);
        return 1;
    }

    wf_repo_write_record_result out = {0};
    s = wf_agent_put_record_typed(agent, repo, "app.bsky.graph.list", rkey, -1,
                                  record_json, NULL, NULL, &out);
    free(record_json);
    if (s != WF_OK) {
        fprintf(stderr, "error: updateList failed (status %d)\n", (int)s);
        wf_repo_write_record_result_free(&out);
        wf_repo_record_free(&rec);
        wf_agent_session_data_free(&sd);
        wf_agent_free(agent);
        return 1;
    }
    printf("updated list %s\n", list_uri);
    wf_repo_write_record_result_free(&out);
    wf_repo_record_free(&rec);
    wf_agent_session_data_free(&sd);
    wf_agent_free(agent);
    return 0;
}

int cmd_list_delete(int argc, char **argv) {
    const char *list_uri = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
            list_uri = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !list_uri) {
        fprintf(stderr, "error: usage: wolfram list delete <service> <handle> "
                        "<password> --list <list-uri>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s = wf_agent_graph_delete_list(agent, list_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: deleteList failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("deleted list %s\n", list_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_list_add_item(int argc, char **argv) {
    const char *list_uri = NULL, *actor = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
            list_uri = argv[++i];
        else if (strcmp(argv[i], "--actor") == 0 && i + 1 < argc)
            actor = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !list_uri || !actor) {
        fprintf(stderr, "error: usage: wolfram list add-item <service> "
                        "<handle> <password> --list <list-uri> --actor "
                        "<actor>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        free(did);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_graph_create_list_item(agent, list_uri, did, &out);
    free(did);
    if (s != WF_OK) {
        fprintf(stderr, "error: createListItem failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }
    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

int cmd_list_remove_item(int argc, char **argv) {
    const char *item_uri = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--item") == 0 && i + 1 < argc)
            item_uri = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !item_uri) {
        fprintf(stderr, "error: usage: wolfram list remove-item <service> "
                        "<handle> <password> --item <list-item-uri>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s = wf_agent_graph_delete_list_item(agent, item_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: deleteListItem failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("removed list item %s\n", item_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_mute_list(int argc, char **argv) {
    const char *list_uri = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
            list_uri = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !list_uri) {
        fprintf(stderr, "error: usage: wolfram mute-list <service> <handle> "
                        "<password> --list <list-uri>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s = wf_agent_graph_mute_actor_list(agent, list_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: muteActorList failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("muted list %s\n", list_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_unmute_list(int argc, char **argv) {
    const char *list_uri = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
            list_uri = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !list_uri) {
        fprintf(stderr, "error: usage: wolfram unmute-list <service> <handle> "
                        "<password> --list <list-uri>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s = wf_agent_graph_unmute_actor_list(agent, list_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: unmuteActorList failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("unmuted list %s\n", list_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_block_list(int argc, char **argv) {
    const char *list_uri = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
            list_uri = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !list_uri) {
        fprintf(stderr, "error: usage: wolfram block-list <service> <handle> "
                        "<password> --list <list-uri>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_agent_post_result out = {0};
    wf_status s = wf_agent_graph_create_list_block(agent, list_uri, &out);
    if (s != WF_OK) {
        fprintf(stderr, "error: createListBlock failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }
    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

int cmd_unblock_list(int argc, char **argv) {
    const char *list_uri = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
            list_uri = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !list_uri) {
        fprintf(stderr, "error: usage: wolfram unblock-list <service> <handle> "
                        "<password> --list <list-uri>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s = wf_agent_graph_delete_list_block(agent, list_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: deleteListBlock failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("unblocked list %s\n", list_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_mute_thread(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_status s = wf_agent_mute_thread(agent, argv[4]);
    if (s != WF_OK) {
        fprintf(stderr, "error: muteThread failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("muted thread %s\n", argv[4]);
    wf_agent_free(agent);
    return 0;
}

int cmd_unmute_thread(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_status s = wf_agent_unmute_thread(agent, argv[4]);
    if (s != WF_OK) {
        fprintf(stderr, "error: unmuteThread failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("unmuted thread %s\n", argv[4]);
    wf_agent_free(agent);
    return 0;
}

int cmd_get_list_blocks(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_list_blocks(agent, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_get_list_mutes(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_list_mutes(agent, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_get_suggested_follows(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_suggested_follows(agent, actor, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_starter_pack(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "get") == 0) {
        if (argc < 4) {
            usage_stream(stderr);
            return 0;
        }
        const char *service = argv[2];
        const char *uri = argv[3];

        wf_agent *agent = cli_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }

        wf_response res = {0};
        /* Use the agent-level getStarterPack wrapper */
        wf_status s = wf_agent_get_starter_pack(agent, uri, &res);
        return finish_agent_response(agent, s, &res);
    }

    if (strcmp(sub, "get-membership") == 0) {
        if (argc < 4) {
            usage_stream(stderr);
            return 0;
        }
        const char *service = argv[2];
        const char *actor = argv[3];
        int limit = (argc >= 5) ? atoi(argv[4]) : 50;

        wf_agent *agent = cli_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }

        wf_response res = {0};
        wf_status s = wf_agent_get_starter_packs_with_membership(
            agent, actor, limit, NULL, &res);
        return finish_agent_response(agent, s, &res);
    }

    fprintf(stderr,
            "error: unknown starter-pack subcommand '%s' (try get/"
            "get-membership)\n",
            sub);
    return 1;
}
