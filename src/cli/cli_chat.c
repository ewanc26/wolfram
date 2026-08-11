/*
 * cli_chat.c — the `convos` / `messages` / `chat` / `groups` / `reactions`
 * subcommands, split out of main.c as their own self-contained concern.
 */

#include "cli_chat.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/chat_typed.h"
#include "wolfram/moderation.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* wolfram convos <service> <handle> <password> [limit] */
int cmd_convos(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_chat_convo_list list = {0};
    wf_status s = wf_agent_chat_list_convos(agent, limit, NULL, &list);
    if (s != WF_OK) {
        fprintf(stderr, "error: listConvos failed (status %d)\n", (int)s);
        wf_chat_convo_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }
    for (size_t i = 0; i < list.convo_count; ++i) {
        printf("%s\tunread=%d\t%s\n", list.convos[i].id,
               list.convos[i].unread_count,
               list.convos[i].last_message_text
                   ? list.convos[i].last_message_text
                   : "(no last message)");
    }
    wf_chat_convo_list_free(&list);
    wf_agent_free(agent);
    return 0;
}

/* wolfram messages <service> <handle> <password> --convo <convo-id> [limit] */
int cmd_messages(int argc, char **argv) {
    const char *convo_id = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--convo") == 0 && i + 1 < argc)
            convo_id = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3 || !convo_id) {
        fprintf(stderr, "error: usage: wolfram messages <service> <handle> "
                        "<password> --convo <convo-id> [limit]\n");
        return 1;
    }
    int limit = 50;

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_chat_message_list list = {0};
    wf_status s =
        wf_agent_chat_get_messages(agent, convo_id, limit, NULL, &list);
    if (s != WF_OK) {
        fprintf(stderr, "error: getMessages failed (status %d)\n", (int)s);
        wf_chat_message_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }
    for (size_t i = 0; i < list.message_count; ++i) {
        printf("[%s] %s: %s\n", list.messages[i].sent_at,
               list.messages[i].sender, list.messages[i].text);
    }
    wf_chat_message_list_free(&list);
    wf_agent_free(agent);
    return 0;
}

/* wolfram chat send <service> <handle> <password> --convo <convo-id>
 *   --text <text> */
int cmd_chat(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "send") == 0) {
        const char *convo_id = NULL, *text = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--convo") == 0 && i + 1 < argc)
                convo_id = argv[++i];
            else if (strcmp(argv[i], "--text") == 0 && i + 1 < argc)
                text = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !convo_id || !text) {
            fprintf(stderr, "error: usage: wolfram chat send <service> "
                            "<handle> <password> --convo <convo-id> --text "
                            "<text>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_chat_message msg = {0};
        wf_status s =
            wf_agent_chat_send_message(agent, convo_id, text, NULL, &msg);
        if (s != WF_OK) {
            fprintf(stderr, "error: sendMessage failed (status %d)\n", (int)s);
            wf_chat_message_reset(&msg);
            wf_agent_free(agent);
            return 1;
        }
        printf("sent: id=%s text=%s\n", msg.id ? msg.id : "?",
               msg.text ? msg.text : "?");
        wf_chat_message_reset(&msg);
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr, "error: unknown chat subcommand '%s' (try send)\n", sub);
    return 1;
}

/* wolfram groups create <service> <handle> <password> --members <did>...
 *   --name <name>
 * wolfram groups edit <service> <handle> <password> --convo <convo-id>
 *   --name <name>
 * wolfram groups add-members <service> <handle> <password> --convo <convo-id>
 *   --members <did>...
 * wolfram groups remove-members <service> <handle> <password> --convo
 * <convo-id>
 *   --members <did>... */
int cmd_groups(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "create") == 0) {
        const char *name = NULL;
        const char *members[16];
        int n_members = 0;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
                name = argv[++i];
            else if (strcmp(argv[i], "--members") == 0) {
                while (i + 1 < argc && argv[i + 1][0] != '-' && n_members < 16)
                    members[n_members++] = argv[++i];
            } else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !name || n_members == 0) {
            fprintf(stderr, "error: usage: wolfram groups create <service> "
                            "<handle> <password> --members <did>... --name "
                            "<name>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_response res = {0};
        wf_status s =
            wf_agent_chat_create_group(agent, members, n_members, name, &res);
        return finish_agent_response(agent, s, &res);
    }

    if (strcmp(sub, "edit") == 0) {
        const char *convo_id = NULL, *name = NULL;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--convo") == 0 && i + 1 < argc)
                convo_id = argv[++i];
            else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
                name = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !convo_id || !name) {
            fprintf(stderr, "error: usage: wolfram groups edit <service> "
                            "<handle> <password> --convo <convo-id> --name "
                            "<name>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_response res = {0};
        wf_status s = wf_agent_chat_edit_group(agent, convo_id, name, &res);
        return finish_agent_response(agent, s, &res);
        if (s != WF_OK) {
            fprintf(stderr, "error: editGroup failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("group %s renamed to %s\n", convo_id, name);
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "add-members") == 0 || strcmp(sub, "remove-members") == 0) {
        const char *convo_id = NULL;
        const char *members[16];
        int n_members = 0;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--convo") == 0 && i + 1 < argc)
                convo_id = argv[++i];
            else if (strcmp(argv[i], "--members") == 0) {
                while (i + 1 < argc && argv[i + 1][0] != '-' && n_members < 16)
                    members[n_members++] = argv[++i];
            } else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !convo_id || n_members == 0) {
            fprintf(stderr,
                    "error: usage: wolfram groups %s <service> "
                    "<handle> <password> --convo <convo-id> --members "
                    "<did>...\n",
                    sub);
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_response res = {0};
        wf_status s;
        if (strcmp(sub, "add-members") == 0)
            s = wf_agent_chat_add_members(agent, convo_id, members, n_members,
                                          &res);
        else
            s = wf_agent_chat_remove_members(agent, convo_id, members,
                                             n_members, &res);
        return finish_agent_response(agent, s, &res);
        if (s != WF_OK) {
            fprintf(stderr, "error: %s failed (status %d)\n",
                    strcmp(sub, "add-members") == 0 ? "addMembers"
                                                    : "removeMembers",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("%s %d members %s convo %s\n",
               strcmp(sub, "add-members") == 0 ? "added" : "removed", n_members,
               strcmp(sub, "add-members") == 0 ? "to" : "from", convo_id);
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr,
            "error: unknown groups subcommand '%s' (try create/edit/"
            "add-members/remove-members)\n",
            sub);
    return 1;
}

/* wolfram reactions <service> <handle> <password> <add|remove>
 *   --convo <convo-id> --message <msg-id> --value <emoji> */
int cmd_reactions(int argc, char **argv) {
    const char *action = NULL, *convo_id = NULL, *msg_id = NULL, *value = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--convo") == 0 && i + 1 < argc)
            convo_id = argv[++i];
        else if (strcmp(argv[i], "--message") == 0 && i + 1 < argc)
            msg_id = argv[++i];
        else if (strcmp(argv[i], "--value") == 0 && i + 1 < argc)
            value = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
        else if (!action)
            action = argv[i];
    }
    if (pi < 3 || !action || !convo_id || !msg_id || !value) {
        fprintf(stderr, "error: usage: wolfram reactions <service> <handle> "
                        "<password> <add|remove> --convo <convo-id> --message "
                        "<msg-id> --value <emoji>\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s;
    if (strcmp(action, "add") == 0)
        s = wf_agent_chat_add_reaction(agent, convo_id, msg_id, value);
    else if (strcmp(action, "remove") == 0)
        s = wf_agent_chat_remove_reaction(agent, convo_id, msg_id, value);
    else {
        fprintf(stderr,
                "error: unknown reactions action '%s' (try "
                "add/remove)\n",
                action);
        wf_agent_free(agent);
        return 1;
    }

    if (s != WF_OK) {
        fprintf(stderr, "error: %sReaction failed (status %d)\n", action,
                (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("%s reaction %s on message %s\n", action, value, msg_id);
    wf_agent_free(agent);
    return 0;
}
