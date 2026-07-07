/*
 * test_examples_live.c — gated LIVE end-to-end example test.
 *
 * This is a GATED live integration test. It only performs real network I/O
 * against a real PDS when BSKY_HANDLE and BSKY_PASSWORD environment variables
 * are present. When they are absent (the normal CI/offline case) it prints a
 * single SKIP line and returns 0, so ctest passes without network access.
 *
 * The live flow mirrors exactly what the example programs (get_thread,
 * list_notifications, list_convos, ozone_moderation) do: log in with the
 * high-level agent API, post unique text, fetch the thread, list
 * notifications, and exercise chat service resolution.
 */

#include "wolfram/agent.h"
#include "wolfram/thread_typed.h"
#include "wolfram/chat_typed.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    const char *handle = getenv("BSKY_HANDLE");
    const char *password = getenv("BSKY_PASSWORD");
    const char *service = getenv("BSKY_SERVICE");

    if (!service || service[0] == '\0') {
        service = "https://bsky.social";
    }

    /* Gated skip: no credentials -> pass without network. */
    if (!handle || handle[0] == '\0' || !password || password[0] == '\0') {
        printf("SKIP: set BSKY_HANDLE and BSKY_PASSWORD to run live example test\n");
        return 0;
    }

    wf_status st;
    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "FAIL: wf_agent_new returned NULL\n");
        return 1;
    }

    st = wf_agent_login(agent, handle, password);
    if (st != WF_OK) {
        fprintf(stderr, "FAIL: wf_agent_login status=%d\n", (int)st);
        wf_agent_free(agent);
        return 1;
    }
    printf("live: logged in as %s\n", handle);

    /* Post a unique text carrying the unix time. */
    char text[128];
    snprintf(text, sizeof(text), "hello from wolfram live test %ld",
             (long)time(NULL));

    wf_agent_post_result post_result;
    memset(&post_result, 0, sizeof(post_result));
    st = wf_agent_post(agent, text, &post_result);
    if (st != WF_OK || !post_result.uri) {
        fprintf(stderr, "FAIL: wf_agent_post status=%d uri=%s\n", (int)st,
                post_result.uri ? post_result.uri : "(null)");
        wf_agent_post_result_free(&post_result);
        wf_agent_free(agent);
        return 1;
    }
    printf("live: posted %s\n", post_result.uri);

    /* Fetch the thread for the post we just made. */
    wf_agent_thread thread;
    memset(&thread, 0, sizeof(thread));
    st = wf_agent_get_post_thread_typed(agent, post_result.uri, 1, &thread);
    if (st != WF_OK || thread.root.post.uri == NULL) {
        fprintf(stderr, "FAIL: wf_agent_get_post_thread_typed status=%d\n",
                (int)st);
        wf_agent_thread_free(&thread);
        wf_agent_post_result_free(&post_result);
        wf_agent_free(agent);
        return 1;
    }
    printf("live: thread post uri=%s\n",
           thread.root.post.uri ? thread.root.post.uri : "(null)");
    wf_agent_thread_free(&thread);

    /* List notifications. */
    wf_agent_notification_list notifs;
    memset(&notifs, 0, sizeof(notifs));
    st = wf_agent_list_notifications_typed(agent, 10, NULL, &notifs);
    if (st != WF_OK) {
        fprintf(stderr, "FAIL: wf_agent_list_notifications_typed status=%d\n",
                (int)st);
        wf_agent_notification_list_free(&notifs);
        wf_agent_post_result_free(&post_result);
        wf_agent_free(agent);
        return 1;
    }
    printf("live: notifications count=%zu\n", notifs.notification_count);
    wf_agent_notification_list_free(&notifs);

    /* Exercise chat service resolution live. Errors here are acceptable
     * (e.g. no chat service advertised) but we still try and report. */
    wf_chat_convo_list convos;
    memset(&convos, 0, sizeof(convos));
    st = wf_agent_chat_list_convos(agent, 10, NULL, &convos);
    if (st == WF_OK) {
        printf("live: chat convos count=%zu\n", convos.convo_count);
    } else {
        printf("live: chat listConvos returned status=%d (acceptable)\n",
               (int)st);
    }
    wf_chat_convo_list_free(&convos);

    wf_agent_post_result_free(&post_result);
    wf_agent_free(agent);

    printf("live example test OK\n");
    return 0;
}
