/*
 * test_label_frame.c — unit tests for wf_label_frame_parse_cbor, the decoder
 * for binary (DAG-CBOR) com.atproto.label.subscribeLabels event-stream frames.
 * No network access.
 */
#include "wolfram/label.h"
#include "wolfram/crypto.h"
#include "wolfram/repo/cbor.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* ── DAG-CBOR event-stream frames ── */
static wf_cbor_item *cb_str(const char *v) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_STRING;
    i->string.len = strlen(v);
    i->string.str = strdup(v);
    return i;
}
static wf_cbor_item *cb_uint(uint64_t v) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_UNSIGNED;
    i->uinteger = v;
    return i;
}
static wf_cbor_item *cb_bool(int v) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_SIMPLE;
    i->simple_value = v ? 21 : 20;
    return i;
}
static wf_cbor_item *cb_bytes(const unsigned char *d, size_t n) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_BYTES;
    i->bytes.data = malloc(n);
    memcpy(i->bytes.data, d, n);
    i->bytes.len = n;
    return i;
}
static wf_cbor_item *cb_map(size_t n, const char *const *keys,
                            wf_cbor_item **vals) {
    wf_cbor_item *i = calloc(1, sizeof(*i));
    i->type = WF_CBOR_MAP;
    i->map.count = n;
    i->map.pairs = calloc(n, sizeof(wf_cbor_pair));
    for (size_t k = 0; k < n; k++) {
        i->map.pairs[k].key = cb_str(keys[k]);
        i->map.pairs[k].value = vals[k];
    }
    return i;
}

/* header ++ body, serialised; consumes both items. */
static unsigned char *cb_frame(wf_cbor_item *header, wf_cbor_item *body,
                               size_t *len) {
    size_t hl = 0, bl = 0;
    unsigned char *h = wf_cbor_serialize(header, &hl);
    unsigned char *b = wf_cbor_serialize(body, &bl);
    unsigned char *out = malloc(hl + bl);
    memcpy(out, h, hl);
    memcpy(out + hl, b, bl);
    *len = hl + bl;
    free(h);
    free(b);
    wf_cbor_free(header);
    wf_cbor_free(body);
    return out;
}

static void test_frame_parse_cbor(void) {
    static const unsigned char sig[64] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const char *did = "did:plc:z72i7hdynmk6r22z27h6tvur";
    const char *uri = "at://did:plc:z72i7hdynmk6r22z27h6tvur/"
                      "app.bsky.feed.post/3jui7kd54zh2y";
    wf_label_message message = {0};
    size_t len = 0;

    /* #labels with one label whose sig is raw bytes */
    const char *lk[] = {"ver", "src", "uri", "val", "neg", "cts", "sig"};
    wf_cbor_item *lv[] = {cb_uint(1),
                          cb_str(did),
                          cb_str(uri),
                          cb_str("nsfw"),
                          cb_bool(0),
                          cb_str("2024-10-16T00:00:00Z"),
                          cb_bytes(sig, sizeof(sig))};
    wf_cbor_item *labels = calloc(1, sizeof(*labels));
    labels->type = WF_CBOR_ARRAY;
    labels->children.count = 1;
    labels->children.items = calloc(1, sizeof(wf_cbor_item *));
    labels->children.items[0] = cb_map(7, lk, lv);
    const char *bk[] = {"seq", "labels"};
    wf_cbor_item *bv[] = {cb_uint(17), labels};
    const char *hk[] = {"op", "t"};
    wf_cbor_item *hv[] = {cb_uint(1), cb_str("#labels")};
    unsigned char *frame = cb_frame(cb_map(2, hk, hv), cb_map(2, bk, bv), &len);
    WF_CHECK(wf_label_frame_parse_cbor(frame, len, &message) == WF_OK);
    WF_CHECK(message.type == WF_LABEL_MESSAGE_LABELS);
    WF_CHECK(message.data.labels.seq == 17);
    WF_CHECK(message.data.labels.count == 1);
    if (message.data.labels.count == 1) {
        const wf_label *l = &message.data.labels.items[0];
        char *expect = NULL;
        WF_CHECK(strcmp(l->val, "nsfw") == 0 && l->seq == 17);
        WF_CHECK(l->has_sig && wf_crypto_base64url_encode(sig, sizeof(sig),
                                                          &expect) == WF_OK);
        WF_CHECK(expect && l->sig && strcmp(l->sig, expect) == 0);
        free(expect);
    }
    wf_label_message_free(&message);
    /* truncated frame, and a frame with trailing garbage */
    WF_CHECK(wf_label_frame_parse_cbor(frame, len - 1, &message) != WF_OK);
    unsigned char *padded = malloc(len + 1);
    memcpy(padded, frame, len);
    padded[len] = 0x00;
    WF_CHECK(wf_label_frame_parse_cbor(padded, len + 1, &message) != WF_OK);
    free(padded);
    free(frame);

    /* #info */
    const char *ik[] = {"name", "message"};
    wf_cbor_item *iv[] = {cb_str("OutdatedCursor"), cb_str("cursor too old")};
    wf_cbor_item *ihv[] = {cb_uint(1), cb_str("#info")};
    frame = cb_frame(cb_map(2, hk, ihv), cb_map(2, ik, iv), &len);
    WF_CHECK(wf_label_frame_parse_cbor(frame, len, &message) == WF_OK);
    WF_CHECK(message.type == WF_LABEL_MESSAGE_INFO);
    WF_CHECK(strcmp(message.data.info.name, "OutdatedCursor") == 0);
    wf_label_message_free(&message);
    free(frame);

    /* error frame (op = -1) carries no label data, but decodes its name */
    wf_cbor_item *eop = calloc(1, sizeof(*eop));
    eop->type = WF_CBOR_NEGATIVE;
    eop->neginteger = 0;
    const char *ek[] = {"op"};
    wf_cbor_item *ev[] = {eop};
    const char *ebk[] = {"error", "message"};
    wf_cbor_item *ebv[] = {cb_str("FutureCursor"),
                           cb_str("cursor in the future")};
    frame = cb_frame(cb_map(1, ek, ev), cb_map(2, ebk, ebv), &len);
    WF_CHECK(wf_label_frame_parse_cbor(frame, len, &message) == WF_OK);
    WF_CHECK(message.type == WF_LABEL_MESSAGE_ERROR);
    WF_CHECK(message.data.info.name &&
             strcmp(message.data.info.name, "FutureCursor") == 0);
    WF_CHECK(message.data.info.has_message && message.data.info.message &&
             strcmp(message.data.info.message, "cursor in the future") == 0);
    wf_label_message_free(&message);
    free(frame);

    /* error frame missing the required "error" key is a parse failure */
    const char *ebk2[] = {"message"};
    wf_cbor_item *ebv2[] = {cb_str("no error name")};
    wf_cbor_item *eop2 = calloc(1, sizeof(*eop2));
    eop2->type = WF_CBOR_NEGATIVE;
    eop2->neginteger = 0;
    wf_cbor_item *ev2[] = {eop2};
    frame = cb_frame(cb_map(1, ek, ev2), cb_map(1, ebk2, ebv2), &len);
    WF_CHECK(wf_label_frame_parse_cbor(frame, len, &message) != WF_OK);
    free(frame);

    /* hostile declared sizes in the body are rejected, as is bad input */
    static const unsigned char hdr[] = {0xa2, 0x62, 0x6f, 0x70, 0x01,
                                        0x61, 0x74, 0x67, '#',  'l',
                                        'a',  'b',  'e',  'l',  's'};
    static const unsigned char hostile[] = {
        0xa1, 0x66, 'l', 'a', 'b', 'e', 'l', 's', 0x9a, 0xff, 0xff, 0xff, 0xff};
    unsigned char both[sizeof(hdr) + sizeof(hostile)];
    memcpy(both, hdr, sizeof(hdr));
    memcpy(both + sizeof(hdr), hostile, sizeof(hostile));
    WF_CHECK(wf_label_frame_parse_cbor(both, sizeof(both), &message) != WF_OK);
    WF_CHECK(wf_label_frame_parse_cbor(NULL, 0, &message) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_label_frame_parse_cbor(hdr, sizeof(hdr), &message) != WF_OK);
}

int main(void) {
    test_frame_parse_cbor();
    WF_TEST_SUMMARY();
}
