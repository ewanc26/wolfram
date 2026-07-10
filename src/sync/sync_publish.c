#include "wolfram/sync_publish.h"
#include "wolfram/sync_subscribe.h"

#include <cbor.h>
#include <stdlib.h>
#include <string.h>

/* ── low-level CBOR helpers ──
 * These mirror the encoding side of `src/sync/sync_subscribe.c`'s decoder so
 * the produced frames are the exact inverse of what that decoder parses. */

/* Build a signed integer item (CBOR major type 0 for >=0, 1 for <0). */
static cbor_item_t *int_item(int64_t v) {
    if (v >= 0) return cbor_build_uint64((uint64_t)v);
    return cbor_build_negint64((uint64_t)(-(v + 1)));
}

/* Build a CBOR tag-42 cid-link: a bytestring of the raw CID bytes. The decoder
 * strips a single leading 0x00 byte (none present for CIDv1 dag-cbor/sha2-256),
 * so we emit the bytes verbatim. */
static cbor_item_t *cid_link_item(const wf_cid *cid) {
    cbor_item_t *bs = cbor_build_bytestring(cid->bytes, cid->len);
    if (!bs) return NULL;
    cbor_item_t *tag = cbor_new_tag(42);
    if (!tag) {
        cbor_decref(&bs);
        return NULL;
    }
    cbor_tag_set_item(tag, bs);
    cbor_decref(&bs);
    return tag;
}

static int map_put(cbor_item_t *map, const char *key, cbor_item_t *val) {
    if (!val) return 0;
    struct cbor_pair p = {
        .key = cbor_move(cbor_build_string(key)),
        .value = cbor_move(val),
    };
    return cbor_map_add(map, p);
}

/* Serialize `a` then `b` into one contiguous owned buffer (header then body). */
static wf_status serialize_two(cbor_item_t *a, cbor_item_t *b,
                               unsigned char **out, size_t *out_len) {
    unsigned char *b1 = NULL, *b2 = NULL;
    size_t l1 = 0, l2 = 0;
    if (!cbor_serialize_alloc(a, &b1, &l1)) return WF_ERR_ALLOC;
    if (!cbor_serialize_alloc(b, &b2, &l2)) {
        free(b1);
        return WF_ERR_ALLOC;
    }
    unsigned char *buf = malloc(l1 + l2 ? l1 + l2 : 1);
    if (!buf) {
        free(b1);
        free(b2);
        return WF_ERR_ALLOC;
    }
    if (l1) memcpy(buf, b1, l1);
    if (l2) memcpy(buf + l1, b2, l2);
    free(b1);
    free(b2);
    *out = buf;
    *out_len = l1 + l2;
    return WF_OK;
}

static cbor_item_t *build_ops(const wf_subscribe_repo_op *ops, size_t n) {
    cbor_item_t *arr = cbor_new_definite_array(n);
    if (!arr) return NULL;
    for (size_t i = 0; i < n; i++) {
        cbor_item_t *op = cbor_new_definite_map(4);
        if (!op) {
            cbor_decref(&arr);
            return NULL;
        }
        map_put(op, "action", cbor_build_string(ops[i].action));
        map_put(op, "path", cbor_build_string(ops[i].path));
        if (ops[i].has_cid)
            map_put(op, "cid", cid_link_item(&ops[i].cid));
        else
            map_put(op, "cid", cbor_new_null());
        if (ops[i].has_prev)
            map_put(op, "prev", cid_link_item(&ops[i].prev));
        else
            map_put(op, "prev", cbor_new_null());
        (void)cbor_array_push(arr, op);
        cbor_decref(&op);
    }
    return arr;
}

/* ── body builders (inverse of the parse_* functions in sync_subscribe.c) ── */

static cbor_item_t *build_commit_body(const wf_subscribe_commit *c) {
    cbor_item_t *m = cbor_new_definite_map(8);
    map_put(m, "seq", int_item(c->seq));
    map_put(m, "repo", cbor_build_string(c->did));
    map_put(m, "commit", cid_link_item(&c->commit_cid));
    map_put(m, "rev", cbor_build_string(c->rev));
    if (c->since[0] != '\0')
        map_put(m, "since", cbor_build_string(c->since));
    map_put(m, "blocks",
            cbor_build_bytestring(c->blocks ? c->blocks
                                            : (const unsigned char *)"",
                                  c->blocks_len));
    map_put(m, "ops", build_ops(c->ops, c->ops_count));
    map_put(m, "time", cbor_build_string(c->time));
    return m;
}

static cbor_item_t *build_sync_body(const wf_subscribe_sync *s) {
    cbor_item_t *m = cbor_new_definite_map(5);
    map_put(m, "seq", int_item(s->seq));
    map_put(m, "did", cbor_build_string(s->did));
    map_put(m, "blocks",
            cbor_build_bytestring(s->blocks ? s->blocks
                                            : (const unsigned char *)"",
                                  s->blocks_len));
    map_put(m, "rev", cbor_build_string(s->rev));
    map_put(m, "time", cbor_build_string(s->time));
    return m;
}

static cbor_item_t *build_identity_body(const wf_subscribe_identity *id) {
    cbor_item_t *m = cbor_new_definite_map(id->has_handle ? 4 : 3);
    map_put(m, "seq", int_item(id->seq));
    map_put(m, "did", cbor_build_string(id->did));
    map_put(m, "time", cbor_build_string(id->time));
    if (id->has_handle)
        map_put(m, "handle", cbor_build_string(id->handle));
    return m;
}

static cbor_item_t *build_account_body(const wf_subscribe_account *ac) {
    cbor_item_t *m = cbor_new_definite_map(ac->has_status ? 5 : 4);
    map_put(m, "seq", int_item(ac->seq));
    map_put(m, "did", cbor_build_string(ac->did));
    map_put(m, "time", cbor_build_string(ac->time));
    map_put(m, "active", cbor_build_bool(ac->active));
    if (ac->has_status)
        map_put(m, "status", cbor_build_string(ac->status));
    return m;
}

static cbor_item_t *build_info_body(const wf_subscribe_info *info) {
    cbor_item_t *m = cbor_new_definite_map(info->has_message ? 2 : 1);
    map_put(m, "name", cbor_build_string(info->name));
    if (info->has_message)
        map_put(m, "message", cbor_build_string(info->message));
    return m;
}

static cbor_item_t *build_error_body(const char *error, const char *message) {
    cbor_item_t *m = cbor_new_definite_map(error ? 2 : 1);
    map_put(m, "error",
            error ? cbor_build_string(error) : cbor_new_null());
    if (message)
        map_put(m, "message", cbor_build_string(message));
    return m;
}

/* ── public API ── */

wf_status wf_sync_publish_event(const wf_subscribe_event *ev,
                                unsigned char **out, size_t *out_len) {
    if (!ev || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;

    cbor_item_t *body = NULL;
    const char *t = NULL;
    int is_error = 0;

    switch (ev->type) {
    case WF_SUBSCRIBE_EVENT_COMMIT:
        body = build_commit_body(&ev->data.commit);
        t = "#commit";
        break;
    case WF_SUBSCRIBE_EVENT_SYNC:
        body = build_sync_body(&ev->data.sync);
        t = "#sync";
        break;
    case WF_SUBSCRIBE_EVENT_IDENTITY:
        body = build_identity_body(&ev->data.identity);
        t = "#identity";
        break;
    case WF_SUBSCRIBE_EVENT_ACCOUNT:
        body = build_account_body(&ev->data.account);
        t = "#account";
        break;
    case WF_SUBSCRIBE_EVENT_INFO:
        body = build_info_body(&ev->data.info);
        t = "#info";
        break;
    case WF_SUBSCRIBE_EVENT_ERROR:
        body = build_error_body(ev->data.error.error, ev->data.error.message);
        is_error = 1;
        break;
    default:
        return WF_ERR_INVALID_ARG;
    }
    if (!body) return WF_ERR_ALLOC;

    /* Header: op:-1 with no `t` for errors, otherwise op:1 + union tag `t`. */
    cbor_item_t *header = cbor_new_definite_map(is_error ? 1 : 2);
    if (!header) {
        cbor_decref(&body);
        return WF_ERR_ALLOC;
    }
    map_put(header, "op", int_item(is_error ? -1 : 1));
    if (!is_error)
        map_put(header, "t", cbor_build_string(t));

    wf_status s = serialize_two(header, body, out, out_len);
    cbor_decref(&header);
    cbor_decref(&body);
    return s;
}

wf_status wf_sync_publish_error(int64_t seq, const char *error,
                                const char *message,
                                unsigned char **out, size_t *out_len) {
    (void)seq; /* the wire error frame carries no seq field; accepted for API symmetry */
    if (!out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;

    cbor_item_t *header = cbor_new_definite_map(1);
    cbor_item_t *body = build_error_body(error, message);
    if (!header || !body) {
        cbor_decref(&header);
        cbor_decref(&body);
        return WF_ERR_ALLOC;
    }
    map_put(header, "op", int_item(-1));

    wf_status s = serialize_two(header, body, out, out_len);
    cbor_decref(&header);
    cbor_decref(&body);
    return s;
}
