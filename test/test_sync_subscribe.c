#include "wolfram/sync_subscribe.h"
#include "wolfram/xrpc.h"
#include "test.h"

#include <string.h>
#include <time.h>
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
    if (g_handle_ptr && *g_handle_ptr) wf_subscribe_stop(*g_handle_ptr);
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

static void test_decode_hostile_sizes(void) {
    wf_subscribe_event ev;
    /* hostile declared sizes (valid header + hostile body, and vice versa)
     * must be rejected promptly, before any allocation sized by the count */
    static const unsigned char hdr[] = {0xa1, 0x62, 0x6f, 0x70, 0x01};
    static const unsigned char arr32[] = {0xa1, 0x61, 0x61, 0x9a,
                                          0xff, 0xff, 0xff, 0xff};
    static const unsigned char map64[] = {0xbb, 0xff, 0xff, 0xff, 0xff,
                                          0xff, 0xff, 0xff, 0xff};
    static const unsigned char *const bad[] = {arr32, map64};
    static const size_t bad_len[] = {sizeof(arr32), sizeof(map64)};
    for (size_t k = 0; k < 2; k++) {
        unsigned char frame[32];
        clock_t t0 = clock();
        memcpy(frame, hdr, sizeof(hdr));
        memcpy(frame + sizeof(hdr), bad[k], bad_len[k]);
        WF_CHECK(wf_subscribe_decode_frame(frame, sizeof(hdr) + bad_len[k],
                                           &ev) != WF_OK);
        memcpy(frame, bad[k], bad_len[k]);
        memcpy(frame + bad_len[k], hdr, sizeof(hdr));
        WF_CHECK(wf_subscribe_decode_frame(frame, bad_len[k] + sizeof(hdr),
                                           &ev) != WF_OK);
        WF_CHECK((double)(clock() - t0) / CLOCKS_PER_SEC < 0.5);
    }
    {
        /* 200000-level nested body */
        size_t n = sizeof(hdr) + 200000 + 1;
        unsigned char *frame = malloc(n);
        WF_CHECK(frame != NULL);
        if (frame) {
            clock_t t0 = clock();
            memcpy(frame, hdr, sizeof(hdr));
            memset(frame + sizeof(hdr), 0x81, 200000);
            frame[n - 1] = 0x00;
            WF_CHECK(wf_subscribe_decode_frame(frame, n, &ev) != WF_OK);
            WF_CHECK((double)(clock() - t0) / CLOCKS_PER_SEC < 1.0);
            free(frame);
        }
    }
    wf_subscribe_event_free(&ev);
}

int main(void) {
    test_firehose_connect_and_receive();
    test_decode_hostile_sizes();
    WF_TEST_SUMMARY();
}
