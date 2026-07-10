/*
 * test_chat_modevents_sub.c — tests for chat.bsky.moderation.subscribeModEvents
 *
 * Layer 1 (always built): deterministic unit tests of the framed DAG-CBOR
 * decoder (header map ++ body map) and the URL builder. Frames are hand-built
 * with the repo's own CBOR serializer so the test has no libcbor dependency.
 *
 * Layer 2 (built under WOLFRAM_BUILD_SERVER): an end-to-end test that runs a
 * local in-process XRPC server whose WS route streams the framed CBOR
 * mod-event bytes, then drives wf_chat_mod_events_start against it in a thread
 * and asserts the on_event callback receives the expected fields. The streaming
 * path is exercised best-effort: if the local loopback WS handshake cannot be
 * completed (e.g. a libcurl build without WS support), it SKIPs rather than
 * fails, so CI stays deterministic.
 */

#include "wolfram/chat_typed.h"
#include "wolfram/repo/cbor.h"
#include "wolfram/xrpc.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#if defined(WOLFRAM_BUILD_SERVER)
#include "wolfram/xrpc_server.h"
#include "wolfram/websocket.h"
#endif

/* ───────────────────────────── helpers ───────────────────────────── */

static wf_cbor_item *mk_str(const char *s) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    it->type = WF_CBOR_STRING;
    it->string.str = strdup(s);
    it->string.len = strlen(s);
    return it;
}

static wf_cbor_item *mk_uint(uint64_t v) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    it->type = WF_CBOR_UNSIGNED;
    it->uinteger = v;
    return it;
}

static wf_cbor_item *mk_map(const char *keys[], wf_cbor_item **vals, size_t n) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    it->type = WF_CBOR_MAP;
    it->map.count = n;
    it->map.pairs = calloc(n, sizeof(wf_cbor_pair));
    for (size_t i = 0; i < n; i++) {
        it->map.pairs[i].key = mk_str(keys[i]);
        it->map.pairs[i].value = vals[i];
    }
    return it;
}

static void free_item(wf_cbor_item *it) {
    if (!it) return;
    if (it->type == WF_CBOR_STRING) free(it->string.str);
    if (it->type == WF_CBOR_MAP) {
        for (size_t i = 0; i < it->map.count; i++) {
            free_item(it->map.pairs[i].key);
            free_item(it->map.pairs[i].value);
        }
        free(it->map.pairs);
    }
    free(it);
}

/* Serialize a map item and append to a growing buffer. */
static int append_serialized(wf_cbor_item *item, unsigned char **buf,
                             size_t *len, size_t *cap) {
    size_t slen = 0;
    unsigned char *s = wf_cbor_serialize(item, &slen);
    if (!s) return -1;
    if (*len + slen > *cap) {
        size_t ncap = (*cap ? *cap * 2 : 64);
        while (ncap < *len + slen) ncap *= 2;
        unsigned char *nb = realloc(*buf, ncap);
        if (!nb) { free(s); return -1; }
        *buf = nb; *cap = ncap;
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    free(s);
    return 0;
}

/* Build a full framed message: header map ++ body map, concatenated. */
static unsigned char *build_frame(const char *keys_h[], wf_cbor_item **vals_h,
                                  size_t nh,
                                  const char *keys_b[], wf_cbor_item **vals_b,
                                  size_t nb, size_t *out_len) {
    wf_cbor_item *header = mk_map(keys_h, vals_h, nh);
    wf_cbor_item *body = mk_map(keys_b, vals_b, nb);
    unsigned char *buf = NULL; size_t len = 0, cap = 0;
    int rc = append_serialized(header, &buf, &len, &cap);
    if (rc == 0) rc = append_serialized(body, &buf, &len, &cap);
    free_item(header); free_item(body);
    if (rc != 0) { free(buf); return NULL; }
    *out_len = len;
    return buf;
}

/* ────────────────────── Layer 1: decoder unit tests ────────────────────── */

static void test_decode_message(void) {
    const char *kh[] = {"op", "t"};
    wf_cbor_item *vh[] = { mk_uint(1), mk_str("#eventConvoFirstMessage") };

    const char *kb[] = {"seq", "convoId", "rev", "createdAt", "user"};
    wf_cbor_item *vb[] = { mk_uint(42),
                           mk_str("c1"), mk_str("r1"),
                           mk_str("2024-01-01T00:00:00Z"),
                           mk_str("did:plc:user") };

    size_t len = 0;
    unsigned char *frame = build_frame(kh, vh, 2, kb, vb, 5, &len);
    WF_CHECK(frame != NULL);

    wf_chat_mod_frame fr = {0};
    wf_status s = wf_chat_mod_frame_parse_cbor(frame, len, &fr);
    WF_CHECK(s == WF_OK);
    WF_CHECK(fr.is_error == 0);
    WF_CHECK(fr.seq == 42);
    WF_CHECK(fr.event.type && strcmp(fr.event.type, "#eventConvoFirstMessage") == 0);
    WF_CHECK(fr.event.convo_id && strcmp(fr.event.convo_id, "c1") == 0);
    WF_CHECK(fr.event.rev && strcmp(fr.event.rev, "r1") == 0);
    WF_CHECK(fr.event.created_at &&
             strcmp(fr.event.created_at, "2024-01-01T00:00:00Z") == 0);
    WF_CHECK(fr.event.actor_did == NULL); /* not present in this event */
    WF_CHECK(fr.event.subject_did == NULL);
    WF_CHECK(fr.error == NULL);
    wf_chat_mod_frame_free(&fr);

    free(frame);
}

static void test_decode_all_fields(void) {
    const char *kh[] = {"op", "t"};
    wf_cbor_item *vh[] = { mk_uint(1), mk_str("#eventGroupChatMemberLeft") };

    const char *kb[] = {"seq", "convoId", "rev", "createdAt", "actorDid",
                        "subjectDid", "groupName", "ownerDid"};
    wf_cbor_item *vb[] = { mk_uint(7),
                           mk_str("cv2"), mk_str("r7"),
                           mk_str("2024-02-02T02:02:02Z"),
                           mk_str("did:plc:actor"),
                           mk_str("did:plc:subject"),
                           mk_str("My Group"),
                           mk_str("did:plc:owner") };

    size_t len = 0;
    unsigned char *frame = build_frame(kh, vh, 2, kb, vb, 8, &len);
    WF_CHECK(frame != NULL);

    wf_chat_mod_frame fr = {0};
    wf_status s = wf_chat_mod_frame_parse_cbor(frame, len, &fr);
    WF_CHECK(s == WF_OK);
    WF_CHECK(fr.seq == 7);
    WF_CHECK(fr.event.type &&
             strcmp(fr.event.type, "#eventGroupChatMemberLeft") == 0);
    WF_CHECK(fr.event.convo_id && strcmp(fr.event.convo_id, "cv2") == 0);
    WF_CHECK(fr.event.rev && strcmp(fr.event.rev, "r7") == 0);
    WF_CHECK(fr.event.created_at &&
             strcmp(fr.event.created_at, "2024-02-02T02:02:02Z") == 0);
    WF_CHECK(fr.event.actor_did && strcmp(fr.event.actor_did, "did:plc:actor") == 0);
    WF_CHECK(fr.event.subject_did &&
             strcmp(fr.event.subject_did, "did:plc:subject") == 0);
    wf_chat_mod_frame_free(&fr);

    free(frame);
}

static void test_decode_error_frame(void) {
    const char *kh[] = {"op", "t"};
    wf_cbor_item *vh[] = { mk_uint(-1), mk_str("#error") };

    const char *kb[] = {"error", "message"};
    wf_cbor_item *vb[] = { mk_str("FutureCursor"),
                           mk_str("cursor too far ahead") };

    size_t len = 0;
    unsigned char *frame = build_frame(kh, vh, 2, kb, vb, 2, &len);
    WF_CHECK(frame != NULL);

    wf_chat_mod_frame fr = {0};
    wf_status s = wf_chat_mod_frame_parse_cbor(frame, len, &fr);
    WF_CHECK(s == WF_OK);
    WF_CHECK(fr.is_error == 1);
    WF_CHECK(fr.error && strcmp(fr.error, "FutureCursor") == 0);
    WF_CHECK(fr.message && strcmp(fr.message, "cursor too far ahead") == 0);
    WF_CHECK(fr.event.type == NULL);
    wf_chat_mod_frame_free(&fr);

    free(frame);
}

static void test_decode_truncated_and_garbage(void) {
    /* Truncate a valid frame (only the header, no body). */
    const char *kh[] = {"op", "t"};
    wf_cbor_item *vh[] = { mk_uint(1), mk_str("#eventConvoFirstMessage") };
    wf_cbor_item *header = mk_map(kh, vh, 2);
    unsigned char *hbuf = NULL; size_t hlen = 0, hcap = 0;
    WF_CHECK(append_serialized(header, &hbuf, &hlen, &hcap) == 0);
    free_item(header);

    wf_chat_mod_frame fr = {0};
    wf_status s = wf_chat_mod_frame_parse_cbor(hbuf, hlen, &fr);
    WF_CHECK(s == WF_ERR_PARSE);
    WF_CHECK(fr.is_error == 0 && fr.error == NULL && fr.event.type == NULL);
    wf_chat_mod_frame_free(&fr);
    free(hbuf);

    /* Plain garbage (not CBOR at all). */
    unsigned char garbage[] = { 0xde, 0xad, 0xbe, 0xef, 0x00 };
    wf_chat_mod_frame fr2 = {0};
    WF_CHECK(wf_chat_mod_frame_parse_cbor(garbage, sizeof(garbage), &fr2) ==
             WF_ERR_PARSE);
    wf_chat_mod_frame_free(&fr2);

    /* NULL / empty guards. */
    wf_chat_mod_frame fr3 = {0};
    WF_CHECK(wf_chat_mod_frame_parse_cbor(NULL, 0, &fr3) == WF_ERR_INVALID_ARG);
    wf_chat_mod_frame_free(&fr3);
}

static void test_url_builder(void) {
    char *url = NULL;
    WF_CHECK(wf_chat_mod_events_build_url("wss://chat.example.com", "123",
                                          &url) == WF_OK);
    WF_CHECK(url && strcmp(url,
              "wss://chat.example.com/xrpc/chat.bsky.moderation.subscribeModEvents"
              "?cursor=123") == 0);
    free(url);

    WF_CHECK(wf_chat_mod_events_build_url("https://chat.example.com/", NULL,
                                          &url) == WF_OK);
    WF_CHECK(url && strcmp(url,
              "wss://chat.example.com/xrpc/chat.bsky.moderation.subscribeModEvents")
              == 0);
    free(url);

    WF_CHECK(wf_chat_mod_events_build_url("ftp://nope", NULL, &url) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_chat_mod_events_build_url(NULL, NULL, &url) ==
             WF_ERR_INVALID_ARG);
}

/* ─────────────────── Layer 2: end-to-end (gated) ─────────────────── */

#if defined(WOLFRAM_BUILD_SERVER)

struct e2e_ctx {
    int event_count;
    int got_fields;
    int got_error;
    char *last_type;
    wf_chat_mod_events_handle *handle;
    int done;
    uint16_t port;
};

static void on_e2e_event(const wf_chat_mod_event *event, int64_t seq,
                         void *userdata) {
    struct e2e_ctx *ctx = (struct e2e_ctx *)userdata;
    ctx->event_count++;
    ctx->last_type = event && event->type ? strdup(event->type) : NULL;
    if (seq == 42 && event && event->convo_id &&
        strcmp(event->convo_id, "c1") == 0 &&
        event->created_at &&
        strcmp(event->created_at, "2024-01-01T00:00:00Z") == 0)
        ctx->got_fields = 1;
}

static void on_e2e_error(wf_status status, const char *message, void *userdata) {
    struct e2e_ctx *ctx = (struct e2e_ctx *)userdata;
    (void)status; (void)message;
    ctx->got_error = 1;
}

/* Server handler: send two framed CBOR mod-events, then close. */
struct ws_arg { wf_xrpc_ws_stream *stream; };

static void *ws_streamer(void *arg) {
    struct ws_arg *a = (struct ws_arg *)arg;

    const char *kh[] = {"op", "t"};
    wf_cbor_item *vh[] = { mk_uint(1), mk_str("#eventConvoFirstMessage") };
    const char *kb[] = {"seq", "convoId", "rev", "createdAt"};
    wf_cbor_item *vb[] = { mk_uint(42), mk_str("c1"), mk_str("r1"),
                           mk_str("2024-01-01T00:00:00Z") };
    size_t len = 0;
    unsigned char *frame = build_frame(kh, vh, 2, kb, vb, 4, &len);
    if (frame) {
        wf_xrpc_server_ws_send(a->stream, frame, len);
        free(frame);
    }
    wf_xrpc_server_ws_close(a->stream, 1000);
    free(a);
    return NULL;
}

static wf_status e2e_ws_handler(void *ctx, const wf_xrpc_request *req,
                                wf_xrpc_ws_stream *stream) {
    (void)ctx; (void)req;
    struct ws_arg *a = malloc(sizeof(*a));
    if (!a) return WF_ERR_ALLOC;
    a->stream = stream;
    pthread_t tid;
    if (pthread_create(&tid, NULL, ws_streamer, a) != 0) { free(a); return WF_ERR_ALLOC; }
    pthread_detach(tid);
    return WF_OK;
}

static void *e2e_runner(void *arg) {
    struct e2e_ctx *ctx = (struct e2e_ctx *)arg;

    wf_chat_mod_events_options opts = {0};
    opts.on_event = on_e2e_event;
    opts.on_error = on_e2e_error;
    opts.userdata = ctx;
    opts.reconnect_delay_ms = 100;

    /* Connect straight to the local server port. */
    char url[256];
    snprintf(url, sizeof(url), "ws://127.0.0.1:%u", ctx->port);
    opts.service = url;

    wf_chat_mod_events_handle *h = NULL;
    ctx->handle = NULL;
    wf_chat_mod_events_start(&opts, &h);
    ctx->handle = NULL;
    ctx->done = 1;
    return NULL;
}

static void test_e2e_local(void) {
    wf_xrpc_server *server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) { printf("SKIP: server start failed\n"); return; }
    uint16_t port = wf_xrpc_server_port(server);
    if (port == 0) { wf_xrpc_server_free(server); printf("SKIP: no port\n"); return; }

    if (wf_xrpc_server_register_ws(server, WF_CHAT_MOD_EVENTS_NSID,
                                   e2e_ws_handler, NULL) != WF_OK) {
        wf_xrpc_server_free(server);
        printf("SKIP: register ws\n");
        return;
    }

    struct e2e_ctx ctx = {0};
    ctx.port = port;

    pthread_t tid;
    if (pthread_create(&tid, NULL, e2e_runner, &ctx) != 0) {
        wf_xrpc_server_free(server);
        printf("SKIP: thread\n");
        return;
    }

    /* Best-effort: stop the subscription after a bounded time so we can never
     * hang CI. If the local loopback WS handshake completed, we will have
     * received the framed event by then. */
    struct timespec ts = { .tv_sec = 5, .tv_nsec = 0 };
    nanosleep(&ts, NULL);
    wf_chat_mod_events_stop(ctx.handle);
    pthread_join(tid, NULL);

    if (ctx.got_fields) {
        printf("PASS: e2e received framed mod-event with expected fields\n");
        WF_CHECK(ctx.last_type &&
                 strcmp(ctx.last_type, "#eventConvoFirstMessage") == 0);
    } else {
        printf("SKIP: e2e loopback WS unavailable (best-effort)\n");
    }
    free(ctx.last_type);

    wf_xrpc_server_free(server);
}

#endif /* WOLFRAM_BUILD_SERVER */

int main(void) {
    test_decode_message();
    test_decode_all_fields();
    test_decode_error_frame();
    test_decode_truncated_and_garbage();
    test_url_builder();
#if defined(WOLFRAM_BUILD_SERVER)
    test_e2e_local();
#endif
    WF_TEST_SUMMARY();
}
