/*
 * moderation_apply.c — wire the moderation decision engine into the agent.
 *
 * These helpers consume the self-contained decision engine (moderation.h) and
 * the existing agent network calls. No new transport is added: all network
 * I/O goes through wf_agent_get_profile_raw / wf_agent_get_preferences /
 * wf_agent_get_post_thread / wf_agent_get_record.
 *
 * The two `wf_agent_mod_*_subject_from_json` shims are offline-testable: they
 * turn API-shaped cJSON into engine subjects without touching the network.
 */

#include "wolfram/agent.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Literal used to mark a viewer boolean (blockedBy / muted) as truthy.
 * The engine only checks these pointers for non-NULL; they are never freed. */
static const char WF_MOD_TRUE[] = "1";

/* Parse a `viewer` object into a moderation viewer state. Strings are borrowed
 * from `obj`; blockedBy/muted booleans are mapped to the WF_MOD_TRUE literal. */
static void parse_viewer(const cJSON *obj, wf_mod_viewer_state *v) {
    memset(v, 0, sizeof(*v));
    if (!obj) return;

    cJSON *blocking = cJSON_GetObjectItemCaseSensitive(obj, "blocking");
    if (blocking && cJSON_IsString(blocking) && blocking->valuestring) {
        v->blocking = blocking->valuestring;
    }
    cJSON *muted_by_list = cJSON_GetObjectItemCaseSensitive(obj, "mutedByList");
    if (muted_by_list && cJSON_IsString(muted_by_list) && muted_by_list->valuestring) {
        v->muted_by_list = muted_by_list->valuestring;
    }
    cJSON *blocking_by_list = cJSON_GetObjectItemCaseSensitive(obj, "blockingByList");
    if (blocking_by_list && cJSON_IsString(blocking_by_list) && blocking_by_list->valuestring) {
        v->blocking_by_list = blocking_by_list->valuestring;
    }
    cJSON *following = cJSON_GetObjectItemCaseSensitive(obj, "following");
    if (following && cJSON_IsString(following) && following->valuestring) {
        v->following = following->valuestring;
    }
    cJSON *blocked_by = cJSON_GetObjectItemCaseSensitive(obj, "blockedBy");
    if (blocked_by && cJSON_IsTrue(blocked_by)) {
        v->blocked_by = WF_MOD_TRUE;
    }
    cJSON *muted = cJSON_GetObjectItemCaseSensitive(obj, "muted");
    if (muted && cJSON_IsTrue(muted)) {
        v->muted = WF_MOD_TRUE;
    }
}

/* Extract a `labels` array (JSON object found at `key` of `owner`) into an
 * owned wf_mod_label array via the engine's JSON ingestion helper. On WF_OK
 * the caller frees *out_labels with wf_mod_labels_free. A missing/non-array
 * field is treated as "no labels" (WF_OK, *out_labels == NULL). */
static wf_status extract_labels(const cJSON *owner, const char *key,
                                wf_mod_label **out_labels, size_t *out_count) {
    *out_labels = NULL;
    *out_count = 0;
    if (!owner) return WF_OK;

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(owner, key);
    if (!cJSON_IsArray(arr)) return WF_OK;

    char *json = cJSON_PrintUnformatted(arr);
    if (!json) return WF_ERR_ALLOC;

    wf_status st = wf_mod_labels_from_json(out_labels, out_count, json);
    free(json);
    return st;
}

wf_status wf_agent_mod_profile_subject_from_json(const cJSON *obj,
                                                 wf_mod_subject_profile *out,
                                                 wf_mod_label **out_labels,
                                                 size_t *out_label_count) {
    if (!obj || !out || !out_labels || !out_label_count) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    *out_labels = NULL;
    *out_label_count = 0;

    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    if (did && cJSON_IsString(did) && did->valuestring) {
        out->did = did->valuestring;
    }
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    if (handle && cJSON_IsString(handle) && handle->valuestring) {
        out->handle = handle->valuestring;
    }

    cJSON *viewer = cJSON_GetObjectItemCaseSensitive(obj, "viewer");
    parse_viewer(viewer, &out->viewer);

    return extract_labels(obj, "labels", out_labels, out_label_count);
}

wf_status wf_agent_mod_post_subject_from_json(const cJSON *post,
                                              const cJSON *author,
                                              wf_mod_subject_post *out,
                                              wf_mod_label **out_labels,
                                              size_t *out_label_count,
                                              wf_mod_label **out_author_labels,
                                              size_t *out_author_label_count) {
    if (!post || !out || !out_labels || !out_label_count ||
        !out_author_labels || !out_author_label_count) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    *out_labels = NULL;
    *out_label_count = 0;
    *out_author_labels = NULL;
    *out_author_label_count = 0;

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(post, "uri");
    if (uri && cJSON_IsString(uri) && uri->valuestring) {
        out->uri = uri->valuestring;
    }
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(post, "cid");
    if (cid && cJSON_IsString(cid) && cid->valuestring) {
        out->cid = cid->valuestring;
    }

    /* Author profile (borrowed strings + viewer). */
    if (author) {
        cJSON *adid = cJSON_GetObjectItemCaseSensitive(author, "did");
        if (adid && cJSON_IsString(adid) && adid->valuestring) {
            out->author.did = adid->valuestring;
        }
        cJSON *ahandle = cJSON_GetObjectItemCaseSensitive(author, "handle");
        if (ahandle && cJSON_IsString(ahandle) && ahandle->valuestring) {
            out->author.handle = ahandle->valuestring;
        }
        cJSON *aviewer = cJSON_GetObjectItemCaseSensitive(author, "viewer");
        parse_viewer(aviewer, &out->author.viewer);
    }

    /* Post text lives in record.text. */
    cJSON *record = cJSON_GetObjectItemCaseSensitive(post, "record");
    if (record) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(record, "text");
        if (text && cJSON_IsString(text) && text->valuestring) {
            out->text = text->valuestring;
        }
    }

    /* Embed: capture $type and a quoted/embedded record URI when present. */
    cJSON *embed = cJSON_GetObjectItemCaseSensitive(post, "embed");
    if (embed) {
        cJSON *type = cJSON_GetObjectItemCaseSensitive(embed, "$type");
        if (type && cJSON_IsString(type) && type->valuestring) {
            out->embed_type = type->valuestring;
        }
        cJSON *emb_record = cJSON_GetObjectItemCaseSensitive(embed, "record");
        if (emb_record) {
            cJSON *emb_uri = cJSON_GetObjectItemCaseSensitive(emb_record, "uri");
            if (emb_uri && cJSON_IsString(emb_uri) && emb_uri->valuestring) {
                out->embed_uri = emb_uri->valuestring;
            }
        }
    }

    wf_status st = extract_labels(post, "labels", out_labels, out_label_count);
    if (st != WF_OK) return st;

    if (author) {
        st = extract_labels(author, "labels", out_author_labels,
                            out_author_label_count);
        if (st != WF_OK) {
            wf_mod_labels_free(*out_labels, *out_label_count);
            *out_labels = NULL;
            *out_label_count = 0;
        }
    }
    return st;
}

/* Append `bcount` label defs owned by `batch` into `out->label_defs`, taking
 * over ownership of the batch's internal allocations and freeing only the
 * batch's outer array. */
static wf_status append_label_defs(wf_mod_opts *out,
                                   wf_mod_label_def *batch, size_t bcount) {
    if (bcount == 0) return WF_OK;

    wf_mod_label_def *grown = realloc(out->label_defs,
        (out->label_def_count + bcount) * sizeof(wf_mod_label_def));
    if (!grown) {
        return WF_ERR_ALLOC;
    }
    memcpy(grown + out->label_def_count, batch,
           bcount * sizeof(wf_mod_label_def));
    out->label_def_count += bcount;
    out->label_defs = grown;
    /* Defs' internals (identifier/defined_by) are now owned by `grown`; only
     * free the now-empty outer array of the batch. */
    free(batch);
    return WF_OK;
}

wf_status wf_agent_moderate_init_opts(wf_agent *agent, wf_mod_opts *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->user_did = wf_agent_get_did(agent);

    char *prefs_json = NULL;
    wf_status status = wf_agent_get_preferences(agent, &prefs_json);
    if (status == WF_OK && prefs_json) {
        wf_mod_prefs_from_json(&out->prefs, prefs_json);
        free(prefs_json);
    }

    /* Resolve label value definitions for each configured labeler. This is
     * best-effort: a labeler record we cannot fetch is simply skipped, leaving
     * its labels without a definition (the engine ignores unrecognised ones). */
    for (size_t i = 0; i < out->prefs.labeler_count; i++) {
        const char *did = out->prefs.labelers[i].did;
        if (!did) continue;

        wf_response res = {0};
        wf_status rs = wf_agent_get_record(agent, "app.bsky.labeler.service",
                                           did, &res);
        if (rs != WF_OK) continue;

        wf_mod_label_def *batch = NULL;
        size_t bcount = 0;
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        if (root) {
            cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
            cJSON *policies = value
                ? cJSON_GetObjectItemCaseSensitive(value, "policies") : NULL;
            cJSON *defs = policies
                ? cJSON_GetObjectItemCaseSensitive(policies, "labelValueDefinitions")
                : NULL;
            if (cJSON_IsArray(defs)) {
                char *defs_json = cJSON_PrintUnformatted(defs);
                if (defs_json) {
                    /* wf_mod_label_defs_from_labeler expects an object wrapping
                     * the array (labelValueDefinitions or policies.*). */
                    size_t need = strlen(defs_json) + 64;
                    char *wrapped = (char *)malloc(need);
                    if (wrapped) {
                        snprintf(wrapped, need,
                                 "{\"labelValueDefinitions\":%s}", defs_json);
                        wf_mod_label_defs_from_labeler(did, wrapped, &batch, &bcount);
                        free(wrapped);
                    }
                    free(defs_json);
                }
            }
            cJSON_Delete(root);
        }
        wf_response_free(&res);

        if (batch) {
            wf_status as = append_label_defs(out, batch, bcount);
            if (as != WF_OK) {
                wf_mod_label_defs_free(out->label_defs, out->label_def_count);
                out->label_defs = NULL;
                out->label_def_count = 0;
                wf_mod_prefs_free(&out->prefs);
                return as;
            }
        }
    }

    return WF_OK;
}

wf_status wf_agent_moderate_profile(wf_agent *agent, const char *actor,
                                    wf_mod_decision **out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;

    wf_response res = {0};
    wf_status status = wf_agent_get_profile_raw(agent, actor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_mod_decision *dec = NULL;
    wf_mod_subject_profile subj = {0};
    wf_mod_label *labels = NULL;
    size_t label_count = 0;
    wf_mod_opts opts = {0};

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!root) {
        status = WF_ERR_PARSE;
        goto done;
    }

    status = wf_agent_mod_profile_subject_from_json(root, &subj,
                                                    &labels, &label_count);
    if (status != WF_OK) {
        goto done;
    }
    subj.labels = labels;
    subj.label_count = label_count;

    status = wf_agent_moderate_init_opts(agent, &opts);
    if (status != WF_OK) {
        goto done;
    }

    dec = (wf_mod_decision *)calloc(1, sizeof(wf_mod_decision));
    if (!dec) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    wf_mod_decision acc = {0}, prof = {0};
    status = wf_mod_decide_account(&acc, &subj, &opts);
    if (status != WF_OK) {
        wf_mod_decision_free(&acc);
        goto done;
    }
    status = wf_mod_decide_profile(&prof, &subj, &opts);
    if (status != WF_OK) {
        wf_mod_decision_free(&acc);
        wf_mod_decision_free(&prof);
        goto done;
    }
    status = wf_mod_decision_merge(dec, &acc, &prof);
    wf_mod_decision_free(&acc);
    wf_mod_decision_free(&prof);
    if (status != WF_OK) {
        goto done;
    }

    *out = dec;
    dec = NULL;

done:
    if (root) cJSON_Delete(root);
    if (labels) wf_mod_labels_free(labels, label_count);
    if (opts.label_def_count) wf_mod_label_defs_free(opts.label_defs, opts.label_def_count);
    wf_mod_prefs_free(&opts.prefs);
    wf_response_free(&res);
    free(dec);
    return status;
}

wf_status wf_agent_moderate_post(wf_agent *agent, const char *uri,
                                 wf_mod_decision **out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;

    wf_response res = {0};
    wf_status status = wf_agent_get_post_thread(agent, uri, 0, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_mod_decision *dec = NULL;
    wf_mod_subject_post subj = {0};
    wf_mod_label *labels = NULL, *author_labels = NULL;
    size_t label_count = 0, author_label_count = 0;
    wf_mod_opts opts = {0};

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!root) {
        status = WF_ERR_PARSE;
        goto done;
    }

    cJSON *thread = cJSON_GetObjectItemCaseSensitive(root, "thread");
    if (!thread) {
        status = WF_ERR_PARSE;
        goto done;
    }
    cJSON *post = cJSON_GetObjectItemCaseSensitive(thread, "post");
    if (!post) post = thread;

    cJSON *author = cJSON_GetObjectItemCaseSensitive(post, "author");

    status = wf_agent_mod_post_subject_from_json(post, author, &subj,
                                                 &labels, &label_count,
                                                 &author_labels,
                                                 &author_label_count);
    if (status != WF_OK) {
        goto done;
    }
    subj.labels = labels;
    subj.label_count = label_count;
    subj.author.labels = author_labels;
    subj.author.label_count = author_label_count;

    status = wf_agent_moderate_init_opts(agent, &opts);
    if (status != WF_OK) {
        goto done;
    }

    dec = (wf_mod_decision *)calloc(1, sizeof(wf_mod_decision));
    if (!dec) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    status = wf_mod_decide_post(dec, &subj, &opts);
    if (status != WF_OK) {
        goto done;
    }

    *out = dec;
    dec = NULL;

done:
    if (root) cJSON_Delete(root);
    if (labels) wf_mod_labels_free(labels, label_count);
    if (author_labels) wf_mod_labels_free(author_labels, author_label_count);
    if (opts.label_def_count) wf_mod_label_defs_free(opts.label_defs, opts.label_def_count);
    wf_mod_prefs_free(&opts.prefs);
    wf_response_free(&res);
    free(dec);
    return status;
}
