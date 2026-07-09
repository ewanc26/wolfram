/*
 * actor_status_typed.c — owned typed parsers + builder + agent convenience
 * wrappers for the app.bsky.actor.status namespace. See
 * include/wolfram/actor_status_typed.h for the public API, the authoritative
 * wire format, and ownership rules.
 *
 * Mirrors labeler_typed.c / actor_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra` cJSON subtrees where shapes are
 * open/unbounded, and full cleanup on the first error. The agent wrappers call
 * the generated lex wrappers directly after syncing auth via wf_agent_sync_auth;
 * because those helpers are absent for this namespace they are honest stubs
 * (see the notes in the header).
 */

#include "wolfram/actor_status_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_actor_status_strdup(const char *s) {
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

static wf_status wf_actor_status_set_string(char **dst, const char *src) {
    char *copy = wf_actor_status_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- record ---- */

static void wf_actor_status_reset(wf_actor_status *r) {
    if (!r) {
        return;
    }
    free(r->status);
    free(r->created_at);
    if (r->embed) {
        cJSON_Delete(r->embed);
    }
    if (r->extra) {
        cJSON_Delete(r->extra);
    }
    memset(r, 0, sizeof(*r));
}

/* ---- view ---- */

static void wf_actor_status_view_reset(wf_actor_status_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    free(v->status);
    free(v->created_at);
    free(v->expires_at);
    free(v->actor);
    free(v->last_updated);
    if (v->embed) {
        cJSON_Delete(v->embed);
    }
    if (v->viewer_state) {
        cJSON_Delete(v->viewer_state);
    }
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

/* ---- put result ---- */

void wf_actor_status_put_result_free(wf_actor_status_put_result *r) {
    if (!r) {
        return;
    }
    free(r->uri);
    free(r->cid);
    free(r->value);
    memset(r, 0, sizeof(*r));
}

/* ---- core field extraction helpers ---- */

/* Copy a string field into `dst` (owned). */
static wf_status wf_actor_status_take_string(cJSON *obj, const char *key,
                                             char **dst) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        return wf_actor_status_set_string(dst, item->valuestring);
    }
    return WF_OK;
}

/* Extract an optional integer field into `has_*`/`out`. */
static void wf_actor_status_take_int(cJSON *obj, const char *key, bool *has,
                                     int64_t *out) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        *has = true;
        *out = (int64_t)item->valuedouble;
    }
}

/* Extract an optional bool field into `has_*`/`out`. */
static void wf_actor_status_take_bool(cJSON *obj, const char *key, bool *has,
                                      bool *out) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) {
        *has = true;
        *out = cJSON_IsTrue(item);
    }
}

/* Detach an object-typed field into an owned subtree (NULL if absent/wrong
 * type). Returns WF_OK; does not fail. */
static cJSON *wf_actor_status_take_object(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsObject(item)) {
        return cJSON_DetachItemFromObject(obj, key);
    }
    if (item) {
        cJSON_Delete(item);
        cJSON_DetachItemFromObject(obj, key);
    }
    return NULL;
}

/* ---- record parser ---- */

wf_status wf_actor_status_parse_record(const char *json, size_t json_len,
                                       wf_actor_status *out) {
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
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "status", &out->status);
    }
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "createdAt",
                                              &out->created_at);
    }
    if (status == WF_OK) {
        wf_actor_status_take_int(root, "durationMinutes",
                                 &out->has_duration_minutes,
                                 &out->duration_minutes);
        cJSON_DetachItemFromObject(root, "durationMinutes");
    }

    if (status == WF_OK) {
        /* embed kept as an owned detached subtree */
        cJSON *embed = wf_actor_status_take_object(root, "embed");
        if (embed) {
            out->embed = embed;
        }
    }

    if (status == WF_OK) {
        out->extra = root; /* remaining keys are open fields */
    } else {
        wf_actor_status_reset(out);
        cJSON_Delete(root);
    }
    return status;
}

/* ---- view parser ---- */

wf_status wf_actor_status_parse_view(const char *json, size_t json_len,
                                     wf_actor_status_view *out) {
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
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "uri", &out->uri);
    }
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "cid", &out->cid);
    }
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "status", &out->status);
    }
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "createdAt",
                                              &out->created_at);
    }
    if (status == WF_OK) {
        wf_actor_status_take_int(root, "durationMinutes",
                                 &out->has_duration_minutes,
                                 &out->duration_minutes);
        cJSON_DetachItemFromObject(root, "durationMinutes");
    }
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "expiresAt",
                                              &out->expires_at);
    }
    if (status == WF_OK) {
        wf_actor_status_take_bool(root, "isActive", &out->has_is_active,
                                  &out->is_active);
        cJSON_DetachItemFromObject(root, "isActive");
        wf_actor_status_take_bool(root, "isDisabled", &out->has_is_disabled,
                                  &out->is_disabled);
        cJSON_DetachItemFromObject(root, "isDisabled");
    }
    /* task-described envelope fields, tolerated when present */
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "actor", &out->actor);
    }
    if (status == WF_OK) {
        status = wf_actor_status_take_string(root, "lastUpdated",
                                              &out->last_updated);
    }
    if (status == WF_OK) {
        cJSON *embed = wf_actor_status_take_object(root, "embed");
        if (embed) {
            out->embed = embed;
        }
        cJSON *viewer = wf_actor_status_take_object(root, "viewerState");
        if (viewer) {
            out->viewer_state = viewer;
        }
    }

    if (status == WF_OK) {
        out->extra = root; /* remaining keys are open fields */
    } else {
        wf_actor_status_view_reset(out);
        cJSON_Delete(root);
    }
    return status;
}

/* ---- free functions ---- */

void wf_actor_status_free(wf_actor_status *r) {
    wf_actor_status_reset(r);
}

void wf_actor_status_view_free(wf_actor_status_view *v) {
    wf_actor_status_view_reset(v);
}

/* ---- builder ---- */

wf_status wf_actor_status_build_record(const char *created_at,
                                       const char *status, const cJSON *embed,
                                       char **out_json) {
    if (!created_at || !created_at[0] || !status || !status[0] || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return WF_ERR_ALLOC;
    }

    cJSON *s = cJSON_CreateString(status);
    cJSON *ca = cJSON_CreateString(created_at);
    if (!s || !ca) {
        cJSON_Delete(obj);
        cJSON_Delete(s);
        cJSON_Delete(ca);
        return WF_ERR_ALLOC;
    }
    cJSON_AddItemToObject(obj, "status", s);
    cJSON_AddItemToObject(obj, "createdAt", ca);

    if (embed) {
        cJSON *e = cJSON_Duplicate(embed, 1);
        if (!e) {
            cJSON_Delete(obj);
            return WF_ERR_ALLOC;
        }
        cJSON_AddItemToObject(obj, "embed", e);
    }

    char *buf = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!buf) {
        return WF_ERR_ALLOC;
    }
    *out_json = buf;
    return WF_OK;
}

/* ---- Agent convenience wrappers ----
 * The atproto lexicon corpus currently ships only the record type for
 * app.bsky.actor.status and does not generate the query/procedure call helpers
 * (getActorStatus/getStatus/putStatus). Until those generated bindings exist,
 * these are honest stubs: they validate required inputs and return
 * WF_ERR_INVALID_ARG with a TODO explaining what is missing. */

wf_status wf_agent_get_actor_status(wf_agent *agent, const char *actor,
                                    wf_actor_status_view *out) {
    if (!agent || !actor || !actor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    /* TODO: once wf_lex_app_bsky_actor_status_get_actor_status_main_call and
     * its _main_output_decode_json/_free exist, sync auth, call the wrapper,
     * decode res.body into `out`, and free the response. The atproto lexicon
     * does not yet ship this endpoint's generated binding. */
    return WF_ERR_INVALID_ARG;
}

wf_status wf_agent_get_status(wf_agent *agent, wf_actor_status_view *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    /* TODO: once wf_lex_app_bsky_actor_status_get_status_main_call and its
     * _main_output_decode_json/_free exist, sync auth, call the wrapper,
     * decode res.body into `out`, and free the response. The atproto lexicon
     * does not yet ship this endpoint's generated binding. */
    return WF_ERR_INVALID_ARG;
}

wf_status wf_agent_put_status(wf_agent *agent, const wf_actor_status *in,
                              wf_actor_status_put_result *out) {
    if (!agent || !in || !out) {
        return WF_ERR_INVALID_ARG;
    }
    /* TODO: once wf_lex_app_bsky_actor_status_put_status_main_call and its
     * _main_output_decode_json/_free exist, build the record via
     * wf_actor_status_build_record, sync auth, call the wrapper with the record
     * input, decode res.body into `out` (uri/cid/value), and free the
     * response. The atproto lexicon does not yet ship this endpoint's
     * generated binding. */
    return WF_ERR_INVALID_ARG;
}
