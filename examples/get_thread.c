/*
 * get_thread.c — fetch and print a post thread (app.bsky.feed.getPostThread).
 *
 * Demonstrates:
 *   1. High-level agent login with wf_agent
 *   2. Fetching a post thread via the typed wrapper wf_agent_get_post_thread_typed
 *   3. Walking the owned recursive thread tree (parent + replies) and printing
 *      each post's text, author, and engagement counts.
 *
 * Usage:
 *   get_thread <service-url> <handle> <password> <post-at-uri> [depth]
 *
 * The <post-at-uri> is an at:// URI such as
 *   at://did:plc:abc/app.bsky.feed.post/xyz
 * and [depth] is the thread depth to fetch (defaults to 6).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"
#include "wolfram/thread_typed.h"

/* Indent by 2 spaces per level so the tree reads top-down. */
static void print_node(const wf_agent_thread_node *node, int indent) {
    if (!node) {
        return;
    }

    for (int i = 0; i < indent; ++i) {
        printf("  ");
    }

    switch (node->kind) {
    case WF_AGENT_THREAD_KIND_POST: {
        const wf_agent_thread_post *p = &node->post;
        const char *author = p->author.handle ? p->author.handle
                            : (p->author.did ? p->author.did : "?");
        const char *text = "?";
        if (p->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(p->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("post: %s\n", p->uri ? p->uri : "?");
        for (int i = 0; i < indent; ++i) {
            printf("  ");
        }
        printf("  by %s  replies=%d reposts=%d likes=%d quotes=%d\n",
               author, p->reply_count, p->repost_count, p->like_count,
               p->quote_count);
        for (int i = 0; i < indent; ++i) {
            printf("  ");
        }
        printf("  \"%.100s\"\n", text);
        break;
    }

    case WF_AGENT_THREAD_KIND_NOT_FOUND:
        printf("notFound: %s\n", node->uri ? node->uri : "?");
        break;

    case WF_AGENT_THREAD_KIND_BLOCKED:
        printf("blocked: %s\n", node->uri ? node->uri : "?");
        break;
    }

    for (size_t i = 0; i < node->replies_count; ++i) {
        print_node(&node->replies[i], indent + 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> <password> <post-at-uri> [depth]\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *post_uri    = argv[4];
    int depth = (argc > 5) ? (int)strtol(argv[5], NULL, 10) : 6;
    if (depth <= 0) {
        depth = 6;
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

    wf_agent_thread thread = {0};
    status = wf_agent_get_post_thread_typed(agent, post_uri, depth, &thread);
    if (status != WF_OK) {
        fprintf(stderr, "getPostThread failed: %d\n", (int)status);
        wf_agent_thread_free(&thread);
        wf_agent_free(agent);
        return 1;
    }

    printf("Thread for %s (depth=%d):\n", post_uri, depth);

    /* The parent chain (if any) runs upward from the root; print it first as
     * context, then the root and its reply subtree. */
    if (thread.root.parent) {
        print_node(thread.root.parent, 0);
    }
    print_node(&thread.root, 0);

    if (thread.cursor) {
        printf("cursor: %s\n", thread.cursor);
    }

    wf_agent_thread_free(&thread);
    wf_agent_free(agent);
    return 0;
}
