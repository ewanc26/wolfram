/*
 * label_subscribe_typed.c — agent-level consumption wrapper for
 * com.atproto.label.subscribeLabels.
 *
 * Bridges the streaming label subscription (wf_label_subscribe_start, which
 * decodes the `#labels`/`#info` frames produced by the new
 * WF_SUBSCRIBE_EVENT_LABELS path) into the moderation engine's owned
 * wf_mod_label representation:
 *
 *   - wf_label_to_mod_label     : one parsed #label -> owned wf_mod_label
 *   - wf_label_parse_subscribe  : one `#labels` frame -> owned wf_mod_label[]
 *   - wf_agent_subscribe_labels_typed : the agent convenience. It resolves the
 *       labeler service, syncs auth, and delegates to wf_label_subscribe_start,
 *       converting each received label into an owned wf_mod_label for the caller.
 *
 * All ownership follows the wf_mod_label contract: outputs are heap-owned and
 * released with wf_mod_labels_free (or field-by-field free()).
 */

#include "wolfram/label.h"
#include "wolfram/label_typed.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/identity.h"
#include "wolfram/xrpc.h"

#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local string dup (mirrors the helper in label_query.c). */
static char *wf_lsub_dup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

/* Convert one parsed #label into an owned wf_mod_label. */
wf_status wf_label_to_mod_label(const wf_label *src, wf_mod_label *dst) {
    if (!src || !dst) {
        return WF_ERR_INVALID_ARG;
    }
    memset(dst, 0, sizeof(*dst));

    /* A label without a value is not representable by the engine. */
    if (!src->val) {
        return WF_ERR_INVALID_ARG;
    }

    dst->src = wf_lsub_dup(src->src);
    dst->uri = wf_lsub_dup(src->uri);
    dst->val = wf_lsub_dup(src->val);
    dst->cts = wf_lsub_dup(src->cts);
    dst->cid = wf_lsub_dup(src->has_cid ? src->cid : NULL);
    dst->exp = wf_lsub_dup(src->has_exp ? src->exp : NULL);
    dst->neg = src->neg ? 1 : 0;
    dst->has_cid = src->has_cid ? 1 : 0;
    dst->ver = src->has_ver ? (int)src->ver : 0;

    if (!dst->val ||
        (src->src && !dst->src) ||
        (src->uri && !dst->uri) ||
        (src->cts && !dst->cts) ||
        (src->has_cid && !dst->cid) ||
        (src->has_exp && !dst->exp)) {
        free(dst->src);
        free(dst->uri);
        free(dst->val);
        free(dst->cts);
        free(dst->cid);
        free(dst->exp);
        memset(dst, 0, sizeof(*dst));
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/* Release the heap fields of a single wf_mod_label that was stack-allocated
 * (wf_mod_labels_free would also free the outer array, which we must not do). */
static void wf_lsub_free_one(wf_mod_label *m) {
    if (!m) return;
    free(m->src);
    free(m->uri);
    free(m->val);
    free(m->cts);
    free(m->cid);
    free(m->exp);
    memset(m, 0, sizeof(*m));
}

/* Parse a `#labels` frame into an owned wf_mod_label array. */
wf_status wf_label_parse_subscribe(const char *json, size_t len,
                                  wf_mod_label **out, size_t *out_count) {
    if (!out || !out_count || !json) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    *out_count = 0;

    /* Reuse the battle-tested frame parser, then convert per-label. */
    wf_label_message msg = {0};
    wf_status st = wf_label_message_parse(json, len, &msg);
    if (st != WF_OK) {
        return st;
    }
    if (msg.type != WF_LABEL_MESSAGE_LABELS) {
        wf_label_message_free(&msg);
        return WF_ERR_PARSE;
    }

    size_t n = msg.data.labels.count;
    wf_mod_label *labels = n ? (wf_mod_label *)calloc(n, sizeof(*labels)) : NULL;
    if (n && !labels) {
        wf_label_message_free(&msg);
        return WF_ERR_ALLOC;
    }

    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        wf_status cs = wf_label_to_mod_label(&msg.data.labels.items[i],
                                             &labels[count]);
        if (cs == WF_ERR_INVALID_ARG) {
            /* Label without a value: not representable, skip it. */
            continue;
        }
        if (cs != WF_OK) {
            for (size_t j = 0; j < count; j++) {
                wf_lsub_free_one(&labels[j]);
            }
            free(labels);
            wf_label_message_free(&msg);
            *out = NULL;
            *out_count = 0;
            return cs;
        }
        count++;
    }

    wf_label_message_free(&msg);

    if (count != n) {
        wf_mod_label *shrunk =
            (wf_mod_label *)realloc(labels, count ? count * sizeof(*labels)
                                                 : 1);
        if (shrunk) {
            labels = shrunk;
        }
    }

    *out = labels;
    *out_count = count;
    return WF_OK;
}

/* ── agent convenience ────────────────────────────────────────────────────── */

typedef struct {
    wf_agent_label_sub_cb user_cb;
    void *userdata;
    wf_status error;
} wf_lsub_ctx;

static void wf_lsub_forward(wf_lsub_ctx *ctx, const wf_label *l) {
    if (!ctx || !ctx->user_cb || !l) return;
    wf_mod_label m;
    memset(&m, 0, sizeof(m));
    if (wf_label_to_mod_label(l, &m) == WF_OK) {
        ctx->user_cb(&m, ctx->userdata);
    }
    wf_lsub_free_one(&m);
}

static void wf_lsub_on_label(const wf_label *l, void *userdata) {
    wf_lsub_forward((wf_lsub_ctx *)userdata, l);
}

static void wf_lsub_on_neg(const wf_label *l, void *userdata) {
    wf_lsub_forward((wf_lsub_ctx *)userdata, l);
}

wf_status wf_agent_subscribe_labels_typed(wf_agent *agent,
    const char *service, int64_t cursor, int has_cursor,
    wf_agent_label_sub_cb on_label, void *userdata) {
    if (!agent || !service || !service[0] || !on_label) {
        return WF_ERR_INVALID_ARG;
    }
    if (has_cursor && cursor < 0) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);

    /* Resolve a `did:` to its labeler service endpoint, otherwise treat
     * `service` as a base URL directly. */
    char *endpoint = NULL;
    if (strncmp(service, "did:", 4) == 0) {
        if (!agent->client) {
            return WF_ERR_INVALID_ARG;
        }
        wf_status rs = wf_did_resolve_service(agent->client, service,
                                              "AtprotoLabeler", &endpoint);
        if (rs != WF_OK) {
            return rs;
        }
    } else {
        endpoint = wf_lsub_dup(service);
        if (!endpoint) {
            return WF_ERR_ALLOC;
        }
    }

    wf_lsub_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.user_cb = on_label;
    ctx.userdata = userdata;
    ctx.error = WF_OK;

    wf_label_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = endpoint;
    opts.cursor = cursor;
    opts.has_cursor = has_cursor;
    opts.reconnect_delay_ms = 1000;
    opts.on_label = wf_lsub_on_label;
    opts.on_neg = wf_lsub_on_neg;
    opts.userdata = &ctx;

    wf_label_subscribe_handle *handle = NULL;
    wf_status st = wf_label_subscribe_start(&opts, &handle);

    free(endpoint);

    return (ctx.error != WF_OK) ? ctx.error : st;
}
