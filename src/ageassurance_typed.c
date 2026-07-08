/*
 * ageassurance_typed.c — typed parsers + agent wrappers for
 * app.bsky.ageassurance (see ageassurance_typed.h).
 *
 * Mirrors feed_typed.c: static strdup/set_string/reset helpers, ownership via
 * cJSON_DetachItemFromObject, and full cleanup on the first error.
 */

#include "wolfram/ageassurance_typed.h"

#include "wolfram/atproto_lex.h"

#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_aa_strdup(const char *s) {
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

static wf_status wf_aa_set_string(char **dst, const char *src) {
    char *copy = wf_aa_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_aa_begin_reset(wf_ageassurance_begin *b) {
    if (!b) {
        return;
    }
    free(b->last_initiated_at);
    free(b->status);
    free(b->access);
    memset(b, 0, sizeof(*b));
}

static void wf_aa_metadata_reset(wf_ageassurance_metadata *m) {
    if (!m) {
        return;
    }
    free(m->account_created_at);
    memset(m, 0, sizeof(*m));
}

static void wf_aa_config_reset(wf_ageassurance_config *c) {
    if (!c) {
        return;
    }
    if (c->regions) {
        cJSON_Delete(c->regions);
    }
    memset(c, 0, sizeof(*c));
}

static void wf_aa_state_reset(wf_ageassurance_state *s) {
    if (!s) {
        return;
    }
    wf_aa_begin_reset(&s->state);
    wf_aa_metadata_reset(&s->metadata);
}

/* Parse a defs#state object into `out`. Caller performs NULL checks. Returns
 * WF_ERR_PARSE if the required string fields are missing. */
static wf_status wf_aa_parse_state_obj(cJSON *obj, wf_ageassurance_begin *out) {
    cJSON *status = cJSON_GetObjectItemCaseSensitive(obj, "status");
    cJSON *access = cJSON_GetObjectItemCaseSensitive(obj, "access");
    if (!cJSON_IsString(status) || !cJSON_IsString(access)) {
        return WF_ERR_PARSE;
    }
    wf_status st = wf_aa_set_string(&out->status, status->valuestring);
    if (st != WF_OK) {
        return st;
    }
    st = wf_aa_set_string(&out->access, access->valuestring);
    if (st != WF_OK) {
        return st;
    }
    cJSON *last = cJSON_GetObjectItemCaseSensitive(obj, "lastInitiatedAt");
    if (cJSON_IsString(last)) {
        st = wf_aa_set_string(&out->last_initiated_at, last->valuestring);
        if (st != WF_OK) {
            return st;
        }
    }
    return WF_OK;
}

wf_status wf_ageassurance_parse_begin(const char *json, size_t json_len,
                                      wf_ageassurance_begin *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_aa_begin_reset(out);
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return WF_ERR_PARSE;
    }
    wf_status st = wf_aa_parse_state_obj(root, out);
    cJSON_Delete(root);
    if (st != WF_OK) {
        wf_aa_begin_reset(out);
    }
    return st;
}

wf_status wf_ageassurance_parse_config(const char *json, size_t json_len,
                                       wf_ageassurance_config *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_aa_config_reset(out);
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return WF_ERR_PARSE;
    }
    cJSON *regions = cJSON_DetachItemFromObjectCaseSensitive(root, "regions");
    cJSON_Delete(root);
    if (!regions) {
        /* Required by the schema; treat absence as malformed. */
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsArray(regions)) {
        cJSON_Delete(regions);
        return WF_ERR_PARSE;
    }
    out->regions = regions;
    return WF_OK;
}

wf_status wf_ageassurance_parse_state(const char *json, size_t json_len,
                                      wf_ageassurance_state *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_aa_state_reset(out);
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return WF_ERR_PARSE;
    }
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    cJSON *metadata = cJSON_GetObjectItemCaseSensitive(root, "metadata");
    if (!cJSON_IsObject(state) || !cJSON_IsObject(metadata)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    wf_status st = wf_aa_parse_state_obj(state, &out->state);
    if (st != WF_OK) {
        cJSON_Delete(root);
        wf_aa_state_reset(out);
        return st;
    }
    cJSON *created = cJSON_GetObjectItemCaseSensitive(metadata, "accountCreatedAt");
    if (cJSON_IsString(created)) {
        st = wf_aa_set_string(&out->metadata.account_created_at,
                              created->valuestring);
        if (st != WF_OK) {
            cJSON_Delete(root);
            wf_aa_state_reset(out);
            return st;
        }
    }
    cJSON_Delete(root);
    return WF_OK;
}

void wf_ageassurance_begin_free(wf_ageassurance_begin *out) {
    wf_aa_begin_reset(out);
}

void wf_ageassurance_config_free(wf_ageassurance_config *out) {
    wf_aa_config_reset(out);
}

void wf_ageassurance_state_free(wf_ageassurance_state *out) {
    wf_aa_state_reset(out);
}

/* Agent convenience wrappers. */

wf_status wf_agent_begin_ageassurance(wf_agent *agent, wf_ageassurance_begin *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_aa_begin_reset(out);
    /* The opaque agent cannot supply the required begin input (email /
     * language / countryCode), so we issue the call with an empty input. Callers
     * needing a real initiation should use the lex wrapper directly. */
    wf_lex_app_bsky_ageassurance_begin_main_input input = {0};
    wf_agent_sync_auth(agent);
    wf_response resp = {0};
    wf_status st = wf_lex_app_bsky_ageassurance_begin_main_call(agent->client,
                                                               &input, &resp);
    if (st != WF_OK) {
        wf_response_free(&resp);
        return st;
    }
    if (!resp.body) {
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }
    st = wf_ageassurance_parse_begin(resp.body, resp.body_len, out);
    wf_response_free(&resp);
    return st;
}

wf_status wf_agent_get_ageassurance_config(wf_agent *agent,
                                           wf_ageassurance_config *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_aa_config_reset(out);
    wf_agent_sync_auth(agent);
    wf_response resp = {0};
    wf_status st = wf_lex_app_bsky_ageassurance_get_config_main_call(agent->client,
                                                                     &resp);
    if (st != WF_OK) {
        wf_response_free(&resp);
        return st;
    }
    if (!resp.body) {
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }
    st = wf_ageassurance_parse_config(resp.body, resp.body_len, out);
    wf_response_free(&resp);
    return st;
}

wf_status wf_agent_get_ageassurance_state(wf_agent *agent,
                                          wf_ageassurance_state *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_aa_state_reset(out);
    /* The opaque agent cannot supply the required getState countryCode param,
     * so we issue the call with an empty params. Callers needing a region-
     * specific state should use the lex wrapper directly. */
    wf_lex_app_bsky_ageassurance_get_state_main_params params = {0};
    wf_agent_sync_auth(agent);
    wf_response resp = {0};
    wf_status st = wf_lex_app_bsky_ageassurance_get_state_main_call(agent->client,
                                                                   &params, &resp);
    if (st != WF_OK) {
        wf_response_free(&resp);
        return st;
    }
    if (!resp.body) {
        wf_response_free(&resp);
        return WF_ERR_PARSE;
    }
    st = wf_ageassurance_parse_state(resp.body, resp.body_len, out);
    wf_response_free(&resp);
    return st;
}
