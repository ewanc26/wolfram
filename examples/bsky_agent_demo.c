/*
 * bsky_agent_demo.c — minimal wf_bsky_agent usage example.
 *
 * Logs in (reading credentials from BSKY_HANDLE / BSKY_PASSWORD and an
 * optional BSKY_SERVICE, defaulting to https://bsky.social) and posts a short
 * message. Demonstrates the high-level wrapper's convenience relative to the
 * raw wf_agent / wf_session flow.
 *
 * Build with the rest of the examples; run with:
 *   BSKY_HANDLE=you.bsky.social BSKY_PASSWORD=... ./bsky_agent_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/bsky_agent.h"

int main(void) {
    const char *handle = getenv("BSKY_HANDLE");
    const char *password = getenv("BSKY_PASSWORD");
    const char *service = getenv("BSKY_SERVICE");
    if (!service) {
        service = "https://bsky.social";
    }

    if (!handle || !password) {
        fprintf(stderr,
                "set BSKY_HANDLE and BSKY_PASSWORD (optionally BSKY_SERVICE)\n");
        return 1;
    }

    wf_bsky_agent agent;
    wf_bsky_agent_init(&agent);

    wf_status status = wf_bsky_agent_login(&agent, service, handle, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_bsky_agent_free(&agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    status = wf_bsky_agent_post(&agent, "hello from wolfram's wf_bsky_agent", &out);
    if (status == WF_OK) {
        printf("posted: uri=%s cid=%s\n", out.uri ? out.uri : "(null)",
               out.cid ? out.cid : "(null)");
        wf_agent_post_result_free(&out);
    } else {
        fprintf(stderr, "post failed: %d\n", (int)status);
    }

    wf_bsky_agent_free(&agent);
    return status == WF_OK ? 0 : 1;
}
