#ifndef WOLFRAM_REPO_DIFF_H
#define WOLFRAM_REPO_DIFF_H

#include <stddef.h>
#include "wolfram/repo/commit.h"
#include "wolfram/repo/mst.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_repo_verify_options {
    const char *expected_did;
    const char *signing_key;
    const wf_cid *expected_prev;
} wf_repo_verify_options;

typedef enum wf_repo_operation_action {
    WF_REPO_CREATE = 0,
    WF_REPO_UPDATE = 1,
    WF_REPO_DELETE = 2,
} wf_repo_operation_action;

typedef struct wf_repo_operation {
    wf_repo_operation_action action;
    char *collection;
    char *rkey;
    wf_cid cid;
    wf_cid prev;
} wf_repo_operation;

void wf_repo_operations_free(wf_repo_operation *operations, size_t count);
wf_status wf_repo_operations_invert(const wf_repo_operation *operations,
                                    size_t count,
                                    wf_repo_operation **out_operations);

typedef struct wf_repo_diff {
    wf_repo_operation *operations;
    size_t operation_count;
    wf_commit commit;
    wf_cid previous_commit;
    char since[64];
    wf_car new_blocks;
    wf_cid *removed_cids;
    size_t removed_count;
} wf_repo_diff;

void wf_repo_diff_free(wf_repo_diff *diff);
wf_status wf_repo_diff_verify(const wf_car *base,
                              const wf_cid *base_commit,
                              const wf_car *update,
                              const wf_repo_verify_options *options,
                              wf_repo_diff *out);
wf_status wf_repo_diff_apply(wf_car *repo, const wf_repo_diff *diff);
wf_status wf_repo_verify(const wf_car *car,
                         const wf_repo_verify_options *options,
                         wf_commit *out_commit);
wf_status wf_repo_import(const unsigned char *bytes, size_t len,
                         const wf_repo_verify_options *options,
                         wf_car *out_car, wf_commit *out_commit);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_DIFF_H */