/*
 * thread_typed.c — typed parser for app.bsky.feed.getPostThread responses.
 *
 * See include/wolfram/thread_typed.h for the public API and ownership rules.
 */

#include "wolfram/thread_typed.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Maximum recursion depth when parsing nested parent/reply nodes. Real
 * threads are shallow; capping depth avoids unbounded stack use if a
 * (malicious or buggy) payload nests extremely deep. When the cap is hit we
 * simply stop recursing rather than failing the whole parse. */
#define WF_AGENT_THREAD_MAX_DEPTH 64

/* ---- local string/reset helpers (static per TU to avoid linkage conflicts) */

static char *wf_agent_thread_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_agent_thread_set_string(char **dst, const char *src) {
    char *copy = wf_agent_thread_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_agent_thread_profile_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static void wf_agent_thread_post_reset(wf_agent_thread_post *p) {
    if (!p) {
        return;
    }
    free(p->uri);
    free(p->cid);
    wf_agent_thread_profile_reset(&p->author);
    if (p->record) {
        cJSON_Delete(p->record);
    }
    if (p->embed) {
        cJSON_Delete(p->embed);
    }
    free(p->indexed_at);
    memset(p, 0, sizeof(*p));
}

static void wf_agent_thread_node_reset(wf_agent_thread_node *n) {
    if (!n) {
        return;
    }
    free(n->uri);
    wf_agent_thread_post_reset(&n->post);
    if (n->parent) {
        wf_agent_thread_node_reset(n->parent);
        free(n->parent);
    }
    for (size_t i = 0; i < n->replies_count; ++i) {
        wf_agent_thread_node_reset(&n->replies[i]);
    }
    free(n->replies);
    memset(n, 0, sizeof(*n));
}

/* ---- integer extraction (absent/non-number leaves the field untouched) */

static void wf_agent_thread_get_int(cJSON *obj, const char *key, int *out) {
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(it)) {
        double v = it->valuedouble;
        if (v > 2147483647.0) {
            *out = 2147483647;
        } else if (v < -2147483648.0) {
            *out = -2147483648;
        } else {
            *out = (int)v;
        }
    }
}

/* ---- parsing of a single postView into an owned wf_agent_thread_post */

static wf_status wf_agent_thread_parse_post(cJSON *obj,
                                            wf_agent_thread_post *out) {
    wf_status st = WF_OK;

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *author = cJSON_GetObjectItemCaseSensitive(obj, "author");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

    if (cJSON_IsString(uri) && uri->valuestring) {
        st = wf_agent_thread_set_string(&out->uri, uri->valuestring);
    }
    if (st == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        st = wf_agent_thread_set_string(&out->cid, cid->valuestring);
    }
    if (st == WF_OK && cJSON_IsObject(author)) {
        cJSON *d = cJSON_GetObjectItemCaseSensitive(author, "did");
        cJSON *h = cJSON_GetObjectItemCaseSensitive(author, "handle");
        cJSON *n = cJSON_GetObjectItemCaseSensitive(author, "displayName");
        cJSON *a = cJSON_GetObjectItemCaseSensitive(author, "avatar");
        if (cJSON_IsString(d) && d->valuestring) {
            st = wf_agent_thread_set_string(&out->author.did, d->valuestring);
        }
        if (st == WF_OK && cJSON_IsString(h) && h->valuestring) {
            st = wf_agent_thread_set_string(&out->author.handle, h->valuestring);
        }
        if (st == WF_OK && cJSON_IsString(n) && n->valuestring) {
            st = wf_agent_thread_set_string(&out->author.display_name,
                                            n->valuestring);
        }
        if (st == WF_OK && cJSON_IsString(a) && a->valuestring) {
            st = wf_agent_thread_set_string(&out->author.avatar, a->valuestring);
        }
    }
    if (st == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        st = wf_agent_thread_set_string(&out->indexed_at, indexed->valuestring);
    }

    /* Take ownership of the `record` and `embed` subtrees (type `unknown`). */
    if (st == WF_OK) {
        cJSON *record = cJSON_DetachItemFromObject(obj, "record");
        if (record) {
            out->record = record;
        }
    }
    if (st == WF_OK) {
        cJSON *embed = cJSON_DetachItemFromObject(obj, "embed");
        if (embed) {
            out->embed = embed;
        }
    }

    if (st == WF_OK) {
        wf_agent_thread_get_int(obj, "replyCount", &out->reply_count);
        wf_agent_thread_get_int(obj, "repostCount", &out->repost_count);
        wf_agent_thread_get_int(obj, "likeCount", &out->like_count);
        wf_agent_thread_get_int(obj, "quoteCount", &out->quote_count);
    }

    return st;
}

/* ---- recursive node parsing; `depth` bounds recursion. */

static wf_status wf_agent_thread_parse_node(cJSON *obj, int depth,
                                            wf_agent_thread_node *out) {
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }

    cJSON *post = cJSON_GetObjectItemCaseSensitive(obj, "post");
    cJSON *not_found = cJSON_GetObjectItemCaseSensitive(obj, "notFound");
    cJSON *blocked = cJSON_GetObjectItemCaseSensitive(obj, "blocked");

    if (cJSON_IsObject(post)) {
        wf_status st = WF_OK;
        out->kind = WF_AGENT_THREAD_KIND_POST;
        st = wf_agent_thread_parse_post(post, &out->post);
        if (st != WF_OK) {
            wf_agent_thread_node_reset(out);
            return st;
        }

        /* Optional single parent (recursive). */
        if (depth > 0) {
            cJSON *parent = cJSON_GetObjectItemCaseSensitive(obj, "parent");
            if (cJSON_IsObject(parent)) {
                wf_agent_thread_node *pn =
                    (wf_agent_thread_node *)calloc(1, sizeof(*pn));
                if (!pn) {
                    wf_agent_thread_node_reset(out);
                    return WF_ERR_ALLOC;
                }
                st = wf_agent_thread_parse_node(parent, depth - 1, pn);
                if (st != WF_OK) {
                    wf_agent_thread_node_reset(pn);
                    free(pn);
                    wf_agent_thread_node_reset(out);
                    return st;
                }
                out->parent = pn;
            }
        }

        /* Optional array of replies (each recursive). */
        if (st == WF_OK && depth > 0) {
            cJSON *replies = cJSON_GetObjectItemCaseSensitive(obj, "replies");
            if (cJSON_IsArray(replies)) {
                int n = cJSON_GetArraySize(replies);
                if (n > 0) {
                    out->replies =
                        (wf_agent_thread_node *)calloc((size_t)n,
                                                       sizeof(*out->replies));
                    if (!out->replies) {
                        wf_agent_thread_node_reset(out);
                        return WF_ERR_ALLOC;
                    }
                    for (int i = 0; i < n && st == WF_OK; ++i) {
                        cJSON *r = cJSON_GetArrayItem(replies, i);
                        st = wf_agent_thread_parse_node(r, depth - 1,
                                                        &out->replies[i]);
                        if (st == WF_OK) {
                            out->replies_count++;
                        } else {
                            wf_agent_thread_node_reset(&out->replies[i]);
                        }
                    }
                    if (st != WF_OK) {
                        wf_agent_thread_node_reset(out);
                        return st;
                    }
                }
            }
        }

        return WF_OK;
    }

    if (cJSON_IsTrue(not_found)) {
        wf_status st = WF_OK;
        out->kind = WF_AGENT_THREAD_KIND_NOT_FOUND;
        cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
        if (cJSON_IsString(uri) && uri->valuestring) {
            st = wf_agent_thread_set_string(&out->uri, uri->valuestring);
        }
        if (st != WF_OK) {
            wf_agent_thread_node_reset(out);
        }
        return st;
    }

    if (cJSON_IsTrue(blocked)) {
        wf_status st = WF_OK;
        out->kind = WF_AGENT_THREAD_KIND_BLOCKED;
        cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
        if (cJSON_IsString(uri) && uri->valuestring) {
            st = wf_agent_thread_set_string(&out->uri, uri->valuestring);
        }
        if (st != WF_OK) {
            wf_agent_thread_node_reset(out);
        }
        return st;
    }

    return WF_ERR_PARSE;
}

/* ---- public API --------------------------------------------------------- */

wf_status wf_agent_parse_thread(const char *json, size_t json_len,
                                wf_agent_thread *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status st = WF_OK;
    cJSON *thread = cJSON_GetObjectItemCaseSensitive(root, "thread");
    if (!cJSON_IsObject(thread)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    st = wf_agent_thread_parse_node(thread, WF_AGENT_THREAD_MAX_DEPTH,
                                    &out->root);
    if (st == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            st = wf_agent_thread_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (st != WF_OK) {
        wf_agent_thread_node_reset(&out->root);
        free(out->cursor);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return st;
}

void wf_agent_thread_free(wf_agent_thread *thread) {
    if (!thread) {
        return;
    }
    wf_agent_thread_node_reset(&thread->root);
    free(thread->cursor);
    memset(thread, 0, sizeof(*thread));
}
