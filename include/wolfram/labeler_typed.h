/*
 * labeler_typed.h — owned typed parsers for the labeler / label namespaces,
 * plus convenience agent wrappers for labeler service coverage and label
 * queries. See src/labeler_typed.c for ownership rules.
 *
 * Conventions mirror contact_typed.h / admin_typed.h: wf_status error codes,
 * static strdup/set_string/reset helpers, ownership via cJSON_DetachItem*,
 * and a matching `_free` for every owned struct (a freed/zeroed struct frees
 * safely). Every owned string is heap-allocated; the `extra` field (where
 * present) holds an owned detached cJSON subtree of open/unbounded fields.
 *
 * Parsers (wf_labeler_parse_*):
 *   - wf_labeler_parse_query_labels    com.atproto.label.queryLabels output
 *   - wf_labeler_parse_services        app.bsky.labeler.getServices output
 *   - wf_labeler_parse_temp_labels     com.atproto.temp.fetchLabels output
 *   - wf_labeler_parse_service_record  app.bsky.labeler.service record (raw)
 *
 * Agent wrappers (wf_agent_*): thin convenience calls layered on the generated
 * lex wrappers after syncing auth onto the agent's primary XRPC client.
 */

#ifndef WOLFRAM_LABELER_TYPED_H
#define WOLFRAM_LABELER_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single label (com.atproto.label.defs#label). `sig` is the base64-encoded
 * dag-cbor signature kept as an owned string (the wire format is bytes). */
typedef struct wf_labeler_label {
    bool has_ver;
    int64_t ver;
    char *src;
    char *uri;
    bool has_cid;
    char *cid;
    char *val;
    bool has_neg;
    bool neg;
    char *cts;
    bool has_exp;
    char *exp;
    bool has_sig;
    char *sig;            /* base64 string; NULL absent */
} wf_labeler_label;

/* A list of labels plus an optional cursor (queryLabels). */
typedef struct wf_labeler_label_list {
    wf_labeler_label *labels;
    size_t label_count;
    char *cursor;
} wf_labeler_label_list;

/* A list of labels (temp.fetchLabels; no cursor on that endpoint). */
typedef struct wf_labeler_temp_label_list {
    wf_labeler_label *labels;
    size_t label_count;
} wf_labeler_temp_label_list;

/* Localized strings for a label value definition (com.atproto.label.defs
 * #labelValueDefinitionStrings). */
typedef struct wf_labeler_label_value_def_locale {
    char *lang;
    char *name;
    char *description;
} wf_labeler_label_value_def_locale;

/* A label value definition (com.atproto.label.defs#labelValueDefinition). */
typedef struct wf_labeler_label_value_def {
    char *identifier;
    char *severity;
    char *blurs;
    bool has_default_setting;
    char *default_setting;
    bool has_adult_only;
    bool adult_only;
    wf_labeler_label_value_def_locale *locales;
    size_t locale_count;
} wf_labeler_label_value_def;

/* A labeler's policies (app.bsky.labeler.defs#labelerPolicies). `label_values`
 * are the published (global or custom) label values; `label_value_definitions`
 * are labeler-scoped value definitions that override global ones. */
typedef struct wf_labeler_policies {
    char **label_values;
    size_t label_value_count;
    wf_labeler_label_value_def *label_value_definitions;
    size_t label_value_definition_count;
} wf_labeler_policies;

/* The creator profile (app.bsky.actor.defs#profileView) of a labeler. Core
 * fields are copied; any other fields remain in owned `extra`. */
typedef struct wf_labeler_creator {
    char *did;
    char *handle;
    char *display_name;
    char *avatar;
    cJSON *extra;          /* owned detached subtree of unknown fields; NULL absent */
} wf_labeler_creator;

/* A labeler service view (app.bsky.labeler.defs#labelerView or
 * #labelerViewDetailed). When `has_policies` is true the view is detailed and
 * `policies` is populated. */
typedef struct wf_labeler_service_view {
    char *uri;
    char *cid;
    wf_labeler_creator creator;
    bool has_like_count;
    int64_t like_count;
    char *indexed_at;
    wf_labeler_label *labels;
    size_t label_count;
    bool has_policies;
    wf_labeler_policies policies;
    char **reason_types;
    size_t reason_type_count;
    char **subject_types;
    size_t subject_type_count;
    char **subject_collections;
    size_t subject_collection_count;
    bool is_detailed;
    cJSON *extra;          /* owned detached subtree of unknown fields; NULL absent */
} wf_labeler_service_view;

/* A list of labeler service views (getServices). */
typedef struct wf_labeler_service_list {
    wf_labeler_service_view *services;
    size_t service_count;
} wf_labeler_service_list;

/* A raw app.bsky.labeler.service record. `self_label_values` are the `val`
 * strings from the embedded selfLabels.values array. */
typedef struct wf_labeler_service_record {
    wf_labeler_policies policies;
    bool has_labels;
    char **self_label_values;
    size_t self_label_count;
    char *created_at;
    char **reason_types;
    size_t reason_type_count;
    char **subject_types;
    size_t subject_type_count;
    char **subject_collections;
    size_t subject_collection_count;
    cJSON *extra;          /* owned detached subtree of unknown fields; NULL absent */
} wf_labeler_service_record;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a com.atproto.label.queryLabels JSON body ("labels" array + cursor). */
wf_status wf_labeler_parse_query_labels(const char *json, size_t json_len,
                                        wf_labeler_label_list *out);

/* Parse an app.bsky.labeler.getServices JSON body ("views" array of service
 * views, including embedded policies and label value definitions). */
wf_status wf_labeler_parse_services(const char *json, size_t json_len,
                                     wf_labeler_service_list *out);

/* Parse a com.atproto.temp.fetchLabels JSON body ("labels" array). */
wf_status wf_labeler_parse_temp_labels(const char *json, size_t json_len,
                                       wf_labeler_temp_label_list *out);

/* Parse a raw app.bsky.labeler.service record JSON body. */
wf_status wf_labeler_parse_service_record(const char *json, size_t json_len,
                                          wf_labeler_service_record *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_labeler_label_list_free(wf_labeler_label_list *l);
void wf_labeler_temp_label_list_free(wf_labeler_temp_label_list *l);
void wf_labeler_service_list_free(wf_labeler_service_list *l);
void wf_labeler_service_record_free(wf_labeler_service_record *r);
void wf_labeler_policies_free(wf_labeler_policies *p);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding lex call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty. */

/* com.atproto.label.queryLabels. `sources`/`source_count` may be NULL/0. */
wf_status wf_agent_query_labels(wf_agent *agent, const char *const *uris,
                                size_t uri_count, const char *const *sources,
                                size_t source_count, wf_labeler_label_list *out);

/* app.bsky.labeler.getServices. */
wf_status wf_agent_get_labeler_services(wf_agent *agent, const char *const *dids,
                                        size_t did_count, bool detailed,
                                        wf_labeler_service_list *out);

/* Legacy com.atproto.temp.fetchLabels helper. The endpoint has no DID filter,
 * so this obsolete signature cannot honor `did` and returns
 * WF_ERR_INVALID_ARG rather than silently ignoring it. */
wf_status wf_agent_fetch_labels_typed(wf_agent *agent, const char *did,
                                      wf_labeler_temp_label_list *out);

/* Exact typed fetchLabels query. `has_since` controls the optional integer
 * cursor; `limit == 0` omits the parameter, otherwise the lexicon requires
 * 1..250. The returned list is freed with wf_labeler_temp_label_list_free. */
wf_status wf_agent_fetch_labels_list(wf_agent *agent,
                                     int has_since, int64_t since, int limit,
                                     wf_labeler_temp_label_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_LABELER_TYPED_H */
