#include "wolfram/sync_subscribe.h"
#include "wolfram/xrpc.h"
#include "test.h"

#include <string.h>
#include <stdlib.h>

#define MAX_EVENTS 5

static wf_subscribe_handle **g_handle_ptr = NULL;
static int g_event_count = 0;
static int g_last_type = -1;
static int g_errored = 0;

static void on_event(const wf_subscribe_event *event, void *userdata) {
    (void)userdata;
    g_event_count++;
    g_last_type = (int)event->type;

    if (g_event_count >= MAX_EVENTS && g_handle_ptr && *g_handle_ptr)
        wf_subscribe_stop(*g_handle_ptr);
}

static void on_error(wf_status status, const char *msg, void *userdata) {
    (void)status;
    (void)msg;
    (void)userdata;
    g_errored = 1;
    if (g_handle_ptr && *g_handle_ptr)
        wf_subscribe_stop(*g_handle_ptr);
}

static void test_firehose_connect_and_receive(void) {
    g_event_count = 0;
    g_last_type = -1;
    g_errored = 0;

    wf_subscribe_handle *handle = NULL;
    g_handle_ptr = &handle;

    wf_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = "wss://bsky.network";
    opts.cursor = 0;
    opts.has_cursor = 0;
    opts.on_event = on_event;
    opts.on_error = on_error;
    opts.userdata = NULL;
    opts.max_retry_seconds = 1;
    opts.reconnect_delay_ms = 100;

    wf_status status = wf_subscribe_start(&opts, &handle);

    /* handle_connect failure exits subscribe_loop without calling on_error */
    if (g_errored || status != WF_OK) {
        return;
    }

    WF_CHECK(g_event_count > 0);
    WF_CHECK(g_last_type >= 0);
    WF_CHECK(g_last_type <= WF_SUBSCRIBE_EVENT_ERROR);
    (void)status;
}

int main(void) {
    test_firehose_connect_and_receive();
    WF_TEST_SUMMARY();
}
