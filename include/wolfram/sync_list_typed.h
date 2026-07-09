/*
 * sync_list_typed.h — owned typed parsers for the com.atproto.sync
 * LIST / HOST / CRAWL endpoints, plus convenience agent wrappers layered on
 * the generated lex wrappers. This module is DISTINCT from sync_typed.h
 * (which covers repoStatus, latestCommit, getBlocks, getRecord).
 *
 * Conventions mirror labeler_typed.h: wf_status error codes, static
 * strdup/set_string/reset helpers, ownership via owned heap strings, and a
 * matching `_free` for every owned struct (a freed/zeroed struct frees
 * safely). Every owned string is heap-allocated.
 *
 * Parsers (wf_sync_parse_*):
 *   - wf_sync_parse_repo_list          com.atproto.sync.listRepos output
 *   - wf_sync_parse_repo_by_collection_list
 *                                      com.atproto.sync.listReposByCollection
 *   - wf_sync_parse_blob_cid_list      com.atproto.sync.listBlobs output
 *   - wf_sync_parse_host_list          com.atproto.sync.listHosts output
 *   - wf_sync_parse_host               com.atproto.sync.getHostStatus output
 *
 * Agent wrappers (wf_agent_*): thin convenience calls layered on the generated
 * lex wrappers after syncing auth onto the agent's primary XRPC client. The
 * getRepo/getCheckout wrappers return the raw CAR body bytes (owned; freed by
 * the caller with free()); they do NOT parse the CAR (that is sync.h's job).
 */

#ifndef WOLFRAM_SYNC_LIST_TYPED_H
#define WOLFRAM_SYNC_LIST_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single repo reference (com.atproto.sync.listRepos#repo): did, head, rev,
 * optional active flag, and optional status string. */
typedef struct wf_sync_repo_ref {
    char *did;
    char *head;
    char *rev;
    bool has_active;
    bool active;
    bool has_status;
    char *status;        /* NULL absent */
} wf_sync_repo_ref;

/* A list of repo references plus an optional cursor (listRepos). */
typedef struct wf_sync_repo_ref_list {
    wf_sync_repo_ref *items;
    size_t count;
    char *cursor;        /* NULL absent */
} wf_sync_repo_ref_list;

/* A single repo reference keyed by DID only (listReposByCollection#repo). */
typedef struct wf_sync_repo_by_collection {
    char *did;
} wf_sync_repo_by_collection;

/* A list of repo references plus an optional cursor (listReposByCollection). */
typedef struct wf_sync_repo_by_collection_list {
    wf_sync_repo_by_collection *items;
    size_t count;
    char *cursor;        /* NULL absent */
} wf_sync_repo_by_collection_list;

/* A list of blob CIDs (strings) plus an optional cursor (listBlobs). */
typedef struct wf_sync_blob_cid_list {
    char **cids;
    size_t count;
    char *cursor;        /* NULL absent */
} wf_sync_blob_cid_list;

/* A single host (com.atproto.sync.listHosts#host and getHostStatus output):
 * hostname, optional seq, optional account_count, and optional status. */
typedef struct wf_sync_host {
    char *hostname;
    bool has_seq;
    int64_t seq;
    bool has_account_count;
    int64_t account_count;
    bool has_status;
    char *status;        /* NULL absent */
} wf_sync_host;

/* A list of hosts plus an optional cursor (listHosts). */
typedef struct wf_sync_host_list {
    wf_sync_host *items;
    size_t count;
    char *cursor;        /* NULL absent */
} wf_sync_host_list;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a com.atproto.sync.listRepos JSON body ("repos" array + cursor). */
wf_status wf_sync_parse_repo_list(const char *json, size_t json_len,
                                  wf_sync_repo_ref_list *out);

/* Parse a com.atproto.sync.listReposByCollection JSON body ("repos" + cursor). */
wf_status wf_sync_parse_repo_by_collection_list(
    const char *json, size_t json_len, wf_sync_repo_by_collection_list *out);

/* Parse a com.atproto.sync.listBlobs JSON body ("cids" array + cursor). */
wf_status wf_sync_parse_blob_cid_list(const char *json, size_t json_len,
                                      wf_sync_blob_cid_list *out);

/* Parse a com.atproto.sync.listHosts JSON body ("hosts" array + cursor). */
wf_status wf_sync_parse_host_list(const char *json, size_t json_len,
                                  wf_sync_host_list *out);

/* Parse a com.atproto.sync.getHostStatus JSON body (single host object). */
wf_status wf_sync_parse_host(const char *json, size_t json_len,
                             wf_sync_host *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_sync_repo_ref_list_free(wf_sync_repo_ref_list *l);
void wf_sync_repo_by_collection_list_free(wf_sync_repo_by_collection_list *l);
void wf_sync_blob_cid_list_free(wf_sync_blob_cid_list *l);
void wf_sync_host_list_free(wf_sync_host_list *l);
void wf_sync_host_free(wf_sync_host *h);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding lex call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty. The binary CAR wrappers (getRepo/getCheckout) return owned bytes
 * via `out_bytes`/`out_len` (caller frees with free()). */

/* com.atproto.sync.listRepos. `limit` <= 0 uses the server default. */
wf_status wf_agent_list_repos_typed(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_sync_repo_ref_list *out);

/* com.atproto.sync.listReposByCollection. `collection` is required. */
wf_status wf_agent_list_repos_by_collection_typed(
    wf_agent *agent, const char *collection, int limit, const char *cursor,
    wf_sync_repo_by_collection_list *out);

/* com.atproto.sync.listBlobs. `did` is required; `since` may be NULL. */
wf_status wf_agent_list_blobs_typed(wf_agent *agent, const char *did,
                                    const char *since, int limit,
                                    const char *cursor,
                                    wf_sync_blob_cid_list *out);

/* com.atproto.sync.listHosts. `limit` <= 0 uses the server default. */
wf_status wf_agent_list_hosts_typed(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_sync_host_list *out);

/* com.atproto.sync.getHostStatus. `hostname` is required. */
wf_status wf_agent_get_host_status_typed(wf_agent *agent, const char *hostname,
                                         wf_sync_host *out);

/* com.atproto.sync.notifyOfUpdate (procedure, input {hostname}). No output. */
wf_status wf_agent_notify_of_update_typed(wf_agent *agent, const char *hostname);

/* com.atproto.sync.requestCrawl (procedure, input {hostname}). No output. */
wf_status wf_agent_request_crawl_typed(wf_agent *agent, const char *hostname);

/* com.atproto.sync.getRepo (binary CAR output). `did` is required; `since` may
 * be NULL. Returns owned bytes via `out_bytes`/`out_len` (caller frees with
 * free()). The CAR is NOT parsed here (see sync.h for CAR handling). */
wf_status wf_agent_get_repo_car(wf_agent *agent, const char *did,
                                const char *since_or_null,
                                unsigned char **out_bytes, size_t *out_len);

/* com.atproto.sync.getCheckout (binary CAR output). `did` is required. Returns
 * owned bytes via `out_bytes`/`out_len` (caller frees with free()). */
wf_status wf_agent_get_checkout_car(wf_agent *agent, const char *did,
                                    unsigned char **out_bytes,
                                    size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SYNC_LIST_TYPED_H */
