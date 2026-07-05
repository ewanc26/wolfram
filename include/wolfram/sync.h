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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SYNC_H */
