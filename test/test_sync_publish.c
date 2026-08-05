#include "wolfram/sync_publish.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/repo/cid.h"
#include "wolfram/crypto.h"
#include "test.h"

#include <cbor.h>
#include <stdlib.h>
#include <string.h>

/* ── event equality (field-level, the round-trip invariant) ── */

static int cid_eq(const wf_cid *a, const wf_cid *b) {
    return cid_equal(a, b);
}

static int event_equal(const wf_subscribe_event *a,
                       const wf_subscribe_event *b) {
    if (a->type != b->type) return 0;
    if (a->seq != b->seq) return 0;

    switch (a->type) {
        case WF_SUBSCRIBE_EVENT_COMMIT: {
            const wf_subscribe_commit *x = &a->data.commit,
                                      *y = &b->data.commit;
            if (strcmp(x->did, y->did)) return 0;
            if (strcmp(x->rev, y->rev)) return 0;
            if (strcmp(x->since, y->since)) return 0;
            if (strcmp(x->time, y->time)) return 0;
            if (!cid_eq(&x->commit_cid, &y->commit_cid)) return 0;
            if (x->blocks_len != y->blocks_len) return 0;
            if (x->blocks_len && memcmp(x->blocks, y->blocks, x->blocks_len))
                return 0;
            if (x->ops_count != y->ops_count) return 0;
            for (size_t i = 0; i < x->ops_count; i++) {
                if (strcmp(x->ops[i].action, y->ops[i].action)) return 0;
                if (strcmp(x->ops[i].path, y->ops[i].path)) return 0;
                if (x->ops[i].has_cid != y->ops[i].has_cid) return 0;
                if (x->ops[i].has_cid &&
                    !cid_eq(&x->ops[i].cid, &y->ops[i].cid))
                    return 0;
                if (x->ops[i].has_prev != y->ops[i].has_prev) return 0;
                if (x->ops[i].has_prev &&
                    !cid_eq(&x->ops[i].prev, &y->ops[i].prev))
                    return 0;
            }
            if (x->has_prev_data != y->has_prev_data) return 0;
            if (x->has_prev_data && !cid_eq(&x->prev_data, &y->prev_data))
                return 0;
            return 1;
        }
        case WF_SUBSCRIBE_EVENT_SYNC: {
            const wf_subscribe_sync *x = &a->data.sync, *y = &b->data.sync;
            if (strcmp(x->did, y->did)) return 0;
            if (strcmp(x->rev, y->rev)) return 0;
            if (strcmp(x->time, y->time)) return 0;
            if (x->blocks_len != y->blocks_len) return 0;
            if (x->blocks_len && memcmp(x->blocks, y->blocks, x->blocks_len))
                return 0;
            return 1;
        }
        case WF_SUBSCRIBE_EVENT_IDENTITY: {
            const wf_subscribe_identity *x = &a->data.identity,
                                        *y = &b->data.identity;
            if (strcmp(x->did, y->did)) return 0;
            if (strcmp(x->time, y->time)) return 0;
            if (x->has_handle != y->has_handle) return 0;
            if (x->has_handle && strcmp(x->handle, y->handle)) return 0;
            return 1;
        }
        case WF_SUBSCRIBE_EVENT_ACCOUNT: {
            const wf_subscribe_account *x = &a->data.account,
                                       *y = &b->data.account;
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
        case WF_SUBSCRIBE_EVENT_LABELS: {
            const wf_subscribe_labels *x = &a->data.labels,
                                      *y = &b->data.labels;
            if (x->labels_count != y->labels_count) return 0;
            for (size_t i = 0; i < x->labels_count; i++) {
                const wf_label *lx = &x->labels[i], *ly = &y->labels[i];
                if (lx->has_ver != ly->has_ver) return 0;
                if (lx->has_ver && lx->ver != ly->ver) return 0;
                if (strcmp(lx->src, ly->src)) return 0;
                if (strcmp(lx->uri, ly->uri)) return 0;
                if (lx->has_cid != ly->has_cid) return 0;
                if (lx->has_cid && strcmp(lx->cid, ly->cid)) return 0;
                if (strcmp(lx->val, ly->val)) return 0;
                if (lx->has_neg != ly->has_neg) return 0;
                if (lx->has_neg && !!lx->neg != !!ly->neg) return 0;
                if (strcmp(lx->cts, ly->cts)) return 0;
                if (lx->has_exp != ly->has_exp) return 0;
                if (lx->has_exp && strcmp(lx->exp, ly->exp)) return 0;
                if (lx->has_sig != ly->has_sig) return 0;
                if (lx->has_sig) {
                    if (!lx->sig || !ly->sig) return 0;
                    if (strcmp(lx->sig, ly->sig)) return 0;
                }
            }
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
            if (kl == 2 && memcmp(ks, "op", 2) == 0)
                op = pairs[i].value;
            else if (kl == 1 && memcmp(ks, "t", 1) == 0)
                t = pairs[i].value;
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

/*
 * Every CID link on the wire must be tag 42 wrapping a byte string that starts
 * with the 0x00 multibase identity prefix.
 *
 * This is checked against the raw bytes on purpose. Round-tripping cannot see
 * it: wf_subscribe's parse_cid_link tolerantly skips leading zero bytes, so a
 * frame written without the prefix decodes back to exactly what went in and
 * every existing test passes. Strict DAG-CBOR readers — the Go ipld stack
 * relays are built on — reject such a frame outright, so the whole firehose
 * was being silently discarded by conforming consumers.
 */
static void test_cid_links_carry_multibase_prefix(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
    wf_subscribe_commit *c = &ev.data.commit;
    c->seq = 7;
    snprintf(c->did, sizeof(c->did), "did:plc:test");
    snprintf(c->rev, sizeof(c->rev), "3jui7kd54zh2y");
    snprintf(c->since, sizeof(c->since), "3jui7kd54zh2x");
    snprintf(c->time, sizeof(c->time), "2026-01-01T00:00:00.000Z");
    c->commit_cid = sample_cid(1);
    c->prev_data = sample_cid(2);
    c->has_prev_data = 1;
    wf_subscribe_repo_op op = {0};
    snprintf(op.action, sizeof(op.action), "update");
    op.path = "app.bsky.feed.post/abc";
    op.cid = sample_cid(3);
    op.has_cid = 1;
    op.prev = sample_cid(4);
    op.has_prev = 1;
    c->ops = &op;
    c->ops_count = 1;

    unsigned char *buf = NULL;
    size_t len = 0;
    WF_CHECK(wf_sync_publish_event(&ev, &buf, &len) == WF_OK);
    if (!buf) return;

    /* Tag 42 encodes as 0xD8 0x2A. Each occurrence must be followed by a byte
     * string header and then 0x00. A 36-byte CID plus the prefix is 37 bytes,
     * which encodes as 0x58 0x25 (one-byte length). */
    int links = 0, prefixed = 0;
    for (size_t i = 0; i + 4 < len; i++) {
        if (buf[i] != 0xD8 || buf[i + 1] != 0x2A) continue;
        links++;
        /* buf[i+2] is the bytestring header; the payload starts after it. */
        size_t payload = 0;
        if (buf[i + 2] == 0x58)
            payload = i + 4; /* 1-byte length */
        else if ((buf[i + 2] & 0xE0) == 0x40)
            payload = i + 3; /* inline len */
        if (payload && payload < len && buf[payload] == 0x00) prefixed++;
    }
    /* commit, prevData, ops[0].cid, ops[0].prev */
    WF_CHECK(links == 4);
    WF_CHECK(prefixed == links);
    free(buf);
}

/* A creation must not carry a `prev` key at all — the lexicon says the field
 * "should not be defined" there, and a null is not the same as absent. */
static void test_create_op_omits_prev(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
    wf_subscribe_commit *c = &ev.data.commit;
    c->seq = 8;
    snprintf(c->did, sizeof(c->did), "did:plc:test");
    snprintf(c->rev, sizeof(c->rev), "3jui7kd54zh2y");
    snprintf(c->time, sizeof(c->time), "2026-01-01T00:00:00.000Z");
    c->commit_cid = sample_cid(1);
    wf_subscribe_repo_op op = {0};
    snprintf(op.action, sizeof(op.action), "create");
    op.path = "app.bsky.feed.post/abc";
    op.cid = sample_cid(3);
    op.has_cid = 1;
    op.has_prev = 0;
    c->ops = &op;
    c->ops_count = 1;

    unsigned char *buf = NULL;
    size_t len = 0;
    WF_CHECK(wf_sync_publish_event(&ev, &buf, &len) == WF_OK);
    if (!buf) return;
    /* "prev" as a CBOR text string is 0x64 'p' 'r' 'e' 'v'. */
    static const unsigned char needle[] = {0x64, 'p', 'r', 'e', 'v'};
    int found = 0;
    for (size_t i = 0; i + sizeof(needle) <= len; i++)
        if (memcmp(buf + i, needle, sizeof(needle)) == 0) {
            found = 1;
            break;
        }
    WF_CHECK(!found);
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
    snprintf(ev.data.commit.since, sizeof(ev.data.commit.since),
             "3kf2fke3oy29");
    snprintf(ev.data.commit.time, sizeof(ev.data.commit.time),
             "2024-01-02T03:04:05.000Z");
    ev.data.commit.has_prev_data = 1;
    ev.data.commit.prev_data = sample_cid(99);

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
    ev.data.commit.ops[1].has_cid = 0; /* deletion: null cid */
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
    snprintf(ev.data.sync.time, sizeof(ev.data.sync.time),
             "2024-05-06T07:08:09.000Z");
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
    snprintf(ev.data.identity.did, sizeof(ev.data.identity.did),
             "did:plc:idme");
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
    snprintf(ev.data.account.status, sizeof(ev.data.account.status),
             "takendown");

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
    buf = NULL;
    len = 0;
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

/* Commit WITHOUT prevData: must encode with prevData omitted and still
 * round-trips (the optional field defaults to absent). */
static void test_commit_no_prev_data(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
    ev.seq = 43;
    ev.data.commit.seq = 43;
    snprintf(ev.data.commit.did, sizeof(ev.data.commit.did), "did:plc:noPrev");
    ev.data.commit.commit_cid = sample_cid(2);
    snprintf(ev.data.commit.rev, sizeof(ev.data.commit.rev), "3kf2fke3oy2c");
    snprintf(ev.data.commit.since, sizeof(ev.data.commit.since),
             "3kf2fke3oy2b");
    snprintf(ev.data.commit.time, sizeof(ev.data.commit.time),
             "2024-01-02T03:04:06.000Z");
    ev.data.commit.has_prev_data = 0;

    unsigned char blocks[4] = {0xaa, 0xbb, 0xcc, 0xdd};
    ev.data.commit.blocks = malloc(sizeof(blocks));
    memcpy(ev.data.commit.blocks, blocks, sizeof(blocks));
    ev.data.commit.blocks_len = sizeof(blocks);

    roundtrip(&ev, 0);
    free(ev.data.commit.blocks);
}

static void test_labels(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_LABELS;
    ev.seq = 1001;
    ev.data.labels.seq = 1001;
    ev.data.labels.labels_count = 2;
    ev.data.labels.labels = calloc(2, sizeof(wf_label));

    /* First label: fully populated (ver, cid, neg, exp, sig). */
    wf_label *a = &ev.data.labels.labels[0];
    a->ver = 1;
    a->has_ver = 1;
    a->src = strdup("did:plc:labeler");
    a->uri = strdup("at://did:plc:alice/app.bsky.feed.post/abc");
    a->cid =
        strdup("bafyreictrgtcg7wph56xjgu3ke7c2tjjgbj5dssviw6staozglsfg5nlu");
    a->has_cid = 1;
    a->val = strdup("!no-unauthenticated");
    a->neg = 1;
    a->has_neg = 1;
    a->cts = strdup("2024-01-02T03:04:05.000Z");
    a->exp = strdup("2025-01-02T03:04:05.000Z");
    a->has_exp = 1;
    unsigned char sig_bytes[8] = {0x01, 0x02, 0x03, 0x04,
                                  0x05, 0x06, 0x07, 0x08};
    WF_CHECK(wf_crypto_base64url_encode(sig_bytes, sizeof(sig_bytes),
                                        &a->sig) == WF_OK);
    a->has_sig = 1;

    /* Second label: minimal, only the required fields set. */
    wf_label *b = &ev.data.labels.labels[1];
    b->src = strdup("did:plc:labeler");
    b->uri = strdup("at://did:plc:bob/app.bsky.graph.follow/xyz");
    b->val = strdup("spam");
    b->cts = strdup("2024-03-04T05:06:07.000Z");

    roundtrip(&ev, 0);

    free(a->src);
    free(a->uri);
    free(a->cid);
    free(a->val);
    free(a->cts);
    free(a->exp);
    free(a->sig);
    free(b->src);
    free(b->uri);
    free(b->val);
    free(b->cts);
    free(ev.data.labels.labels);
}

/*
 * Map keys must go out in DAG-CBOR's deterministic order.
 *
 * RFC 8949 §4.2.1: shorter keys first, then bytewise. This asserts on the
 * encoded bytes because a round-trip cannot see the difference — our decoder
 * accepts any order, so frames in insertion order decode perfectly here and
 * are rejected by a strict reader. The expected order below is the order
 * bsky.network actually emits for a #commit.
 */
static void test_map_keys_are_canonically_ordered(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
    ev.seq = 77;
    ev.data.commit.seq = 77;
    snprintf(ev.data.commit.did, sizeof(ev.data.commit.did), "did:plc:order");
    ev.data.commit.commit_cid = sample_cid(3);
    snprintf(ev.data.commit.rev, sizeof(ev.data.commit.rev), "3kf2fke3oy2c");
    snprintf(ev.data.commit.time, sizeof(ev.data.commit.time),
             "2024-01-02T03:04:05.000Z");
    unsigned char blocks[2] = {0x01, 0x02};
    ev.data.commit.blocks = malloc(sizeof(blocks));
    memcpy(ev.data.commit.blocks, blocks, sizeof(blocks));
    ev.data.commit.blocks_len = sizeof(blocks);

    unsigned char *frame = NULL;
    size_t len = 0;
    WF_CHECK(wf_sync_publish_event(&ev, &frame, &len) == WF_OK);
    free(ev.data.commit.blocks);
    if (!frame) return;

    /* Walk the raw bytes: the keys must appear in this order. Searching for
     * each in turn and requiring the offsets to increase is enough, and does
     * not need a CBOR parser in the test. */
    static const char *expected[] = {
        "ops",   "rev",    "seq",    "repo",   "time",   "blobs",
        "since", "blocks", "commit", "rebase", "tooBig",
    };
    size_t previous = 0;
    int ordered = 1;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        const char *key = expected[i];
        size_t klen = strlen(key);
        size_t at = 0;
        int found = 0;
        for (size_t j = 0; j + klen <= len; j++) {
            /* Text-string header for a short key is 0x60|len, so match the
             * header too and avoid hitting the same letters inside a value. */
            if (j > 0 && (unsigned char)frame[j - 1] == (0x60 | klen) &&
                memcmp(frame + j, key, klen) == 0) {
                at = j;
                found = 1;
                break;
            }
        }
        WF_CHECK(found);
        if (!found) {
            ordered = 0;
            break;
        }
        if (at < previous) ordered = 0;
        previous = at;
    }
    WF_CHECK(ordered);
    free(frame);
}

/*
 * Integers must use the shortest form that holds the value.
 *
 * DAG-CBOR requires minimal-length integer encoding and a strict reader
 * rejects anything wider. This is invisible to a round-trip, because our own
 * decoder accepts any width — the frame decodes perfectly here and is dropped
 * by the consumer. So walk the encoded bytes and check every integer head.
 */
static size_t check_minimal_ints(const unsigned char *b, size_t len, size_t i,
                                 int *bad);

static size_t read_head(const unsigned char *b, size_t i, uint8_t *maj,
                        uint8_t *minor, uint64_t *val) {
    *maj = b[i] >> 5;
    *minor = b[i] & 0x1F;
    i++;
    if (*minor < 24) {
        *val = *minor;
        return i;
    }
    if (*minor == 24) {
        *val = b[i];
        return i + 1;
    }
    if (*minor == 25) {
        *val = ((uint64_t)b[i] << 8) | b[i + 1];
        return i + 2;
    }
    if (*minor == 26) {
        *val = ((uint64_t)b[i] << 24) | ((uint64_t)b[i + 1] << 16) |
               ((uint64_t)b[i + 2] << 8) | b[i + 3];
        return i + 4;
    }
    *val = 0;
    for (int k = 0; k < 8; k++) *val = (*val << 8) | b[i + k];
    return i + 8;
}

static uint8_t canonical_minor(uint64_t v) {
    if (v < 24) return (uint8_t)v;
    if (v <= 0xFF) return 24;
    if (v <= 0xFFFF) return 25;
    if (v <= 0xFFFFFFFFu) return 26;
    return 27;
}

static size_t check_minimal_ints(const unsigned char *b, size_t len, size_t i,
                                 int *bad) {
    uint8_t maj, minor;
    uint64_t val;
    if (i >= len) return i;
    i = read_head(b, i, &maj, &minor, &val);
    if ((maj == 0 || maj == 1) && minor != canonical_minor(val)) (*bad)++;
    switch (maj) {
        case 2:
        case 3:
            i += (size_t)val;
            break;
        case 4:
            for (uint64_t k = 0; k < val; k++)
                i = check_minimal_ints(b, len, i, bad);
            break;
        case 5:
            for (uint64_t k = 0; k < val; k++) {
                i = check_minimal_ints(b, len, i, bad);
                i = check_minimal_ints(b, len, i, bad);
            }
            break;
        case 6:
            i = check_minimal_ints(b, len, i, bad);
            break;
        default:
            break;
    }
    return i;
}

static void test_integers_are_minimally_encoded(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
    /* A seq that fits in 32 bits: encoding it at 64 is the exact bug. */
    ev.seq = 1785119372;
    ev.data.commit.seq = ev.seq;
    snprintf(ev.data.commit.did, sizeof(ev.data.commit.did), "did:plc:minint");
    ev.data.commit.commit_cid = sample_cid(5);
    snprintf(ev.data.commit.rev, sizeof(ev.data.commit.rev), "3kf2fke3oy2c");
    snprintf(ev.data.commit.time, sizeof(ev.data.commit.time),
             "2024-01-02T03:04:05.000Z");
    unsigned char blocks[2] = {0x01, 0x02};
    ev.data.commit.blocks = malloc(sizeof(blocks));
    memcpy(ev.data.commit.blocks, blocks, sizeof(blocks));
    ev.data.commit.blocks_len = sizeof(blocks);

    unsigned char *frame = NULL;
    size_t len = 0;
    WF_CHECK(wf_sync_publish_event(&ev, &frame, &len) == WF_OK);
    free(ev.data.commit.blocks);
    if (!frame) return;

    int bad = 0;
    size_t i = check_minimal_ints(frame, len, 0, &bad); /* header */
    check_minimal_ints(frame, len, i, &bad);            /* body */
    WF_CHECK(bad == 0);
    free(frame);
}

/*
 * One canonical-DAG-CBOR check, applied to every event kind.
 *
 * Three separate defects of this shape each made a PDS built on this SDK
 * unfederatable — a CID link missing its 0x00 multibase prefix, map keys out
 * of canonical order, and integers encoded wider than necessary — and each was
 * found only after federation broke, because our own decoder tolerates
 * precisely what the encoder gets wrong. The targeted tests above were written
 * one at a time, after each bug. This asserts the whole property at once so
 * the next one fails here instead of in the field.
 *
 * Checks, over the encoded bytes: definite lengths only; minimal integer,
 * string and byte heads; map keys sorted shorter-first then bytewise; CID
 * links as tag 42 wrapping a byte string that begins 0x00; no floats.
 */
typedef struct {
    int nonminimal;
    int indefinite;
    int unsorted_keys;
    int bad_cid_link;
    int floats;
} canon_report;

static size_t canon_walk(const unsigned char *b, size_t len, size_t i,
                         canon_report *r);

static size_t canon_head(const unsigned char *b, size_t i, uint8_t *maj,
                         uint8_t *minor, uint64_t *val, canon_report *r) {
    *maj = b[i] >> 5;
    *minor = b[i] & 0x1F;
    i++;
    if (*minor < 24) {
        *val = *minor;
        return i;
    }
    if (*minor == 24) {
        *val = b[i];
        i += 1;
    } else if (*minor == 25) {
        *val = ((uint64_t)b[i] << 8) | b[i + 1];
        i += 2;
    } else if (*minor == 26) {
        *val = ((uint64_t)b[i] << 24) | ((uint64_t)b[i + 1] << 16) |
               ((uint64_t)b[i + 2] << 8) | b[i + 3];
        i += 4;
    } else if (*minor == 27) {
        *val = 0;
        for (int k = 0; k < 8; k++) *val = (*val << 8) | b[i + k];
        i += 8;
    } else {
        /* 31 is indefinite length, which DAG-CBOR forbids outright. */
        r->indefinite++;
        *val = 0;
    }
    /* Every head must use the shortest form that holds its argument. */
    uint8_t want = *val < 24             ? (uint8_t)*val
                   : *val <= 0xFF        ? 24
                   : *val <= 0xFFFF      ? 25
                   : *val <= 0xFFFFFFFFu ? 26
                                         : 27;
    if (*minor <= 27 && *minor != want) r->nonminimal++;
    return i;
}

static size_t canon_walk(const unsigned char *b, size_t len, size_t i,
                         canon_report *r) {
    uint8_t maj, minor;
    uint64_t val;
    if (i >= len) return i;
    i = canon_head(b, i, &maj, &minor, &val, r);
    switch (maj) {
        case 2:
        case 3:
            i += (size_t)val;
            break;
        case 4:
            for (uint64_t k = 0; k < val; k++) i = canon_walk(b, len, i, r);
            break;
        case 5: {
            /* Keys must be sorted shorter-first, then bytewise. */
            size_t prev_at = 0, prev_len = 0;
            for (uint64_t k = 0; k < val; k++) {
                uint8_t kmaj, kminor;
                uint64_t klen;
                size_t head = i;
                i = canon_head(b, i, &kmaj, &kminor, &klen, r);
                size_t at = i;
                if (kmaj == 3) {
                    if (k > 0) {
                        bool ordered =
                            (prev_len < klen) ||
                            (prev_len == klen &&
                             memcmp(b + prev_at, b + at, (size_t)klen) < 0);
                        if (!ordered) r->unsorted_keys++;
                    }
                    prev_at = at;
                    prev_len = (size_t)klen;
                    i += (size_t)klen;
                } else {
                    i = head;
                    i = canon_walk(b, len, i, r);
                }
                i = canon_walk(b, len, i, r); /* value */
            }
            break;
        }
        case 6:
            if (val == 42) {
                /* A CID link wraps a byte string whose first byte is 0x00. */
                uint8_t tmaj, tminor;
                uint64_t tlen;
                size_t at = canon_head(b, i, &tmaj, &tminor, &tlen, r);
                if (tmaj != 2 || tlen == 0 || b[at] != 0x00) r->bad_cid_link++;
                i = at + (size_t)tlen;
            } else {
                i = canon_walk(b, len, i, r);
            }
            break;
        case 7:
            /* 25/26/27 in major 7 are half/single/double floats. */
            if (minor == 25) {
                r->floats++;
                i += 2;
            } else if (minor == 26) {
                r->floats++;
                i += 4;
            } else if (minor == 27) {
                r->floats++;
                i += 8;
            }
            break;
        default:
            break;
    }
    return i;
}

static void canon_check(const unsigned char *frame, size_t len,
                        const char *what) {
    canon_report r = {0};
    size_t i = canon_walk(frame, len, 0, &r); /* header */
    canon_walk(frame, len, i, &r);            /* body */
    if (r.nonminimal)
        fprintf(stderr, "  %s: %d non-minimal heads\n", what, r.nonminimal);
    if (r.indefinite)
        fprintf(stderr, "  %s: %d indefinite lengths\n", what, r.indefinite);
    if (r.unsorted_keys)
        fprintf(stderr, "  %s: %d unsorted map keys\n", what, r.unsorted_keys);
    if (r.bad_cid_link)
        fprintf(stderr, "  %s: %d malformed CID links\n", what, r.bad_cid_link);
    if (r.floats) fprintf(stderr, "  %s: %d floats\n", what, r.floats);
    WF_CHECK(r.nonminimal == 0 && r.indefinite == 0 && r.unsorted_keys == 0 &&
             r.bad_cid_link == 0 && r.floats == 0);
}

/* Build one of every event kind and hold them all to the same standard. */
static void test_every_event_is_canonical(void) {
    unsigned char *frame = NULL;
    size_t len = 0;
    const char *now = "2024-01-02T03:04:05.000Z";

    {
        wf_subscribe_event ev = {0};
        ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
        ev.seq = 1785119372;
        ev.data.commit.seq = ev.seq;
        snprintf(ev.data.commit.did, sizeof(ev.data.commit.did),
                 "did:plc:canon");
        ev.data.commit.commit_cid = sample_cid(1);
        ev.data.commit.has_prev_data = 1;
        ev.data.commit.prev_data = sample_cid(2);
        snprintf(ev.data.commit.rev, sizeof(ev.data.commit.rev),
                 "3kf2fke3oy2c");
        snprintf(ev.data.commit.since, sizeof(ev.data.commit.since),
                 "3kf2fke3oy2b");
        snprintf(ev.data.commit.time, sizeof(ev.data.commit.time), "%s", now);
        unsigned char blocks[3] = {1, 2, 3};
        ev.data.commit.blocks = malloc(sizeof(blocks));
        memcpy(ev.data.commit.blocks, blocks, sizeof(blocks));
        ev.data.commit.blocks_len = sizeof(blocks);
        if (wf_sync_publish_event(&ev, &frame, &len) == WF_OK) {
            canon_check(frame, len, "#commit");
            free(frame);
            frame = NULL;
        }
        free(ev.data.commit.blocks);
    }
    {
        wf_subscribe_event ev = {0};
        ev.type = WF_SUBSCRIBE_EVENT_SYNC;
        ev.seq = 300;
        ev.data.sync.seq = ev.seq;
        snprintf(ev.data.sync.did, sizeof(ev.data.sync.did), "did:plc:canon");
        snprintf(ev.data.sync.rev, sizeof(ev.data.sync.rev), "3kf2fke3oy2c");
        snprintf(ev.data.sync.time, sizeof(ev.data.sync.time), "%s", now);
        unsigned char blocks[2] = {9, 9};
        ev.data.sync.blocks = malloc(sizeof(blocks));
        memcpy(ev.data.sync.blocks, blocks, sizeof(blocks));
        ev.data.sync.blocks_len = sizeof(blocks);
        if (wf_sync_publish_event(&ev, &frame, &len) == WF_OK) {
            canon_check(frame, len, "#sync");
            free(frame);
            frame = NULL;
        }
        free(ev.data.sync.blocks);
    }
    {
        wf_subscribe_event ev = {0};
        ev.type = WF_SUBSCRIBE_EVENT_IDENTITY;
        ev.seq = 70000;
        ev.data.identity.seq = ev.seq;
        snprintf(ev.data.identity.did, sizeof(ev.data.identity.did),
                 "did:plc:canon");
        snprintf(ev.data.identity.handle, sizeof(ev.data.identity.handle),
                 "a.example.com");
        ev.data.identity.has_handle = 1;
        snprintf(ev.data.identity.time, sizeof(ev.data.identity.time), "%s",
                 now);
        if (wf_sync_publish_event(&ev, &frame, &len) == WF_OK) {
            canon_check(frame, len, "#identity");
            free(frame);
            frame = NULL;
        }
    }
    {
        wf_subscribe_event ev = {0};
        ev.type = WF_SUBSCRIBE_EVENT_ACCOUNT;
        ev.seq = 23;
        ev.data.account.seq = ev.seq;
        snprintf(ev.data.account.did, sizeof(ev.data.account.did),
                 "did:plc:canon");
        ev.data.account.active = 0;
        snprintf(ev.data.account.status, sizeof(ev.data.account.status),
                 "deactivated");
        ev.data.account.has_status = 1;
        snprintf(ev.data.account.time, sizeof(ev.data.account.time), "%s", now);
        if (wf_sync_publish_event(&ev, &frame, &len) == WF_OK) {
            canon_check(frame, len, "#account");
            free(frame);
            frame = NULL;
        }
    }
    {
        wf_subscribe_event ev = {0};
        ev.type = WF_SUBSCRIBE_EVENT_INFO;
        snprintf(ev.data.info.name, sizeof(ev.data.info.name),
                 "OutdatedCursor");
        snprintf(ev.data.info.message, sizeof(ev.data.info.message),
                 "Requested cursor exceeded limit");
        ev.data.info.has_message = 1;
        if (wf_sync_publish_event(&ev, &frame, &len) == WF_OK) {
            canon_check(frame, len, "#info");
            free(frame);
            frame = NULL;
        }
    }
    if (wf_sync_publish_error(1234567, "FutureCursor", "Cursor in the future.",
                              &frame, &len) == WF_OK) {
        canon_check(frame, len, "error frame");
        free(frame);
        frame = NULL;
    }
}

int main(void) {
    test_commit();
    test_cid_links_carry_multibase_prefix();
    test_map_keys_are_canonically_ordered();
    test_integers_are_minimally_encoded();
    test_every_event_is_canonical();
    test_create_op_omits_prev();
    test_commit_no_prev_data();
    test_sync();
    test_identity();
    test_account();
    test_info();
    test_labels();
    test_error();
    WF_TEST_SUMMARY();
}
