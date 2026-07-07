/*
 * label_persist.c — bridge persisted labels into the agent's moderation.
 *
 * Persists moderation labels to the optional SQLite wf_store and reloads them
 * into the agent's moderation context so that wf_agent_moderate_profile /
 * wf_agent_moderate_post consult them in addition to live API data.
 *
 * Best-effort: when no store is attached every function here is a quiet no-op
 * and moderation falls back to live API data only. The agent never frees the
 * attached store; the caller owns it. The loaded labels are agent-owned and
 * freed on wf_agent_free.
 *
 * This module only CONSUMES the moderation engine (moderation.h) and the
 * store (store.h); it never modifies either.
 */

#ifdef WOLFRAM_BUILD_STORE

#include "wolfram/agent.h"
#include "wolfram/store.h"
#include "wolfram/moderation.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

#include "_internal.h"

/* Transfer `bcount` labels owned by `batch` into the agent's persisted-label
 * array, taking over ownership of each label's strings. On allocation failure
 * the batch is released (including its strings) and WF_ERR_ALLOC is returned;
 * the agent's existing labels are left intact. */
static wf_status persist_append(wf_agent *agent, wf_mod_label *batch,
                                size_t bcount) {
    if (bcount == 0) {
        free(batch);
        return WF_OK;
    }

    size_t base = agent->persisted_label_count;
    wf_mod_label *grown = realloc(agent->persisted_labels,
        (base + bcount) * sizeof(wf_mod_label));
    if (!grown) {
        wf_mod_labels_free(batch, bcount);
        return WF_ERR_ALLOC;
    }

    /* Move each label's owning pointers into the grown array, then free the
     * now-empty outer batch array (not the strings). */
    memcpy(grown + base, batch, bcount * sizeof(wf_mod_label));
    free(batch);

    agent->persisted_labels = grown;
    agent->persisted_label_count = base + bcount;
    return WF_OK;
}

wf_status wf_agent_attach_label_store(wf_agent *agent, wf_store *store) {
    /* The label store and the repo-mirror store are the same attached store. */
    return wf_agent_attach_store(agent, store);
}

wf_status wf_agent_persist_label(wf_agent *agent, const wf_mod_label *label) {
    if (!agent || !label) return WF_ERR_INVALID_ARG;

    wf_store *store = wf_agent_get_store(agent);
    if (!store) return WF_OK;

    return wf_store_save_label(store, label->uri, NULL, label->val,
                               label->src, label->cts);
}

wf_status wf_agent_load_labels_from_store(wf_agent *agent) {
    if (!agent) return WF_ERR_INVALID_ARG;

    wf_store *store = wf_agent_get_store(agent);
    if (!store) return WF_OK;

    /* Drop any previously loaded labels before reloading. */
    if (agent->persisted_labels) {
        wf_mod_labels_free(agent->persisted_labels, agent->persisted_label_count);
        agent->persisted_labels = NULL;
        agent->persisted_label_count = 0;
    }

    wf_status st = WF_OK;

    /* 1. The agent's own DID (account + profile labels are keyed by it).
     * Prefer the logged-in session DID; fall back to the mirror DID set via
     * wf_agent_set_did so offline label loading still works. */
    const char *did = wf_agent_get_did(agent);
    if (!did) did = agent->mirror_did;
    if (did) {
        wf_mod_label *batch = NULL;
        size_t bcount = 0;
        wf_status ls = wf_store_load_labels(store, did, &batch, &bcount);
        if (ls == WF_OK) {
            st = persist_append(agent, batch, bcount);
            if (st != WF_OK) return st;
        }
    }

    /* 2. Best-effort: any followed/known DIDs we can currently reach. A
     *    network failure or empty result simply yields no extra labels. */
    if (did) {
        wf_response res = {0};
        wf_status fs = wf_agent_get_follows(agent, did, 100, NULL, &res);
        if (fs == WF_OK) {
            cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
            if (root) {
                cJSON *follows = cJSON_GetObjectItemCaseSensitive(root, "follows");
                if (cJSON_IsArray(follows)) {
                    cJSON *item = NULL;
                    cJSON_ArrayForEach(item, follows) {
                        cJSON *fdid = cJSON_GetObjectItemCaseSensitive(item, "did");
                        if (!cJSON_IsString(fdid) || !fdid->valuestring) continue;
                        wf_mod_label *batch = NULL;
                        size_t bcount = 0;
                        wf_status ls = wf_store_load_labels(store,
                            fdid->valuestring, &batch, &bcount);
                        if (ls == WF_OK) {
                            wf_status as = persist_append(agent, batch, bcount);
                            if (as != WF_OK) {
                                cJSON_Delete(root);
                                wf_response_free(&res);
                                return as;
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
            wf_response_free(&res);
        }
    }

    return WF_OK;
}

const wf_mod_label *wf_agent_get_persisted_labels(const wf_agent *agent,
                                                 size_t *out_count) {
    if (!agent || !out_count) {
        return NULL;
    }
    *out_count = agent->persisted_label_count;
    return agent->persisted_labels;
}

#endif /* WOLFRAM_BUILD_STORE */
