#include "wolfram/label.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static wf_label_subscribe_handle **g_handle_ptr = NULL;
static int g_label_count = 0;
static int g_neg_count = 0;
static int g_info_count = 0;
static int g_errored = 0;

static void on_label(const wf_label *label, void *userdata) {
    (void)userdata;
    (void)label;
    g_label_count++;
    if (g_handle_ptr && *g_handle_ptr && g_label_count + g_neg_count >= 4) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

static void on_neg(const wf_label *label, void *userdata) {
    (void)userdata;
    (void)label;
    g_neg_count++;
    if (g_handle_ptr && *g_handle_ptr && g_label_count + g_neg_count >= 4) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

static void on_info(const wf_label_info *info, void *userdata) {
    (void)userdata;
    g_info_count++;
    if (info && info->name && strcmp(info->name, "OutdatedCursor") == 0 &&
        g_handle_ptr && *g_handle_ptr) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

static void on_error(wf_status status, const char *msg, void *userdata) {
    (void)status;
    (void)msg;
    (void)userdata;
    g_errored = 1;
    if (g_handle_ptr && *g_handle_ptr) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

static void test_build_url(void) {
    char *url = NULL;
    WF_CHECK(wf_label_build_url("https://example.com", 0, &url) == WF_OK);
    WF_CHECK(strcmp(url, "wss://example.com/xrpc/com.atproto.label.subscribeLabels") == 0);
    free(url);

    WF_CHECK(wf_label_build_url("wss://example.com/xrpc/com.atproto.label.subscribeLabels",
                                42, &url) == WF_OK);
    WF_CHECK(strcmp(url,
                    "wss://example.com/xrpc/com.atproto.label.subscribeLabels?cursor=42") == 0);
    free(url);

    WF_CHECK(wf_label_build_url("https://example.com/path/",
                                0, &url) == WF_OK);
    WF_CHECK(strcmp(url, "wss://example.com/path/xrpc/com.atproto.label.subscribeLabels") == 0);
    free(url);
}

static void test_message_parse(void) {
    const char *labels_json =
        "{"
        "\"$type\":\"#labels\"," 
        "\"seq\":17,"
        "\"labels\":["
        "{\"src\":\"did:plc:z72i7hdynmk6r22z27h6tvur\","
        "\"uri\":\"at://did:plc:z72i7hdynmk6r22z27h6tvur/app.bsky.feed.post/3jui7kd54zh2y\","
        "\"val\":\"nsfw\","
        "\"cts\":\"2024-10-16T00:00:00Z\","
        "\"neg\":false},"
        "{\"src\":\"did:plc:z72i7hdynmk6r22z27h6tvur\","
        "\"uri\":\"at://did:plc:z72i7hdynmk6r22z27h6tvur/app.bsky.feed.post/3jui7kd54zh2y\","
        "\"val\":\"hide\","
        "\"cts\":\"2024-10-16T00:00:00Z\","
        "\"neg\":true}"
        "]}"
        ;
    wf_label_message message = {0};
    WF_CHECK(wf_label_message_parse(labels_json, strlen(labels_json), &message) == WF_OK);
    WF_CHECK(message.type == WF_LABEL_MESSAGE_LABELS);
    WF_CHECK(message.data.labels.seq == 17);
    WF_CHECK(message.data.labels.count == 2);
    WF_CHECK(message.data.labels.items[0].seq == 17);
    WF_CHECK(strcmp(message.data.labels.items[0].src,
                    "did:plc:z72i7hdynmk6r22z27h6tvur") == 0);
    WF_CHECK(strcmp(message.data.labels.items[0].val, "nsfw") == 0);
    WF_CHECK(message.data.labels.items[0].neg == 0);
    WF_CHECK(message.data.labels.items[1].neg == 1);
    wf_label_message_free(&message);

    const char *info_json =
        "{"
        "\"$type\":\"#info\"," 
        "\"name\":\"OutdatedCursor\","
        "\"message\":\"cursor too old\""
        "}"
        ;
    WF_CHECK(wf_label_message_parse(info_json, strlen(info_json), &message) == WF_OK);
    WF_CHECK(message.type == WF_LABEL_MESSAGE_INFO);
    WF_CHECK(strcmp(message.data.info.name, "OutdatedCursor") == 0);
    WF_CHECK(message.data.info.has_message);
    WF_CHECK(strcmp(message.data.info.message, "cursor too old") == 0);
    wf_label_message_free(&message);

    WF_CHECK(wf_label_message_parse(NULL, 0, &message) == WF_ERR_INVALID_ARG);
}

static void test_label_subscribe_smoke(void) {
    const char *service = getenv("WF_LABEL_SUBSCRIBE_SERVICE");
    if (!service || !service[0]) {
        service = "https://mod.bsky.app";
    }

    g_label_count = 0;
    g_neg_count = 0;
    g_info_count = 0;
    g_errored = 0;

    wf_label_subscribe_handle *handle = NULL;
    g_handle_ptr = &handle;

    wf_label_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = service;
    opts.cursor = 0;
    opts.has_cursor = 0;
    opts.reconnect_delay_ms = 100;
    opts.on_label = on_label;
    opts.on_neg = on_neg;
    opts.on_info = on_info;
    opts.on_error = on_error;
    opts.userdata = NULL;

    wf_status status = wf_label_subscribe_start(&opts, &handle);
    g_handle_ptr = NULL;

    if (status != WF_OK || g_errored) {
        return;
    }

    WF_CHECK(g_label_count + g_neg_count > 0);
    WF_CHECK(g_info_count >= 0);
}

int main(void) {
    test_build_url();
    test_message_parse();
    test_label_subscribe_smoke();
    WF_TEST_SUMMARY();
}
