#ifndef WOLFRAM_REPO_RECORD_H
#define WOLFRAM_REPO_RECORD_H

#include "wolfram/repo/commit.h"

#ifdef __cplusplus
extern "C" {
#endif

wf_status wf_repo_create_record(wf_car *car, const wf_cid *prev_commit,
                                const char *did, const char *collection,
                                const char *rkey,
                                const unsigned char *record_cbor,
                                size_t record_cbor_len,
                                const wf_signing_key *key, wf_cid *out_commit,
                                wf_cid *out_record);

wf_status wf_repo_get_record(wf_car *car, const wf_cid *commit_cid,
                             const char *collection, const char *rkey,
                             unsigned char **out_data, size_t *out_len,
                             wf_cid *out_record_cid);

wf_status wf_repo_update_record(wf_car *car, const wf_cid *prev_commit,
                                const char *did, const char *collection,
                                const char *rkey,
                                const unsigned char *record_cbor,
                                size_t record_cbor_len,
                                const wf_signing_key *key, wf_cid *out_commit,
                                wf_cid *out_record);

wf_status wf_repo_delete_record(wf_car *car, const wf_cid *prev_commit,
                                const char *did, const char *collection,
                                const char *rkey, const wf_signing_key *key,
                                wf_cid *out_commit);

/** One entry in a batched repo write (com.atproto.repo.applyWrites). */
typedef enum wf_repo_write_action {
    WF_REPO_WRITE_CREATE = 0,
    WF_REPO_WRITE_UPDATE,
    WF_REPO_WRITE_DELETE
} wf_repo_write_action;

typedef struct wf_repo_write {
    wf_repo_write_action action;
    const char *collection;
    const char *rkey;
    /* Record body; ignored (and may be NULL) for WF_REPO_WRITE_DELETE. */
    const unsigned char *record_cbor;
    size_t record_cbor_len;
    /* Out: CID of the written record, set for create and update. */
    wf_cid out_record;
} wf_repo_write;

/**
 * Apply `count` writes to the repo as a SINGLE signed commit, the way
 * com.atproto.repo.applyWrites is specified: the batch is atomic and the
 * firehose sees one #commit event carrying every op, rather than one commit
 * per write.
 *
 * All writes are staged against a working MST root and only the final root is
 * committed, so a failure part-way leaves `car` without a new commit and the
 * previous head still current. Creating a record whose key already exists
 * returns WF_ERR_CONFLICT; updating or deleting one that does not exist
 * returns WF_ERR_NOT_FOUND. `count` may be 0, which produces an empty commit
 * (allowed by the protocol: it advances rev and signature only).
 *
 * On WF_OK each write's `out_record` holds the CID of the record written, and
 * *out_commit is the new commit CID.
 */
wf_status wf_repo_apply_writes(wf_car *car, const wf_cid *prev_commit,
                               const char *did, wf_repo_write *writes,
                               size_t count, const wf_signing_key *key,
                               wf_cid *out_commit);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_RECORD_H */