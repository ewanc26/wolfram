/*
 * timeline_paged.c — demonstrate cursor-based pagination with the agent API.
 *
 * Fetches the authenticated user's timeline across N pages using
 * wf_agent_get_timeline_paged, printing the author/handle and like count of
 * each post and the page boundaries.
 *
 * Usage:
 *   timeline_paged <service-url> <handle> <password> [max-pages]
 *
 * With no arguments the program prints usage and exits 0, so it can be built
 * and run offline (e.g. in CI) without performing any network I/O.
 */

#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>

#include "wolfram/agent.h"

typedef struct {
    int page;
    int printed;
} page_ctx;

static wf_status on_page(wf_agent *agent, const wf_agent_feed_list *feed,
                         const char *cursor, void *ud) {
    (void)agent;
    page_ctx *ctx = (page_ctx *)ud;
    ctx->page++;

    printf("--- page %d (requested cursor: %s) : %zu items ---\n",
           ctx->page, cursor ? cursor : "(start)", feed->item_count);

    for (size_t i = 0; i < feed->item_count; ++i) {
        const wf_agent_post_view *post = &feed->items[i].post;
        printf("  [%d] %s  %s  likes=%d\n",
               ctx->printed + (int)i,
               post->author.handle ? post->author.handle : "?",
               post->uri ? post->uri : "?",
               post->like_count);
    }
    ctx->printed += (int)feed->item_count;

    /* Returning WF_OK continues pagination; return non-WF_OK to stop early. */
    return WF_OK;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> <password> [max-pages]\n",
                argv[0]);
        return 0;
    }

    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int max_pages = (argc >= 5) ? atoi(argv[4]) : 0; /* 0 = until exhausted */

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

    page_ctx ctx = {0};
    char *last_cursor = NULL;
    s = wf_agent_get_timeline_paged(agent, 10, max_pages, on_page, &ctx,
                                    &last_cursor);
    if (s != WF_OK) {
        fprintf(stderr, "timeline pagination failed: %d\n", (int)s);
        free(last_cursor);
        wf_agent_free(agent);
        return 1;
    }

    printf("fetched %d posts across %d page(s); final cursor: %s\n",
           ctx.printed, ctx.page, last_cursor ? last_cursor : "(exhausted)");
    free(last_cursor);

    wf_agent_free(agent);
    return 0;
}
