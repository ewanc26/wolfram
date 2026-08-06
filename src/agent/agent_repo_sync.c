/*
 * agent_repo_sync.c — local repo mirror (offline identity, CAR-backed
 * mirror, optional wf_store persistence) and the com.atproto.sync.* read
 * wrappers, split out of agent.c: a distinct concern from the HTTP
 * request/response convenience wrappers that make up the rest of that file
 * — this subsystem tracks local repo state rather than just relaying a
 * single XRPC call.
 */

#include "wolfram/agent.h"

#include "wolfram/repo.h"
#include "wolfram/store.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include "wolfram/atproto_lex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "_internal.h"

/* Local copy of the small string helper (kept static per TU, matching the
 * rest of src/agent/). */
static char *wf_agent_strdup(const char *s) {
    if (!s) {
        return NULL;
    }

    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

/* ── repo sync: set_did / set_signing_key ──────────────────────────── */

#ifdef WOLFRAM_BUILD_STORE
static wf_status wf_agent_persist_mirror(wf_agent *agent);
#endif

wf_status wf_agent_set_did(wf_agent *agent, const char *did) {
    if (!agent || !did || !wf_syntax_did_is_valid(did))
        return WF_ERR_INVALID_ARG;
    char *copy = wf_agent_strdup(did);
    if (!copy) return WF_ERR_ALLOC;
    free(agent->mirror_did);
    agent->mirror_did = copy;
    return WF_OK;
}

wf_status wf_agent_set_signing_key(wf_agent *agent, const char *key) {
    if (!agent || !key || !key[0]) return WF_ERR_INVALID_ARG;
    char *copy = wf_agent_strdup(key);
    if (!copy) return WF_ERR_ALLOC;
    free(agent->mirror_signing_key);
    agent->mirror_signing_key = copy;
    return WF_OK;
}

/* ── repo sync: seed_repo ──────────────────────────────────────────── */

wf_status wf_agent_seed_repo(wf_agent *agent, const wf_car *car) {
    if (!agent || !car || car->root_count == 0 || !car->roots)
        return WF_ERR_INVALID_ARG;

    wf_car existing = {0};
    /* wf_status status = WF_OK; */

    /* Deep-copy the CAR into agent->mirror. */
    existing.roots = malloc(car->root_count * sizeof(wf_cid));
    if (!existing.roots) return WF_ERR_ALLOC;
    existing.root_count = car->root_count;
    for (size_t i = 0; i < car->root_count; i++)
        existing.roots[i] = car->roots[i];

    if (car->block_count > 0) {
        existing.blocks = calloc(car->block_count, sizeof(wf_car_block));
        if (!existing.blocks) {
            free(existing.roots);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < car->block_count; i++) {
            existing.blocks[i].data = malloc(car->blocks[i].data_len);
            if (!existing.blocks[i].data) {
                for (size_t j = 0; j < i; j++) free(existing.blocks[j].data);
                free(existing.blocks);
                free(existing.roots);
                return WF_ERR_ALLOC;
            }
            memcpy(existing.blocks[i].data, car->blocks[i].data,
                   car->blocks[i].data_len);
            existing.blocks[i].data_len = car->blocks[i].data_len;
            existing.blocks[i].cid = car->blocks[i].cid;
        }
        existing.block_count = car->block_count;
    }

    wf_car_free(&agent->mirror);
    agent->mirror = existing;
#ifdef WOLFRAM_BUILD_STORE
    wf_agent_persist_mirror(agent);
#endif
    return WF_OK;
}

/* ── repo sync: apply_repo_diff ────────────────────────────────────── */

wf_status wf_agent_apply_repo_diff(wf_agent *agent,
                                   const unsigned char *car_bytes,
                                   size_t car_len) {
    if (!agent || !car_bytes || car_len == 0) return WF_ERR_INVALID_ARG;
    if (agent->mirror.root_count != 1 || !agent->mirror.roots ||
        !agent->mirror_did || !agent->mirror_signing_key)
        return WF_ERR_INVALID_ARG;

    wf_car update = {0};
    wf_status status = wf_car_parse(car_bytes, car_len, &update);
    if (status != WF_OK) return status;

    wf_repo_verify_options opts = {
        .expected_did = agent->mirror_did,
        .signing_key = agent->mirror_signing_key,
    };

    wf_repo_diff diff = {0};
    status = wf_repo_diff_verify(&agent->mirror, &agent->mirror.roots[0],
                                 &update, &opts, &diff);
    wf_car_free(&update);
    if (status != WF_OK) return status;

    status = wf_repo_diff_apply(&agent->mirror, &diff);
    wf_repo_diff_free(&diff);
#ifdef WOLFRAM_BUILD_STORE
    wf_agent_persist_mirror(agent);
#endif
    return status;
}

/* ── repo sync: repo_head ──────────────────────────────────────────── */

wf_status wf_agent_repo_head(wf_agent *agent, char **out_head) {
    if (!agent || !out_head) return WF_ERR_INVALID_ARG;
    if (agent->mirror.root_count != 1 || !agent->mirror.roots)
        return WF_ERR_INVALID_ARG;

    *out_head = wf_cid_to_string(&agent->mirror.roots[0]);
    return *out_head ? WF_OK : WF_ERR_ALLOC;
}

/* ── repo sync: invert_repo_operations ─────────────────────────────── */

wf_status wf_agent_invert_repo_operations(wf_agent *agent,
                                          const wf_repo_operation *operations,
                                          size_t count,
                                          wf_repo_operation **out_inverse) {
    (void)agent;
    return wf_repo_operations_invert(operations, count, out_inverse);
}

/* ── repo sync: mirror_get_record ──────────────────────────────────── */

wf_status wf_agent_mirror_get_record(wf_agent *agent, const char *collection,
                                     const char *rkey, unsigned char **out_data,
                                     size_t *out_len) {
    if (!agent || !collection || !rkey || !out_data || !out_len)
        return WF_ERR_INVALID_ARG;
    if (agent->mirror.root_count == 0) return WF_ERR_INVALID_ARG;

    wf_cid record_cid = {0};
    return wf_repo_get_record(&agent->mirror, &agent->mirror.roots[0],
                              collection, rkey, out_data, out_len, &record_cid);
}

/* ── repo sync: persistence bridge ─────────────────────────────────── */

#ifdef WOLFRAM_BUILD_STORE

/* Best-effort: persist the current in-memory mirror HEAD + blocks to the
 * attached store. Failures are intentionally swallowed so the agent keeps
 * working in-memory. */
static wf_status wf_agent_persist_mirror(wf_agent *agent) {
    if (!agent || !agent->store || !agent->mirror_did) return WF_OK;
    if (agent->mirror.root_count != 1 || !agent->mirror.roots) return WF_OK;

    char *head = wf_cid_to_string(&agent->mirror.roots[0]);
    if (!head) return WF_OK;

    wf_status st =
        wf_store_save_mirror_head(agent->store, agent->mirror_did, head);
    free(head);
    if (st != WF_OK) return WF_OK;

    for (size_t i = 0; i < agent->mirror.block_count; i++) {
        wf_car_block *b = &agent->mirror.blocks[i];
        wf_store_save_mirror_block(agent->store, agent->mirror_did,
                                   b->cid.bytes, b->cid.len, b->data,
                                   b->data_len);
    }
    return WF_OK;
}

wf_status wf_agent_attach_store(wf_agent *agent, wf_store *store) {
    if (!agent) return WF_ERR_INVALID_ARG;
    /* Caller retains ownership of `store`; the agent only borrows it. */
    agent->store = store;
    return WF_OK;
}

wf_status wf_agent_mirror_load_from_store(wf_agent *agent) {
    if (!agent || !agent->store || !agent->mirror_did)
        return WF_ERR_INVALID_ARG;

    char *head = NULL;
    wf_status st =
        wf_store_load_mirror_head(agent->store, agent->mirror_did, &head);
    if (st != WF_OK) return st; /* WF_ERR_NOT_FOUND when nothing persisted */

    wf_cid head_cid = {0};
    st = wf_cid_from_string(head, &head_cid);
    free(head);
    if (st != WF_OK) return st;

    wf_cid *cids = NULL;
    size_t count = 0;
    st = wf_store_list_mirror_cids(agent->store, agent->mirror_did, &cids,
                                   &count);
    if (st != WF_OK) return st;

    wf_car mirror = {0};
    mirror.roots = malloc(sizeof(wf_cid));
    if (!mirror.roots) {
        wf_store_mirror_cids_free(cids);
        return WF_ERR_ALLOC;
    }
    mirror.roots[0] = head_cid;
    mirror.root_count = 1;

    if (count > 0) {
        mirror.blocks = calloc(count, sizeof(wf_car_block));
        if (!mirror.blocks) {
            free(mirror.roots);
            wf_store_mirror_cids_free(cids);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < count; i++) {
            uint8_t *block = NULL;
            size_t block_len = 0;
            if (wf_store_load_mirror_block(agent->store, agent->mirror_did,
                                           cids[i].bytes, cids[i].len, &block,
                                           &block_len) == WF_OK) {
                mirror.blocks[mirror.block_count].cid = cids[i];
                mirror.blocks[mirror.block_count].data = block;
                mirror.blocks[mirror.block_count].data_len = block_len;
                mirror.block_count++;
            }
        }
    }

    wf_store_mirror_cids_free(cids);
    wf_car_free(&agent->mirror);
    agent->mirror = mirror;
    return WF_OK;
}

/* Accessor used by the label-persistence helpers (label_persist.c). The
 * store is caller-owned and never freed by the agent. */
wf_store *wf_agent_get_store(wf_agent *agent) {
    return agent ? agent->store : NULL;
}

#endif /* WOLFRAM_BUILD_STORE */

/* ── sync.getBlob ──────────────────────────────────────────────────── */

wf_status wf_agent_sync_get_blob(wf_agent *agent, const char *did,
                                 const char *cid, wf_response *out) {
    if (!agent || !did || !cid || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
        {"cid", cid},
    };
    return wf_xrpc_query_params(agent->client, "com.atproto.sync.getBlob",
                                params, 2, out);
}

/* ── sync.getBlocks ────────────────────────────────────────────────── */

wf_status wf_agent_sync_get_blocks(wf_agent *agent, const char *did,
                                   const char *const *cids, size_t cid_count,
                                   wf_response *out) {
    if (!agent || !did || !cids || cid_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param *params = calloc(cid_count + 1, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;

    params[0].name = "did";
    params[0].value = did;
    for (size_t i = 0; i < cid_count; i++) {
        if (!cids[i] || !cids[i][0]) {
            free(params);
            return WF_ERR_INVALID_ARG;
        }
        params[i + 1].name = "cids";
        params[i + 1].value = cids[i];
    }

    wf_status status =
        wf_xrpc_query_params(agent->client, "com.atproto.sync.getBlocks",
                             params, cid_count + 1, out);
    free(params);
    return status;
}

/* ── sync.getRecord ────────────────────────────────────────────────── */

wf_status wf_agent_sync_get_record(wf_agent *agent, const char *did,
                                   const char *collection, const char *rkey,
                                   wf_response *out) {
    if (!agent || !did || !collection || !rkey || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did) ||
        wf_syntax_nsid_validate(collection) != WF_OK ||
        !wf_syntax_record_key_is_valid(rkey)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
        {"collection", collection},
        {"rkey", rkey},
    };
    return wf_xrpc_query_params(agent->client, "com.atproto.sync.getRecord",
                                params, 3, out);
}

/* Expose the agent's underlying XRPC client. Needed by the typed sync layer
 * (sync_typed.c) which issues raw queries for the JSON sync endpoints
 * (getRepoStatus / getLatestCommit) that have no dedicated agent wrapper. */
wf_xrpc_client *wf_agent_xrpc_client(wf_agent *agent) {
    return agent ? agent->client : NULL;
}

/* ── sync.listBlobs ────────────────────────────────────────────────── */

wf_status wf_agent_sync_list_blobs(wf_agent *agent, const char *did, int limit,
                                   const char *cursor, const char *since,
                                   wf_response *out) {
    if (!agent || !did || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[5];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "did";
    params[param_count].value = did;
    param_count++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    if (since && since[0]) {
        if (!wf_syntax_tid_is_valid(since)) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "since";
        params[param_count].value = since;
        param_count++;
    }

    return wf_xrpc_query_params(agent->client, "com.atproto.sync.listBlobs",
                                params, param_count, out);
}
