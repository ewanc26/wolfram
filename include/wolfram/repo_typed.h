/*
 * repo_typed.h — owned typed agent wrappers for the com.atproto.repo XRPC
 * endpoints: getRecord, listRecords, describeRepo, listMissingBlobs, and
 * applyWrites (with a small owned input builder).
 *
 * Conventions mirror labeler_typed.h: wf_status error codes, static
 * strdup/set_string/reset helpers, ownership via cJSON_DetachItem*, and a
 * matching `_free` for every owned struct (a freed/zeroed struct frees
 * safely). Every owned string is heap-allocated; the `value`, `did_doc`,
 * `commit`, `results`, and trailing `extra` fields (where present) hold an
 * owned detached cJSON subtree of open/unbounded fields.
 *
 * Parsers (wf_repo_parse_*): hand-parse the raw response body into owned
 * structured output.
 *
 * Agent wrappers (wf_agent_*_typed): validate required args, sync auth onto
 * the agent's primary XRPC client, issue the generated lex call, then parse
 * the body. On success the output is owned by the caller (free with the
 * matching `_free`); on error it is left reset.
 */

#ifndef WOLFRAM_REPO_TYPED_H
#define WOLFRAM_REPO_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single record (getRecord / listRecords). `value` is the record value
 * object, kept as an owned detached cJSON subtree since its shape varies per
 * collection. */
typedef struct wf_repo_record {
    char *uri;
    bool has_cid;
    char *cid;
    cJSON *value;          /* owned detached subtree; NULL absent */
} wf_repo_record;

/* A list of records plus an optional cursor (listRecords). */
typedef struct wf_repo_record_list {
    wf_repo_record *items;
    size_t count;
    char *cursor;
} wf_repo_record_list;

/* A repo description (describeRepo). Core fields are copied; the DID document
 * is kept as an owned `did_doc` subtree, and any other fields remain in owned
 * `extra`. */
typedef struct wf_repo_description {
    char *handle;
    char *did;
    cJSON *did_doc;        /* owned detached subtree; NULL absent */
    char **collections;
    size_t collection_count;
    bool has_handle_is_correct;
    bool handle_is_correct;
    cJSON *extra;          /* owned detached subtree of unknown fields; NULL absent */
} wf_repo_description;

/* A single missing blob (listMissingBlobs). */
typedef struct wf_repo_missing_blob {
    char *cid;
    char *record_uri;
} wf_repo_missing_blob;

/* A list of missing blobs plus an optional cursor (listMissingBlobs). */
typedef struct wf_repo_missing_blob_list {
    wf_repo_missing_blob *items;
    size_t count;
    char *cursor;
} wf_repo_missing_blob_list;

/* The result of applyWrites. The commit and results union array are kept as
 * owned detached cJSON subtrees (`commit`, `results`); convenience `cid`/`rev`
 * fields are also extracted from the commit object when present. */
typedef struct wf_repo_apply_writes_result {
    cJSON *commit;         /* owned detached subtree; NULL absent */
    bool has_commit_cid;
    char *commit_cid;
    bool has_commit_rev;
    char *commit_rev;
    cJSON *results;        /* owned detached subtree (array); NULL absent */
} wf_repo_apply_writes_result;

/* ---- Parsers (own their outputs; full cleanup on the first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a com.atproto.repo.getRecord JSON body ("uri", optional "cid", "value"). */
wf_status wf_repo_parse_get_record(const char *json, size_t json_len,
                                   wf_repo_record *out);

/* Parse a com.atproto.repo.listRecords JSON body ("records" array + cursor). */
wf_status wf_repo_parse_list_records(const char *json, size_t json_len,
                                     wf_repo_record_list *out);

/* Parse a com.atproto.repo.describeRepo JSON body ("handle", "did",
 * "didDoc", "collections", "handleIsCorrect"). */
wf_status wf_repo_parse_describe_repo(const char *json, size_t json_len,
                                      wf_repo_description *out);

/* Parse a com.atproto.repo.listMissingBlobs JSON body ("blobs" array +
 * cursor). */
wf_status wf_repo_parse_list_missing_blobs(const char *json, size_t json_len,
                                           wf_repo_missing_blob_list *out);

/* Parse a com.atproto.repo.applyWrites JSON body ("commit", "results"). */
wf_status wf_repo_parse_apply_writes(const char *json, size_t json_len,
                                     wf_repo_apply_writes_result *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_repo_record_free(wf_repo_record *r);
void wf_repo_record_list_free(wf_repo_record_list *l);
void wf_repo_description_free(wf_repo_description *d);
void wf_repo_missing_blob_list_free(wf_repo_missing_blob_list *l);
void wf_repo_apply_writes_result_free(wf_repo_apply_writes_result *r);

/* ---- applyWrites input builder ----
 * Builds an owned com.atproto.repo.applyWrites request body. Each `value`
 * argument is *taken ownership of* by the builder (do not free it yourself);
 * the builder frees it on wf_repo_writes_builder_free. wf_repo_writes_build_json
 * returns an owned JSON string (freed by the caller with free()). */

typedef struct wf_repo_writes_builder wf_repo_writes_builder;

/* Allocate a new, empty builder. Returns WF_OK / WF_ERR_ALLOC. */
wf_status wf_repo_writes_builder_init(wf_repo_writes_builder **out);

/* Append a create operation. `value` is consumed (ownership transferred). */
wf_status wf_repo_writes_add_create(wf_repo_writes_builder *b,
                                    const char *collection,
                                    const char *rkey_or_null, cJSON *value);

/* Append an update operation. `value` is consumed (ownership transferred). */
wf_status wf_repo_writes_add_update(wf_repo_writes_builder *b,
                                    const char *collection, const char *rkey,
                                    cJSON *value);

/* Append a delete operation. */
wf_status wf_repo_writes_add_delete(wf_repo_writes_builder *b,
                                    const char *collection, const char *rkey);

/* Serialize the builder to an owned JSON request body string into *out_json
 * (caller frees with free()). `validate`: <0 unset, 0 false, 1 true.
 * `swap_commit_or_null` is optional. Returns WF_OK / WF_ERR_INVALID_ARG /
 * WF_ERR_ALLOC. */
wf_status wf_repo_writes_build_json(wf_repo_writes_builder *b, const char *repo,
                                    int validate,
                                    const char *swap_commit_or_null,
                                    char **out_json);

/* Free the builder and any buffered value objects. Safe on NULL. */
void wf_repo_writes_builder_free(wf_repo_writes_builder *b);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding lex call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty. */

/* com.atproto.repo.getRecord. `cid_or_null` is optional. */
wf_status wf_agent_get_record_typed(wf_agent *agent, const char *repo,
                                    const char *collection, const char *rkey,
                                    const char *cid_or_null,
                                    wf_repo_record *out);

/* com.atproto.repo.listRecords. `cursor` is optional. */
wf_status wf_agent_list_records_typed(wf_agent *agent, const char *repo,
                                      const char *collection, int limit,
                                      const char *cursor, int reverse,
                                      wf_repo_record_list *out);

/* com.atproto.repo.describeRepo. `repo` is the handle or DID. */
wf_status wf_agent_describe_repo_typed(wf_agent *agent, const char *repo,
                                       wf_repo_description *out);

/* com.atproto.repo.listMissingBlobs. `cursor` is optional. */
wf_status wf_agent_list_missing_blobs_typed(wf_agent *agent, int limit,
                                            const char *cursor,
                                            wf_repo_missing_blob_list *out);

/* com.atproto.repo.applyWrites. `writes_json` is any valid applyWrites input
 * body (e.g. produced by wf_repo_writes_build_json). */
wf_status wf_agent_apply_writes_typed(wf_agent *agent, const char *writes_json,
                                      wf_repo_apply_writes_result *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_TYPED_H */
