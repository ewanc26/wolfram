/*
 * sync_typed.c — owned typed parsers + agent wrappers for the
 * com.atproto.sync query responses. See include/wolfram/sync_typed.h for the
 * public API and ownership rules. Follows feed_typed.c / actor_typed.c:
 * static strdup/set_string/reset helpers, owned strings/bytes, full cleanup
 * on the first error. Reuses the repo CAR machinery (wf_car_parse,
 * wf_repo_get_record) rather than hand-rolling CBOR/CID logic.
 */

#include "wolfram/sync_typed.h"

#include "wolfram/repo.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Defined in src/agent/agent.c; returns the agent's underlying xrpc client
 * so the JSON sync endpoints (no dedicated agent wrapper) can be queried. */
wf_xrpc_client *wf_agent_xrpc_client(wf_agent *agent);

/* ── local string/byte helpers ──────────────────────────────────── */

static char *wf_sync_strdup(const char *s) {
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

static wf_status wf_sync_set_string(char **dst, const char *src) {
    char *copy = wf_sync_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static uint8_t *wf_sync_bytes_dup(const unsigned char *src, size_t len) {
    if (len == 0) {
        return NULL;
    }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (copy) {
        memcpy(copy, src, len);
    }
    return copy;
}

/* ── com.atproto.sync.getRepoStatus ────────────────────────────── */

void wf_sync_repo_status_typed_free(wf_sync_repo_status_typed *s) {
    if (!s) {
        return;
    }
    free(s->did);
    free(s->status);
    free(s->rev);
    memset(s, 0, sizeof(*s));
}

wf_status wf_sync_repo_status_typed_parse(const char *json, size_t json_len,
                                   wf_sync_repo_status_typed *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    cJSON *status_f = cJSON_GetObjectItemCaseSensitive(root, "status");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");

    if (!(cJSON_IsString(did) && did->valuestring && did->valuestring[0])) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && !cJSON_IsBool(active)) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK) {
        status = wf_sync_set_string(&out->did, did->valuestring);
    }
    if (status == WF_OK) {
        out->active = cJSON_IsTrue(active) ? 1 : 0;
    }
    if (status == WF_OK && cJSON_IsString(status_f) && status_f->valuestring) {
        status = wf_sync_set_string(&out->status, status_f->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring) {
        status = wf_sync_set_string(&out->rev, rev->valuestring);
    }

    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_sync_repo_status_typed_free(out);
    }
    return status;
}

/* ── com.atproto.sync.getLatestCommit ──────────────────────────── */

void wf_sync_latest_commit_free(wf_sync_latest_commit *c) {
    if (!c) {
        return;
    }
    free(c->cid);
    free(c->rev);
    memset(c, 0, sizeof(*c));
}

wf_status wf_sync_latest_commit_parse(const char *json, size_t json_len,
                                     wf_sync_latest_commit *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");

    if (!(cJSON_IsString(cid) && cid->valuestring && cid->valuestring[0])) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK &&
        !(cJSON_IsString(rev) && rev->valuestring && rev->valuestring[0])) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK) {
        status = wf_sync_set_string(&out->cid, cid->valuestring);
    }
    if (status == WF_OK) {
        status = wf_sync_set_string(&out->rev, rev->valuestring);
    }

    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_sync_latest_commit_free(out);
    }
    return status;
}

/* ── com.atproto.sync.getBlocks (CAR -> block list) ────────────── */

void wf_sync_block_list_free(wf_sync_block_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].cid);
        free(list->items[i].value);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

wf_status wf_sync_block_list_parse_car(const unsigned char *car_bytes,
                                      size_t len,
                                      wf_sync_block_list *out) {
    if (!car_bytes || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_car car;
    memset(&car, 0, sizeof(car));
    wf_status status = wf_car_parse(car_bytes, len, &car);
    if (status != WF_OK) {
        return status;
    }

    wf_sync_block *items = NULL;
    if (car.block_count > 0) {
        items = (wf_sync_block *)calloc(car.block_count, sizeof(*items));
        if (!items) {
            wf_car_free(&car);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < car.block_count && status == WF_OK; ++i) {
        wf_sync_block *b = &items[i];
        char *cid = wf_cid_to_string(&car.blocks[i].cid);
        if (!cid) {
            status = WF_ERR_ALLOC;
            break;
        }
        b->value = wf_sync_bytes_dup(car.blocks[i].data,
                                      car.blocks[i].data_len);
        if (car.blocks[i].data_len > 0 && !b->value) {
            free(cid);
            status = WF_ERR_ALLOC;
            break;
        }
        b->cid = cid;
        b->value_len = car.blocks[i].data_len;
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = car.block_count;
    } else {
        for (size_t i = 0; i < car.block_count; ++i) {
            free(items[i].cid);
            free(items[i].value);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    wf_car_free(&car);
    return status;
}

/* ── com.atproto.sync.getRecord (CAR -> record CBOR) ──────────── */

void wf_sync_record_free(wf_sync_record *r) {
    if (!r) {
        return;
    }
    free(r->record_cbor);
    free(r->repo_rev);
    memset(r, 0, sizeof(*r));
}

wf_status wf_sync_record_parse_car(const unsigned char *car_bytes,
                                  size_t len,
                                  const char *collection,
                                  const char *rkey,
                                  wf_sync_record *out) {
    if (!car_bytes || !out || !collection || !collection[0] ||
        !rkey || !rkey[0]) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_car car;
    memset(&car, 0, sizeof(car));
    wf_status status = wf_car_parse(car_bytes, len, &car);
    if (status != WF_OK) {
        return status;
    }

    /* The CAR root is the repo commit. */
    if (car.root_count == 0) {
        wf_car_free(&car);
        return WF_ERR_PARSE;
    }

    wf_car_block *commit_block = wf_car_find_block(&car, &car.roots[0]);
    if (!commit_block) {
        wf_car_free(&car);
        return WF_ERR_PARSE;
    }

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    status = wf_commit_parse(commit_block->data, commit_block->data_len,
                             &commit);
    if (status != WF_OK) {
        wf_car_free(&car);
        return status;
    }

    if (commit.rev[0]) {
        status = wf_sync_set_string(&out->repo_rev, commit.rev);
        if (status != WF_OK) {
            wf_car_free(&car);
            return status;
        }
    }

    /* Extract the requested record (reuses MST traversal in repo.c). */
    unsigned char *data = NULL;
    size_t data_len = 0;
    wf_cid rec_cid;
    memset(&rec_cid, 0, sizeof(rec_cid));
    status = wf_repo_get_record(&car, &car.roots[0], collection, rkey,
                                &data, &data_len, &rec_cid);
    if (status == WF_OK) {
        /* wf_repo_get_record owns `data` to the caller; take it directly. */
        out->record_cbor = data;
        out->record_len = data_len;
    } else if (status == WF_ERR_NOT_FOUND) {
        /* Non-existence proof: commit+rev are valid, record is absent. */
        out->record_cbor = NULL;
        out->record_len = 0;
        status = WF_OK;
    } else {
        wf_car_free(&car);
        wf_sync_record_free(out);
        return status;
    }

    wf_car_free(&car);
    return status;
}

/* ── com.atproto.sync.getHead (deprecated) ─────────────────────── */

void wf_sync_head_typed_free(wf_sync_head_typed *h) {
    if (!h) {
        return;
    }
    free(h->root);
    memset(h, 0, sizeof(*h));
}

wf_status wf_sync_head_typed_parse(const char *json, size_t json_len,
                                   wf_sync_head_typed *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *root_f = cJSON_GetObjectItemCaseSensitive(root, "root");
    if (!(cJSON_IsString(root_f) && root_f->valuestring &&
          root_f->valuestring[0])) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK) {
        status = wf_sync_set_string(&out->root, root_f->valuestring);
    }

    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_sync_head_typed_free(out);
    }
    return status;
}

/* ── Agent convenience wrappers ─────────────────────────────────── */

wf_status wf_agent_get_repo_status_typed(wf_agent *agent, const char *did,
                                        wf_sync_repo_status_typed *out) {
    if (!agent || !did || !did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_client *client = wf_agent_xrpc_client(agent);
    if (!client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
    };
    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(client,
                                            "com.atproto.sync.getRepoStatus",
                                            params, 1, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_sync_repo_status_typed_parse(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_latest_commit_typed(wf_agent *agent, const char *did,
                                           wf_sync_latest_commit *out) {
    if (!agent || !did || !did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_client *client = wf_agent_xrpc_client(agent);
    if (!client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
    };
    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(client,
                                            "com.atproto.sync.getLatestCommit",
                                            params, 1, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_sync_latest_commit_parse(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_head_typed(wf_agent *agent, const char *did,
                                  wf_sync_head_typed *out) {
    if (!agent || !did || !did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_client *client = wf_agent_xrpc_client(agent);
    if (!client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
    };
    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(client,
                                            "com.atproto.sync.getHead",
                                            params, 1, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_sync_head_typed_parse(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_sync_get_blocks_typed(wf_agent *agent, const char *did,
                                    const char *const *cids, size_t n,
                                    wf_sync_block_list *out) {
    if (!agent || !did || !did[0] || !cids || n == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_sync_get_blocks(agent, did, cids, n, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_sync_block_list_parse_car((const unsigned char *)res.body,
                                          res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_sync_get_record_typed(wf_agent *agent, const char *did,
                                    const char *collection, const char *rkey,
                                    wf_sync_record *out) {
    if (!agent || !did || !did[0] || !collection || !collection[0] ||
        !rkey || !rkey[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_sync_get_record(agent, did, collection, rkey,
                                                &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_sync_record_parse_car((const unsigned char *)res.body,
                                       res.body_len, collection, rkey, out);
    wf_response_free(&res);
    return status;
}

/* com.atproto.sync.getBlob — query returning the raw blob bytes. */
wf_status wf_agent_get_blob_typed(wf_agent *agent, const char *did,
                                  const char *cid, uint8_t **out_data,
                                  size_t *out_len) {
    if (!agent || !did || !did[0] || !cid || !cid[0] || !out_data ||
        !out_len) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_client *client = wf_agent_xrpc_client(agent);
    if (!client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
        {"cid", cid},
    };
    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(client,
                                            "com.atproto.sync.getBlob",
                                            params, 2, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    uint8_t *buf = NULL;
    if (res.body_len > 0) {
        buf = (uint8_t *)malloc(res.body_len);
        if (!buf) {
            wf_response_free(&res);
            return WF_ERR_ALLOC;
        }
        memcpy(buf, res.body, res.body_len);
    }
    *out_data = buf;
    *out_len = res.body_len;
    wf_response_free(&res);
    return WF_OK;
}
