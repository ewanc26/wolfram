/*
 * timeline_thread_typed.c — end-to-end demonstration of the typed high-level
 * agent API.
 *
 * Fetches the authenticated user's timeline with wf_agent_get_timeline_typed,
 * prints each post's author / uri / like count, then walks the conversation
 * around the first post with wf_agent_get_post_thread_typed.
 *
 * Usage:
 *   timeline_thread_typed <service-url> <handle> <password>
 */

#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>

#include "wolfram/agent.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> <password>\n",
                argv[0]);
        return 1;
    }

    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_feed_list feed = {0};
    s = wf_agent_get_timeline_typed(agent, 10, NULL, &feed);
    if (s != WF_OK) {
        fprintf(stderr, "getTimeline failed: %d\n", (int)s);
        wf_agent_feed_list_free(&feed);
        wf_agent_free(agent);
        return 1;
    }

    printf("Timeline (%zu items):\n", feed.item_count);
    for (size_t i = 0; i < feed.item_count; ++i) {
        wf_agent_post_view *post = &feed.items[i].post;
        printf("  [%zu] %s  %s  likes=%d\n",
               i,
               post->author.handle ? post->author.handle : "?",
               post->uri ? post->uri : "?",
               post->like_count);
    }

    if (feed.item_count == 0 || !feed.items[0].post.uri) {
        printf("no posts to expand\n");
        wf_agent_feed_list_free(&feed);
        wf_agent_free(agent);
        return 0;
    }

    const char *first_uri = feed.items[0].post.uri;

    wf_agent_thread thread = {0};
    s = wf_agent_get_post_thread_typed(agent, first_uri, 6, &thread);
    if (s != WF_OK) {
        fprintf(stderr, "getPostThread failed: %d\n", (int)s);
        wf_agent_thread_free(&thread);
        wf_agent_feed_list_free(&feed);
        wf_agent_free(agent);
        return 1;
    }

    printf("Thread for %s:\n", first_uri);
    if (thread.root.kind == WF_AGENT_THREAD_KIND_POST) {
        printf("  root: %s by %s\n",
               thread.root.post.uri ? thread.root.post.uri : "?",
               thread.root.post.author.handle ? thread.root.post.author.handle : "?");
    } else if (thread.root.uri) {
        printf("  root node (not a post): %s\n", thread.root.uri);
    }

    wf_agent_thread_free(&thread);
    wf_agent_feed_list_free(&feed);
    wf_agent_free(agent);
    return 0;
}
