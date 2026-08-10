#include "cli_stream.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/jetstream.h"

#include <cJSON.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile sig_atomic_t g_stream_stop = 0;
static wf_subscribe_handle **g_stream_handle_ptr = NULL;

static void stream_on_sigint(int sig) {
    (void)sig;
    g_stream_stop = 1;
    if (g_stream_handle_ptr && *g_stream_handle_ptr) {
        wf_subscribe_stop(*g_stream_handle_ptr);
    }
}

static void stream_on_event(const wf_subscribe_event *event,
                            void *userdata) {
    (void)userdata;
    switch (event->type) {
    case WF_SUBSCRIBE_EVENT_COMMIT: {
        const wf_subscribe_commit *c = &event->data.commit;
        printf("commit: did=%s seq=%" PRId64 " rev=%s ops=%zu\n",
               c->did, event->seq, c->rev, c->ops_count);
        for (size_t i = 0; i < c->ops_count; ++i) {
            const wf_subscribe_repo_op *op = &c->ops[i];
            printf("  op: action=%s path=%s\n",
                   op->action, op->path ? op->path : "?");
        }
        break;
    }
    case WF_SUBSCRIBE_EVENT_SYNC: {
        const wf_subscribe_sync *s = &event->data.sync;
        printf("sync: seq=%" PRId64 " did=%s\n", event->seq, s->did);
        break;
    }
    case WF_SUBSCRIBE_EVENT_IDENTITY: {
        const wf_subscribe_identity *id = &event->data.identity;
        printf("identity: seq=%" PRId64 " did=%s handle=%s\n",
               event->seq, id->did,
               id->has_handle ? id->handle : "?");
        break;
    }
    case WF_SUBSCRIBE_EVENT_ACCOUNT: {
        const wf_subscribe_account *acc = &event->data.account;
        printf("account: seq=%" PRId64 " did=%s active=%d status=%s\n",
               event->seq, acc->did, acc->active,
               acc->has_status ? acc->status : "?");
        break;
    }
    case WF_SUBSCRIBE_EVENT_INFO: {
        const wf_subscribe_info *info = &event->data.info;
        printf("info: name=%s message=%s\n", info->name,
               info->has_message ? info->message : "(none)");
        break;
    }
    case WF_SUBSCRIBE_EVENT_ERROR: {
        printf("error: %s\n",
               event->data.error.message ? event->data.error.message : "");
        break;
    }
    default:
        break;
    }
    fflush(stdout);
}

static void stream_on_error(wf_status status, const char *msg,
                            void *userdata) {
    (void)userdata;
    fprintf(stderr, "error: stream status=%d %s\n", (int)status,
            msg ? msg : "");
}

int cmd_sync_subscribe(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];

    int64_t cursor = 0;
    int has_cursor = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) {
            cursor = (int64_t)strtoll(argv[++i], NULL, 10);
            has_cursor = 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = stream_on_sigint;
    sigaction(SIGINT, &sa, NULL);

    g_stream_stop = 0;

    wf_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = service;
    opts.cursor = cursor;
    opts.has_cursor = has_cursor;
    opts.on_event = stream_on_event;
    opts.on_error = stream_on_error;

    printf("subscribing to repo stream at %s (cursor=%s)\n", service,
           has_cursor ? "set" : "none");

    wf_subscribe_handle *handle = NULL;
    g_stream_handle_ptr = &handle;
    wf_status s = wf_subscribe_start(&opts, &handle);
    g_stream_handle_ptr = NULL;

    if (s != WF_OK) {
        fprintf(stderr, "error: subscription ended (status %d)\n", (int)s);
        return 1;
    }
    printf("subscription ended cleanly\n");
    return 0;
}

int cmd_firehose(int argc, char **argv) {
    return cmd_sync_subscribe(argc, argv);
}

int cmd_jetstream(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *endpoint = argv[1];

    const char *collections[16];
    int n_collections = 0;
    const char *dids[16];
    int n_dids = 0;
    int64_t cursor = 0;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--collections") == 0) {
            while (i + 1 < argc && argv[i + 1][0] != '-' &&
                   n_collections < 16)
                collections[n_collections++] = argv[++i];
        } else if (strcmp(argv[i], "--dids") == 0) {
            while (i + 1 < argc && argv[i + 1][0] != '-' && n_dids < 16)
                dids[n_dids++] = argv[++i];
        } else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) {
            cursor = (int64_t)strtoll(argv[++i], NULL, 10);
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = stream_on_sigint;
    sigaction(SIGINT, &sa, NULL);

    g_stream_stop = 0;

    wf_jetstream_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.endpoint = endpoint;
    opts.wanted_collections = collections;
    opts.wanted_collections_count = n_collections;
    opts.wanted_dids = dids;
    opts.wanted_dids_count = n_dids;
    opts.cursor = cursor;

    printf("connecting to jetstream at %s (collections=%d, dids=%d, "
           "cursor=%" PRId64 ")\n",
           endpoint, n_collections, n_dids, cursor);

    wf_jetstream *stream = NULL;
    wf_status s = wf_jetstream_connect(&opts, &stream);
    if (s != WF_OK) {
        fprintf(stderr, "error: jetstream connect failed (status %d)\n",
                (int)s);
        return 1;
    }

    int count = 0;
    while (!g_stream_stop) {
        wf_jetstream_event event = {0};
        s = wf_jetstream_next(stream, &event);
        if (s != WF_OK) {
            if (g_stream_stop) break;
            fprintf(stderr, "error: jetstream next failed (status %d)\n",
                    (int)s);
            break;
        }
        count++;
        printf("event: kind=%d did=%s\n",
               (int)event.kind, event.did ? event.did : "?");
        if (event.json)
            printf("  %s\n", event.json);
        wf_jetstream_event_free(&event);
        fflush(stdout);
    }

    wf_jetstream_free(stream);
    printf("jetstream ended (%d events)\n", count);
    return 0;
}
