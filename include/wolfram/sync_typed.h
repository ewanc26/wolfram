/*
 * sync_typed.h — owned typed parsers for the com.atproto.sync *query*
 * responses that return JSON or a CAR.
 *
 *   com.atproto.sync.getRepoStatus     -> application/json
 *   com.atproto.sync.getLatestCommit  -> application/json
 *   com.atproto.sync.getBlocks        -> application/vnd.ipld.car
 *   com.atproto.sync.getRecord        -> application/vnd.ipld.car
 *
 * The first two are parsed straight from the JSON body. The latter two are
 * CAR archives; their parsers decode the CAR with the existing repo CAR
 * machinery (wf_car_parse) and, for getRecord, extract the requested record
 * CBOR with wf_repo_get_record (reusing the MST traversal already in repo).
 *
 * Wire shapes follow the AUTHORITATIVE lexicons under
 * /Volumes/Storage/Developer/Local/atproto/lexicons/com/atproto/sync/:
 *   getRepoStatus  -> { did, active, status?, rev? }   (active is boolean)
 *   getLatestCommit-> { cid, rev }
 * (Note: these differ from the older prose shapes that mentioned head /
 * intersectingBlocks / lagMillis — the lexicon is the source of truth.)
 *
 * Naming note: the JSON-endpoint structs are suffixed `_typed` only where
 * they would otherwise collide with the pre-existing wf_sync_repo_status_typed /
 * wf_sync_commit_info types in sync.h; the CAR-endpoint structs reuse the
 * task's names (wf_sync_block / wf_sync_block_list / wf_sync_record) which
 * do not collide.
 *
 * Conventions mirror feed_typed.h / actor_typed.c: wf_status codes
 * (WF_ERR_INVALID_ARG / WF_ERR_PARSE / WF_ERR_ALLOC / WF_OK), static
 * strdup/set_string/reset helpers owned by the translation unit, and a
 * matching `_free` for every owned output.
 */

#ifndef WOLFRAM_SYNC_TYPED_H
#define WOLFRAM_SYNC_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── com.atproto.sync.getRepoStatus ──────────────────────────────── */
/* { did, active (bool, required), status? (string), rev? (string) }. */
typedef struct wf_sync_repo_status_typed {
    char *did;       /* owned; required */
    int   active;    /* required boolean from "active" */
    char *status;    /* owned; optional "status" (e.g. "deactivated") */
    char *rev;       /* owned; optional repo rev (present when active) */
} wf_sync_repo_status_typed;

/* Parse a getRepoStatus JSON body into an owned struct. Returns
 * WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE when the document is
 * malformed or missing the required `did`/`active`, WF_ERR_ALLOC on
 * allocation failure, WF_OK on success. On any error `out` is reset. */
wf_status wf_sync_repo_status_typed_parse(const char *json, size_t json_len,
                                   wf_sync_repo_status_typed *out);

/* Free the owned contents of a parsed repo status (safe on a reset list). */
void wf_sync_repo_status_typed_free(wf_sync_repo_status_typed *s);

/* ── com.atproto.sync.getLatestCommit ────────────────────────────── */
/* { cid, rev } — both required strings. */
typedef struct wf_sync_latest_commit {
    char *cid;       /* owned; required commit CID */
    char *rev;       /* owned; required repo rev (TID) */
} wf_sync_latest_commit;

/* Parse a getLatestCommit JSON body. Same ownership/error rules as the
 * repo-status parser (required cid+rev; missing -> WF_ERR_PARSE). */
wf_status wf_sync_latest_commit_parse(const char *json, size_t json_len,
                                     wf_sync_latest_commit *out);

/* Free the owned contents of a parsed latest-commit (safe on reset). */
void wf_sync_latest_commit_free(wf_sync_latest_commit *c);

/* ── com.atproto.sync.getBlocks ──────────────────────────────────── */
/* CAR archive -> list of { cid (string), value (owned CBOR bytes) }. */
typedef struct wf_sync_block {
    char    *cid;       /* owned CID string (base32 CIDv1) */
    uint8_t *value;     /* owned block bytes (CBOR) */
    size_t   value_len; /* byte length of `value` */
} wf_sync_block;

typedef struct wf_sync_block_list {
    wf_sync_block *items;
    size_t         count;
} wf_sync_block_list;

/* Parse a getBlocks CAR (raw bytes) into an owned list of blocks. Returns
 * WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on a malformed CAR,
 * WF_ERR_ALLOC on allocation failure, WF_OK on success. On error `out` is
 * reset. */
wf_status wf_sync_block_list_parse_car(const unsigned char *car_bytes,
                                      size_t len,
                                      wf_sync_block_list *out);

/* Free the owned contents of a parsed block list (safe on reset). */
void wf_sync_block_list_free(wf_sync_block_list *list);

/* ── com.atproto.sync.getRecord ──────────────────────────────────── */
/* CAR archive -> the requested record CBOR plus the repo rev from the commit.
 * When the record does not exist (a non-existence proof), parsing still
 * succeeds with `record_cbor == NULL` and `record_len == 0`, but `repo_rev`
 * is populated from the commit. */
typedef struct wf_sync_record {
    uint8_t *record_cbor;  /* owned record CBOR bytes (NULL if absent) */
    size_t   record_len;    /* byte length of `record_cbor` */
    char    *repo_rev;      /* owned repo rev (TID) from the commit */
} wf_sync_record;

/* Parse a getRecord CAR (raw bytes), extracting the record identified by
 * `collection`/`rkey`. Returns WF_ERR_INVALID_ARG on NULL inputs,
 * WF_ERR_PARSE on a malformed CAR or missing commit, WF_ERR_ALLOC on
 * allocation failure, WF_OK otherwise. On error `out` is reset. */
wf_status wf_sync_record_parse_car(const unsigned char *car_bytes,
                                  size_t len,
                                  const char *collection,
                                  const char *rkey,
                                  wf_sync_record *out);

/* Free the owned contents of a parsed record (safe on reset). */
void wf_sync_record_free(wf_sync_record *r);

/* ── Agent convenience wrappers ──────────────────────────────────── */
/* Each issues the corresponding com.atproto.sync call through the agent and
 * parses the body into `out`. Inputs are validated: required non-NULL /
 * non-empty strings yield WF_ERR_INVALID_ARG. On success `out` is owned by
 * the caller and freed with the matching `_free`; on error it is reset. */

wf_status wf_agent_get_repo_status_typed(wf_agent *agent, const char *did,
                                        wf_sync_repo_status_typed *out);

wf_status wf_agent_get_latest_commit_typed(wf_agent *agent, const char *did,
                                           wf_sync_latest_commit *out);

wf_status wf_agent_sync_get_blocks_typed(wf_agent *agent, const char *did,
                                     const char *const *cids, size_t n,
                                     wf_sync_block_list *out);

wf_status wf_agent_sync_get_record_typed(wf_agent *agent, const char *did,
                                    const char *collection, const char *rkey,
                                    wf_sync_record *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SYNC_TYPED_H */
