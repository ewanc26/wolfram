/*
 * lexicon_typed.c — owned typed parser + agent convenience wrapper for the
 * com.atproto.lexicon.resolveLexicon query. See include/wolfram/lexicon_typed.h
 * for the public API, the authoritative wire format, and ownership rules.
 *
 * Mirrors the other *_typed modules: static strdup/set_string/reset helpers,
 * owned strings, a detached `schema` cJSON subtree for the open lexicon schema,
 * and full cleanup on the first error. The agent wrapper calls the generated
 * lex wrapper directly after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/lexicon_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* ---- local string/reset helpers ---- */

static char *wf_lexicon_strdup(const char *s) {
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

static wf_status wf_lexicon_set_string(char **dst, const char *src) {
    char *copy = wf_lexicon_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_lexicon_resolved_reset(wf_lexicon_resolved *r) {
    if (!r) {
        return;
    }
    free(r->cid);
    free(r->uri);
    if (r->schema) {
        cJSON_Delete(r->schema);
    }
    memset(r, 0, sizeof(*r));
}

wf_status wf_lexicon_parse_resolve(const char *json, size_t json_len,
                                   wf_lexicon_resolved *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(root, "uri");
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");

    if (cJSON_IsString(cid) && cid->valuestring) {
        status = wf_lexicon_set_string(&out->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(uri) && uri->valuestring) {
        status = wf_lexicon_set_string(&out->uri, uri->valuestring);
    }
    if (status == WF_OK && schema != NULL) {
        out->schema = cJSON_DetachItemFromObject(root, "schema");
    }

    if (status == WF_OK) {
        /* Keep any remaining top-level fields in `extra` would require a slot;
         * the schema is the only open field we surface, so just free the root
         * now that its known children have been detached. */
        cJSON_DetachItemFromObject(root, "cid");
        cJSON_DetachItemFromObject(root, "uri");
        cJSON_Delete(root);
    } else {
        wf_lexicon_resolved_reset(out);
        cJSON_Delete(root);
    }
    return status;
}

void wf_lexicon_resolved_free(wf_lexicon_resolved *r) {
    if (!r) {
        return;
    }
    wf_lexicon_resolved_reset(r);
}

wf_status wf_agent_resolve_lexicon_typed(wf_agent *agent, const char *nsid,
                                         wf_lexicon_resolved *out) {
    if (!agent || !agent->client || !nsid || !nsid[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lexicon_resolved result = {0};
    wf_lex_com_atproto_lexicon_resolve_lexicon_main_params params = {0};
    params.nsid = nsid;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_lexicon_resolve_lexicon_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_lexicon_parse_resolve(res.body, res.body_len, &result);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    } else {
        wf_lexicon_resolved_free(&result);
    }
    return status;
}
