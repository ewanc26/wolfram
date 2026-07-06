#ifndef WOLFRAM_REPO_COMMIT_H
#define WOLFRAM_REPO_COMMIT_H

#include <stddef.h>
#include "wolfram/xrpc.h"
#include "wolfram/repo/cid.h"
#include "wolfram/repo/car.h"
#include "wolfram/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A commit object. */
typedef struct wf_commit {
    char did[256];
    int version;
    wf_cid data;
    char rev[64];
    wf_cid prev;
    int has_prev;
    wf_cid cid;
    unsigned char sig[64];
    size_t sig_len;
} wf_commit;

wf_status wf_commit_parse(const unsigned char *cbor, size_t len,
                          wf_commit *out);
wf_status wf_commit_create(const char *did, const char *rev,
                           const wf_cid *data,
                           const wf_cid *prev,
                           const wf_signing_key *key,
                           wf_car *car,
                           wf_commit *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_COMMIT_H */