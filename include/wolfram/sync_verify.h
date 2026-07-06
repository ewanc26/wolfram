#ifndef WOLFRAM_SYNC_VERIFY_H
#define WOLFRAM_SYNC_VERIFY_H

#include "wolfram/sync_subscribe.h"
#include "wolfram/repo/diff.h"
#include "wolfram/identity.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verify a firehose commit event.
 *
 * Parses the CAR from commit->blocks, resolves the repo's DID to
 * obtain the signing key, and verifies the commit signature, block
 * integrity, and MST structure.
 *
 * @param commit    The commit event from the firehose.
 * @param client    XRPC client used for DID resolution (HTTP transport).
 * @param out_verified  Set to 1 if verification succeeded, 0 otherwise.
 * @param out_commit    On success, the parsed and verified commit struct.
 *
 * @return WF_OK on success (even if signature verification failed;
 *         check *out_verified), or an error status if parsing/resolution
 *         failed.
 */
wf_status wf_sync_verify_commit(const wf_subscribe_commit *commit,
                                 wf_xrpc_client *client,
                                 int *out_verified,
                                 wf_commit *out_commit);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SYNC_VERIFY_H */
