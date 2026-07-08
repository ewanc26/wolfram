/*
 * label_query.c — request/response label endpoints:
 *   - com.atproto.label.queryLabels (generated lexicon wrapper)
 *   - com.atproto.label.getLabels  (hand-rolled authenticated XRPC GET)
 *   - wf_label_parse_query         (owned typed parser)
 *   - wf_agent_query_labels_typed  (fetch + persist convenience wrapper)
 *
 * All network I/O goes through the low-level XRPC client / generated lexicon
 * wrappers; this module only builds parameters, decodes JSON, and persists.
 */

#include "wolfram/label.h"
#include "wolfram/label_typed.h"
#include "wolfram/atproto_lex.h"

#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* com.atproto.label.getLabels has no generated lexicon wrapper; issue it
 * directly as an authenticated XRPC GET. */
#define WF_LABEL_GET_LABELS_NSID "com.atproto.label.getLabels"

/* Local string helpers (mirror conventions in moderation.c / feed_typed.c). */
static char *wf_label_dup(const char *s) {
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

/* queryLabels — use the generated lexicon wrapper. */
wf_status wf_label_query_labels(wf_xrpc_client *client,
                                const char *const *uris, size_t uri_count,
                                const char *const *sources, size_t source_count,
                                int limit, wf_response *out) {
    if (!client || !uris || uri_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_label_query_labels_main_params params;
    memset(&params, 0, sizeof(params));

    params.uri_patterns.items = uris;
    params.uri_patterns.count = uri_count;

    if (sources && source_count > 0) {
        params.has_sources = true;
        params.sources.items = sources;
        params.sources.count = source_count;
    }

    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }

    return wf_lex_com_atproto_label_query_labels_main_call(client, &params, out);
}

/* getLabels — hand-rolled authenticated XRPC GET (no generated wrapper). */
wf_status wf_label_get_labels(wf_xrpc_client *client,
                              const char *uri,
                              const char *const *sources, size_t source_count,
                              wf_response *out) {
    if (!client || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    size_t param_count = 1 + (sources ? source_count : 0);
    wf_xrpc_param *params = (wf_xrpc_param *)calloc(param_count, sizeof(*params));
    if (!params) {
        return WF_ERR_ALLOC;
    }

    params[0].name = "uri";
    params[0].value = uri;
    for (size_t i = 0; i < (sources ? source_count : 0); i++) {
        params[1 + i].name = "sources";
        params[1 + i].value = sources[i];
    }

    wf_status status = wf_xrpc_query_params(client, WF_LABEL_GET_LABELS_NSID,
                                             params, param_count, out);

    free(params);
    return status;
}

/* Parse a queryLabels/getLabels JSON body into owned wf_mod_label structs. */
wf_status wf_label_parse_query(const char *json, size_t len,
                               wf_mod_label **out, size_t *out_count) {
    if (!out || !out_count || !json) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    *out_count = 0;

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *arr = root;
    cJSON *labels_field = cJSON_GetObjectItemCaseSensitive(root, "labels");
    if (cJSON_IsArray(labels_field)) {
        arr = labels_field;
    }
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status st = WF_OK;
    size_t cap = 0;
    wf_mod_label *labels = NULL;
    size_t count = 0;
    cJSON *l;
    cJSON_ArrayForEach(l, arr) {
        const char *src = cJSON_IsString(
            cJSON_GetObjectItemCaseSensitive(l, "src"))
            ? cJSON_GetObjectItemCaseSensitive(l, "src")->valuestring : NULL;
        const char *uri = cJSON_IsString(
            cJSON_GetObjectItemCaseSensitive(l, "uri"))
            ? cJSON_GetObjectItemCaseSensitive(l, "uri")->valuestring : NULL;
        const char *val = cJSON_IsString(
            cJSON_GetObjectItemCaseSensitive(l, "val"))
            ? cJSON_GetObjectItemCaseSensitive(l, "val")->valuestring : NULL;
        const char *cts = cJSON_IsString(
            cJSON_GetObjectItemCaseSensitive(l, "cts"))
            ? cJSON_GetObjectItemCaseSensitive(l, "cts")->valuestring : NULL;

        /* Labels without a value are not representable; skip them. */
        if (!val) {
            continue;
        }

        if (count >= cap) {
            size_t new_cap = (cap == 0) ? 4 : cap * 2;
            wf_mod_label *grown =
                (wf_mod_label *)realloc(labels, new_cap * sizeof(*labels));
            if (!grown) {
                st = WF_ERR_ALLOC;
                goto done;
            }
            labels = grown;
            cap = new_cap;
        }

        wf_mod_label *lab = &labels[count];
        memset(lab, 0, sizeof(*lab));
        lab->src = wf_label_dup(src);
        lab->uri = wf_label_dup(uri);
        lab->val = wf_label_dup(val);
        lab->cts = wf_label_dup(cts);

        if (!lab->val || (src && !lab->src) || (uri && !lab->uri) ||
            (cts && !lab->cts)) {
            st = WF_ERR_ALLOC;
            goto done;
        }
        count++;
    }

done:
    cJSON_Delete(root);
    if (st != WF_OK) {
        if (labels) {
            for (size_t i = 0; i < count; i++) {
                free(labels[i].src);
                free(labels[i].uri);
                free(labels[i].val);
                free(labels[i].cts);
            }
            free(labels);
        }
        *out = NULL;
        *out_count = 0;
    } else {
        *out = labels;
        *out_count = count;
    }
    return st;
}

/* Fetch labels through the agent's authenticated client, parse, and persist. */
wf_status wf_agent_query_labels_typed(wf_agent *agent,
                                      const char *const *uris, size_t uri_count,
                                      const char *const *sources,
                                      size_t source_count,
                                      wf_mod_label **out, size_t *out_count) {
    if (!agent || !out || !out_count || !uris || uri_count == 0) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    *out_count = 0;

    wf_agent_sync_auth(agent);

    wf_response res = {0};
    wf_status st = wf_label_query_labels(agent->client, uris, uri_count,
                                         sources, source_count, 0, &res);
    if (st != WF_OK) {
        wf_response_free(&res);
        return st;
    }

    st = wf_label_parse_query(res.body, res.body_len, out, out_count);
    wf_response_free(&res);
    if (st != WF_OK) {
        return st;
    }

    /* Persist each parsed label into the agent's attached store. Persistence
     * is best-effort and only compiled in when the store module is enabled. */
#ifdef WOLFRAM_BUILD_STORE
    for (size_t i = 0; i < *out_count; i++) {
        wf_agent_persist_label(agent, &(*out)[i]);
    }
#else
    (void)sources;
    (void)source_count;
#endif

    return WF_OK;
}
