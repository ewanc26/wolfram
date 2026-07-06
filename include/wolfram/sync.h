/**
 * sync.h — AT Protocol repository synchronization APIs.
 */

#ifndef WOLFRAM_SYNC_H
#define WOLFRAM_SYNC_H

#include "wolfram/repo.h"
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_sync_blob_list {
    char **cids;
    size_t cid_count;
    char *cursor;
} wf_sync_blob_list;

typedef struct wf_sync_head {
    char *root;
    char *rev;
} wf_sync_head;

typedef struct wf_sync_commit_info {
    char *cid;
    char *rev;
} wf_sync_commit_info;

typedef struct wf_sync_repo_status {
    char *did;
    char *active; /* textual boolean: "true" or "false" */
    char *rev;
} wf_sync_repo_status;

typedef struct wf_sync_repo_entry {
    char *did;
    char *rev;
} wf_sync_repo_entry;

typedef struct wf_sync_repo_list {
    wf_sync_repo_entry *repos;
    size_t repo_count;
    char *cursor;
} wf_sync_repo_list;

/** Download a blob as raw bytes. Caller frees `*out_data` with free(). */
wf_status wf_sync_get_blob(wf_xrpc_client *client,
                           const char *did, const char *cid,
                           unsigned char **out_data, size_t *out_len);

/** Download blocks by CID and parse the response CAR. */
wf_status wf_sync_get_blocks(wf_xrpc_client *client,
                             const char *did, const char *const *cids, size_t cid_count,
                             wf_car *out);

/** Download record proof blocks and parse the response CAR. */
wf_status wf_sync_get_record(wf_xrpc_client *client,
                             const char *did, const char *collection, const char *rkey,
                             wf_car *out);

/** List blob CIDs for an account. Output owns all heap allocations. */
wf_status wf_sync_list_blobs(wf_xrpc_client *client,
                             const char *did, const char *since,
                             int limit, const char *cursor,
                             wf_sync_blob_list *out);
void wf_sync_blob_list_free(wf_sync_blob_list *list);

/** Get the current root CID of a repo. */
wf_status wf_sync_get_head(wf_xrpc_client *client,
                           const char *did, wf_sync_head *out);
void wf_sync_head_free(wf_sync_head *head);

/** Get the latest commit CID and revision for a repo. */
wf_status wf_sync_get_latest_commit(wf_xrpc_client *client,
                                    const char *did, wf_sync_commit_info *out);
void wf_sync_commit_info_free(wf_sync_commit_info *info);

/** Get the hosting status of a repo. */
wf_status wf_sync_get_repo_status(wf_xrpc_client *client,
                                  const char *did, wf_sync_repo_status *out);
void wf_sync_repo_status_free(wf_sync_repo_status *status);

/** List all repos hosted by a PDS. Output owns all heap allocations. */
wf_status wf_sync_list_repos(wf_xrpc_client *client,
                             const char *cursor, int limit,
                             wf_sync_repo_list *out);
void wf_sync_repo_list_free(wf_sync_repo_list *list);

/**
 * Download and parse a repository export using com.atproto.sync.getRepo.
 *
 * `did` is required. Pass NULL for `since` to request a full export, or a
 * repository revision TID to request a diff. On success, `out` owns all CAR
 * allocations and must be released with wf_car_free().
 */
wf_status wf_sync_get_repo(wf_xrpc_client *client,
                           const char *did,
                           const char *since,
                           wf_car *out);

/** Parse and verify an incremental repository CAR response. */
wf_status wf_sync_verify_diff_car(const wf_car *base,
                                  const wf_cid *base_commit,
                                  const unsigned char *bytes,
                                  size_t len,
                                  const wf_repo_verify_options *options,
                                  wf_repo_diff *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SYNC_H */
