#include "wolfram/sync_publish.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/repo/cid.h"
#include "test.h"

#include <cbor.h>
#include <stdlib.h>
#include <string.h>

/* ── event equality (field-level, the round-trip invariant) ── */

static int cid_eq(const wf_cid *a, const wf_cid *b) {
    return cid_equal(a, b);
}

static int event_equal(const wf_subscribe_event *a, const wf_subscribe_event *b) {
    if (a->type != b->type) return 0;
    if (a->seq != b->seq) return 0;

    switch (a->type) {
    case WF_SUBSCRIBE_EVENT_COMMIT: {
        const wf_subscribe_commit *x = &a->data.commit, *y = &b->data.commit;
        if (strcmp(x->did, y->did)) return 0;
        if (strcmp(x->rev, y->rev)) return 0;
        if (strcmp(x->since, y->since)) return 0;
        if (strcmp(x->time, y->time)) return 0;
        if (!cid_eq(&x->commit_cid, &y->commit_cid)) return 0;
        if (x->blocks_len != y->blocks_len) return 0;
        if (x->blocks_len && memcmp(x->blocks, y->blocks, x->blocks_len)) return 0;
        if (x->ops_count != y->ops_count) return 0;
        for (size_t i = 0; i < x->ops_count; i++) {
            if (strcmp(x->ops[i].action, y->ops[i].action)) return 0;
            if (strcmp(x->ops[i].path, y->ops[i].path)) return 0;
            if (x->ops[i].has_cid != y->ops[i].has_cid) return 0;
            if (x->ops[i].has_cid && !cid_eq(&x->ops[i].cid, &y->ops[i].cid)) return 0;
            if (x->ops[i].has_prev != y->ops[i].has_prev) return 0;
            if (x->ops[i].has_prev && !cid_eq(&x->ops[i].prev, &y->ops[i].prev)) return 0;
        }
        return 1;
    }
    case WF_SUBSCRIBE_EVENT_SYNC: {
        const wf_subscribe_sync *x = &a->data.sync, *y = &b->data.sync;
        if (strcmp(x->did, y->did)) return 0;
        if (strcmp(x->rev, y->rev)) return 0;
        if (strcmp(x->time, y->time)) return 0;
        if (x->blocks_len != y->blocks_len) return 0;
        if (x->blocks_len && memcmp(x->blocks, y->blocks, x->blocks_len)) return 0;
        return 1;
    }
    case WF_SUBSCRIBE_EVENT_IDENTITY: {
        const wf_subscribe_identity *x = &a->data.identity, *y = &b->data.identity;
        if (strcmp(x->did, y->did)) return 0;
        if (strcmp(x->time, y->time)) return 0;
        if (x->has_handle != y->has_handle) return 0;
        if (x->has_handle && strcmp(x->handle, y->handle)) return 0;
        return 1;
    }
    case WF_SUBSCRIBE_EVENT_ACCOUNT: {
        const wf_subscribe_account *x = &a->data.account, *y = &b->data.account;
        if (strcmp(x->did, y->did)) return 0;
        if (strcmp(x->time, y->time)) return 0;
        if (x->active != y->active) return 0;
        if (x->has_status != y->has_status) return 0;
        if (x->has_status && strcmp(x->status, y->status)) return 0;
        return 1;
    }
    case WF_SUBSCRIBE_EVENT_INFO: {
        const wf_subscribe_info *x = &a->data.info, *y = &b->data.info;
        if (strcmp(x->name, y->name)) return 0;
        if (x->has_message != y->has_message) return 0;
        if (x->has_message && strcmp(x->message, y->message)) return 0;
        return 1;
    }
    case WF_SUBSCRIBE_EVENT_ERROR: {
        const char *xe = a->data.error.error ? a->data.error.error : "";
        const char *ye = b->data.error.error ? b->data.error.error : "";
        if (strcmp(xe, ye)) return 0;
        const char *xm = a->data.error.message ? a->data.error.message : "";
        const char *ym = b->data.error.message ? b->data.error.message : "";
        if (strcmp(xm, ym)) return 0;
        return 1;
    }
    default:
        return 0;
    }
}

/* Decode the first CBOR item and confirm it is the `{op,t}` header map, and
 * that a full second item (the body) follows immediately. */
static void check_frame_layout(const unsigned char *buf, size_t len,
                               int expect_error) {
    struct cbor_load_result lr = {0};
    cbor_item_t *header = cbor_load(buf, len, &lr);
    WF_CHECK(header != NULL);
    WF_CHECK(cbor_isa_map(header));
    WF_CHECK(lr.read > 0 && lr.read < len);

    cbor_item_t *op = NULL, *t = NULL;
    size_t count = cbor_map_size(header);
    struct cbor_pair *pairs = cbor_map_handle(header);
    for (size_t i = 0; i < count; i++) {
        if (cbor_isa_string(pairs[i].key)) {
            size_t kl = cbor_string_length(pairs[i].key);
            const char *ks = (const char *)cbor_string_handle(pairs[i].key);
            if (kl == 2 && memcmp(ks, "op", 2) == 0) op = pairs[i].value;
            else if (kl == 1 && memcmp(ks, "t", 1) == 0) t = pairs[i].value;
        }
    }
    WF_CHECK(op != NULL);
    if (expect_error) {
        WF_CHECK(cbor_isa_uint(op) == false && cbor_isa_negint(op));
    } else {
        WF_CHECK(t != NULL);
        WF_CHECK(cbor_isa_string(t));
    }

    cbor_item_t *body = cbor_load(buf + lr.read, len - lr.read, &lr);
    WF_CHECK(body != NULL);
    WF_CHECK(cbor_isa_map(body));
    cbor_decref(&body);
    cbor_decref(&header);
}

static wf_cid sample_cid(unsigned char seed) {
    unsigned char block[16];
    for (int i = 0; i < 16; i++) block[i] = (unsigned char)(seed + i);
    wf_cid c;
    wf_cid_of_block(block, sizeof(block), &c);
    return c;
}

static void roundtrip(const wf_subscribe_event *ev, int expect_error) {
    unsigned char *buf = NULL;
    size_t len = 0;
    wf_status s = wf_sync_publish_event(ev, &buf, &len);
    WF_CHECK(s == WF_OK);
    WF_CHECK(buf != NULL && len > 0);

    check_frame_layout(buf, len, expect_error);

    wf_subscribe_event dec = {0};
    s = wf_subscribe_decode_frame(buf, len, &dec);
    WF_CHECK(s == WF_OK);
    WF_CHECK(event_equal(ev, &dec));

    wf_subscribe_event_free(&dec);
    free(buf);
}

static void test_commit(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
    ev.seq = 42;
    ev.data.commit.seq = 42;
    snprintf(ev.data.commit.did, sizeof(ev.data.commit.did), "did:plc:abc123");
    ev.data.commit.commit_cid = sample_cid(1);
    snprintf(ev.data.commit.rev, sizeof(ev.data.commit.rev), "3kf2fke3oy2a");
    snprintf(ev.data.commit.since, sizeof(ev.data.commit.since), "3kf2fke3oy29");
    snprintf(ev.data.commit.time, sizeof(ev.data.commit.time),
             "2024-01-02T03:04:05.000Z");

    unsigned char blocks[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    ev.data.commit.blocks = malloc(sizeof(blocks));
    memcpy(ev.data.commit.blocks, blocks, sizeof(blocks));
    ev.data.commit.blocks_len = sizeof(blocks);

    static const char *paths[] = {"app.bsky.feed.post/abc",
                                  "app.bsky.feed.post/def"};
    ev.data.commit.ops_count = 2;
    ev.data.commit.ops = calloc(2, sizeof(wf_subscribe_repo_op));
    strcpy(ev.data.commit.ops[0].action, "create");
    ev.data.commit.ops[0].path = strdup(paths[0]);
    ev.data.commit.ops[0].has_cid = 1;
    ev.data.commit.ops[0].cid = sample_cid(10);
    ev.data.commit.ops[0].has_prev = 0;
    strcpy(ev.data.commit.ops[1].action, "delete");
    ev.data.commit.ops[1].path = strdup(paths[1]);
    ev.data.commit.ops[1].has_cid = 0;  /* deletion: null cid */
    ev.data.commit.ops[1].has_prev = 1;
    ev.data.commit.ops[1].prev = sample_cid(20);

    roundtrip(&ev, 0);

    for (size_t i = 0; i < ev.data.commit.ops_count; i++)
        free(ev.data.commit.ops[i].path);
    free(ev.data.commit.ops);
    free(ev.data.commit.blocks);
}

static void test_sync(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_SYNC;
    ev.seq = 7;
    ev.data.sync.seq = 7;
    snprintf(ev.data.sync.did, sizeof(ev.data.sync.did), "did:plc:syncme");
    snprintf(ev.data.sync.rev, sizeof(ev.data.sync.rev), "3kf2fke3oy2b");
    snprintf(ev.data.sync.time, sizeof(ev.data.sync.time), "2024-05-06T07:08:09.000Z");
    unsigned char blocks[4] = {0xde, 0xad, 0xbe, 0xef};
    ev.data.sync.blocks = malloc(sizeof(blocks));
    memcpy(ev.data.sync.blocks, blocks, sizeof(blocks));
    ev.data.sync.blocks_len = sizeof(blocks);

    roundtrip(&ev, 0);
    free(ev.data.sync.blocks);
}

static void test_identity(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_IDENTITY;
    ev.seq = 11;
    ev.data.identity.seq = 11;
    snprintf(ev.data.identity.did, sizeof(ev.data.identity.did), "did:plc:idme");
    snprintf(ev.data.identity.time, sizeof(ev.data.identity.time),
             "2024-07-08T09:10:11.000Z");
    ev.data.identity.has_handle = 1;
    snprintf(ev.data.identity.handle, sizeof(ev.data.identity.handle),
             "alice.example.com");

    roundtrip(&ev, 0);
}

static void test_account(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_ACCOUNT;
    ev.seq = 13;
    ev.data.account.seq = 13;
    snprintf(ev.data.account.did, sizeof(ev.data.account.did), "did:plc:acct");
    snprintf(ev.data.account.time, sizeof(ev.data.account.time),
             "2024-09-10T11:12:13.000Z");
    ev.data.account.active = 0;
    ev.data.account.has_status = 1;
    snprintf(ev.data.account.status, sizeof(ev.data.account.status), "takendown");

    roundtrip(&ev, 0);
}

static void test_info(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_INFO;
    snprintf(ev.data.info.name, sizeof(ev.data.info.name), "OutdatedCursor");
    ev.data.info.has_message = 1;
    snprintf(ev.data.info.message, sizeof(ev.data.info.message),
             "cursor is too old");

    roundtrip(&ev, 0);
}

static void test_error(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_ERROR;
    ev.data.error.error = strdup("FutureCursor");
    ev.data.error.message = strdup("cursor in the future");

    unsigned char *buf = NULL;
    size_t len = 0;
    wf_status s = wf_sync_publish_event(&ev, &buf, &len);
    WF_CHECK(s == WF_OK);
    check_frame_layout(buf, len, 1);

    wf_subscribe_event dec = {0};
    s = wf_subscribe_decode_frame(buf, len, &dec);
    WF_CHECK(s == WF_OK);
    WF_CHECK(event_equal(&ev, &dec));
    wf_subscribe_event_free(&dec);
    free(buf);
    free(ev.data.error.error);
    free(ev.data.error.message);

    /* Also exercise the dedicated error-builder entry point. */
    buf = NULL; len = 0;
    s = wf_sync_publish_error(99, "ConsumerTooSlow", NULL, &buf, &len);
    WF_CHECK(s == WF_OK);
    check_frame_layout(buf, len, 1);
    wf_subscribe_event dec2 = {0};
    s = wf_subscribe_decode_frame(buf, len, &dec2);
    WF_CHECK(s == WF_OK);
    WF_CHECK(dec2.type == WF_SUBSCRIBE_EVENT_ERROR);
    WF_CHECK(dec2.data.error.error &&
             strcmp(dec2.data.error.error, "ConsumerTooSlow") == 0);
    WF_CHECK(dec2.data.error.message == NULL);
    wf_subscribe_event_free(&dec2);
    free(buf);
}

int main(void) {
    test_commit();
    test_sync();
    test_identity();
    test_account();
    test_info();
    test_error();
    WF_TEST_SUMMARY();
}
