/*
 * subscribe_labels.c — subscribe to a labeler's label stream.
 *
 * Demonstrates wf_label_subscribe_start with simple callbacks that print
 * each label and info message as it arrives. The subscription blocks until a
 * stop condition (a max number of labels, an OutdatedCursor info message, or
 * an unrecoverable error) is reached.
 *
 * Usage:
 *   subscribe_labels [service-url] [cursor]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/label.h"

#define MAX_LABELS 10

static int g_count = 0;
static wf_label_subscribe_handle **g_handle_ptr = NULL;

static void on_label(const wf_label *label, void *userdata) {
    (void)userdata;
    g_count++;
    printf("label: seq=%lld src=%s uri=%s val=%s neg=%d\n",
           (long long)label->seq,
           label->src ? label->src : "?",
           label->uri ? label->uri : "?",
           label->val ? label->val : "?",
           label->neg);
    if (g_handle_ptr && *g_handle_ptr && g_count >= MAX_LABELS) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

static void on_neg(const wf_label *label, void *userdata) {
    (void)userdata;
    g_count++;
    printf("neg label: seq=%lld src=%s uri=%s val=%s\n",
           (long long)label->seq,
           label->src ? label->src : "?",
           label->uri ? label->uri : "?",
           label->val ? label->val : "?");
    if (g_handle_ptr && *g_handle_ptr && g_count >= MAX_LABELS) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

static void on_info(const wf_label_info *info, void *userdata) {
    (void)userdata;
    printf("info: name=%s message=%s\n",
           info->name ? info->name : "?",
           info->message ? info->message : "(none)");
    if (g_handle_ptr && *g_handle_ptr && info->name &&
        strcmp(info->name, "OutdatedCursor") == 0) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

static void on_error(wf_status status, const char *msg, void *userdata) {
    (void)userdata;
    fprintf(stderr, "error: status=%d %s\n", (int)status, msg ? msg : "");
    if (g_handle_ptr && *g_handle_ptr) {
        wf_label_subscribe_stop(*g_handle_ptr);
    }
}

int main(int argc, char **argv) {
    const char *service = argc > 1 && argv[1][0] ? argv[1] : "https://mod.bsky.app";
    int64_t cursor = 0;
    int has_cursor = 0;
    if (argc > 2) {
        cursor = (int64_t)strtoll(argv[2], NULL, 10);
        has_cursor = 1;
    }

    wf_label_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = service;
    opts.cursor = cursor;
    opts.has_cursor = has_cursor;
    opts.reconnect_delay_ms = 1000;
    opts.on_label = on_label;
    opts.on_neg = on_neg;
    opts.on_info = on_info;
    opts.on_error = on_error;

    printf("Subscribing to %s (cursor=%s)\n", service,
           has_cursor ? argv[2] : "none");

    wf_label_subscribe_handle *handle = NULL;
    g_handle_ptr = &handle;
    wf_status status = wf_label_subscribe_start(&opts, &handle);
    g_handle_ptr = NULL;

    if (status != WF_OK) {
        fprintf(stderr, "subscription ended with status %d\n", (int)status);
        return 1;
    }
    printf("subscription closed cleanly\n");
    return 0;
}
