/*
 * list_convos.c — list the authenticated account's chat conversations
 * (chat.bsky.convo.listConvos).
 *
 * Demonstrates:
 *   1. High-level agent login with wf_agent
 *   2. Listing chat conversations via the typed wrapper
 *      wf_agent_chat_list_convos
 *   3. Printing each conversation's id, members, last message, and unread count.
 *
 * IMPORTANT — chat service endpoint:
 *   chat.bsky.convo.* endpoints are served by a SEPARATE Bluesky chat service
 *   (e.g. https://chat.bsky.app), NOT the user's main PDS. The agent's XRPC
 *   client (agent->client) is created against the PDS URL passed to
 *   wf_agent_new, so before calling the chat wrappers in production the agent
 *   must be repointed at the chat service.
 *
 *   This build does not yet expose `wf_agent_chat_service_resolve` (the
 *   intended helper that discovers the chat service URL from the user's
 *   preferences / the bsky chat configuration and repoints the agent's chat
 *   client). Until that lands, point the agent at the chat service by passing
 *   the chat service URL as <service-url> OR add the call below once the
 *   symbol is available:
 *
 *       status = wf_agent_chat_service_resolve(agent);
 *       if (status != WF_OK) { ... }
 *
 *   See the NOTE ON SERVICE ENDPOINT in include/wolfram/chat_typed.h.
 *
 * Usage:
 *   list_convos <service-url> <handle> <password> [limit]
 *
 * [limit] is the maximum number of conversations to fetch (defaults to 50).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"
#include "wolfram/chat_typed.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> <password> [limit]\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    int limit = (argc > 4) ? (int)strtol(argv[4], NULL, 10) : 50;
    if (limit <= 0) {
        limit = 50;
    }

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
    printf("Logged in as %s\n", handle ? handle : identifier);

    /* Resolve the separate chat service endpoint before issuing chat calls.
     * See the IMPORTANT note in this file's header — the symbol
     * wf_agent_chat_service_resolve is not yet exposed in this build, so this
     * is documented rather than called. When it becomes available, uncomment:
     *
     *   status = wf_agent_chat_service_resolve(agent);
     *   if (status != WF_OK) {
     *       fprintf(stderr, "chat service resolve failed: %d\n", (int)status);
     *       wf_agent_free(agent);
     *       return 1;
     *   }
     */

    wf_chat_convo_list list = {0};
    status = wf_agent_chat_list_convos(agent, limit, NULL, &list);
    if (status != WF_OK) {
        fprintf(stderr, "listConvos failed: %d\n", (int)status);
        wf_chat_convo_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }

    printf("Conversations (%zu):\n", list.convo_count);
    for (size_t i = 0; i < list.convo_count; ++i) {
        const wf_chat_convo *c = &list.convos[i];
        printf("  [%zu] id=%s rev=%s type=%s unread=%d\n", i,
               c->id ? c->id : "?",
               c->rev ? c->rev : "?",
               c->type ? c->type : "?",
               c->unread_count);
        printf("        members (%zu):", c->member_count);
        for (size_t m = 0; m < c->member_count; ++m) {
            const wf_agent_profile_view *p = &c->members[m];
            printf(" %s", p->handle ? p->handle : (p->did ? p->did : "?"));
        }
        printf("\n");
        if (c->last_message_text) {
            printf("        last: \"%.120s\"\n", c->last_message_text);
        }
    }

    if (list.cursor) {
        printf("cursor: %s\n", list.cursor);
    }

    wf_chat_convo_list_free(&list);
    wf_agent_free(agent);
    return 0;
}
