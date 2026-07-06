#ifndef WOLFRAM_REPO_RECORD_H
#define WOLFRAM_REPO_RECORD_H

#include "wolfram/repo/commit.h"

#ifdef __cplusplus
extern "C" {
#endif

wf_status wf_repo_create_record(wf_car *car,
                                const wf_cid *prev_commit,
                                const char *did,
                                const char *collection,
                                const char *rkey,
                                const unsigned char *record_cbor,
                                size_t record_cbor_len,
                                const wf_signing_key *key,
                                wf_cid *out_commit,
                                wf_cid *out_record);

wf_status wf_repo_get_record(wf_car *car,
                             const wf_cid *commit_cid,
                             const char *collection,
                             const char *rkey,
                             unsigned char **out_data,
                             size_t *out_len,
                             wf_cid *out_record_cid);

wf_status wf_repo_update_record(wf_car *car,
                                const wf_cid *prev_commit,
                                const char *did,
                                const char *collection,
                                const char *rkey,
                                const unsigned char *record_cbor,
                                size_t record_cbor_len,
                                const wf_signing_key *key,
                                wf_cid *out_commit,
                                wf_cid *out_record);

wf_status wf_repo_delete_record(wf_car *car,
                                const wf_cid *prev_commit,
                                const char *did,
                                const char *collection,
                                const char *rkey,
                                const wf_signing_key *key,
                                wf_cid *out_commit);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_RECORD_H */