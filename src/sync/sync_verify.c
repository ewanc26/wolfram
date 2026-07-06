#include "wolfram/sync_verify.h"
#include "wolfram/xrpc.h"

#include <string.h>

wf_status wf_sync_verify_commit(const wf_subscribe_commit *commit,
                                 wf_xrpc_client *client,
                                 int *out_verified,
                                 wf_commit *out_commit)
{
    if (!commit || !client || !out_verified || !out_commit)
        return WF_ERR_INVALID_ARG;

    *out_verified = 0;
    memset(out_commit, 0, sizeof(*out_commit));

    if (!commit->blocks || commit->blocks_len == 0)
        return WF_ERR_INVALID_ARG;

    /* Parse the CAR from the commit blocks */
    wf_car car;
    memset(&car, 0, sizeof(car));
    wf_status s = wf_car_parse(commit->blocks, commit->blocks_len, &car);
    if (s != WF_OK) return s;

    /* Resolve the repo DID to get the signing key */
    wf_did_document doc;
    memset(&doc, 0, sizeof(doc));
    s = wf_did_resolve(client, commit->did, &doc);
    if (s != WF_OK) {
        wf_car_free(&car);
        return s;
    }

    if (!doc.signing_key) {
        wf_did_document_free(&doc);
        wf_car_free(&car);
        return WF_ERR_INVALID_ARG;
    }

    /* Verify the CAR using the DID's signing key */
    wf_repo_verify_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.expected_did = commit->did;
    opts.signing_key = doc.signing_key;

    s = wf_repo_verify(&car, &opts, out_commit);
    if (s == WF_OK)
        *out_verified = 1;

    wf_did_document_free(&doc);
    wf_car_free(&car);
    return s;
}
