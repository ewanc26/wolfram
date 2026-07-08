#include "wolfram/verify.h"

#include "wolfram/repo/diff.h"
#include "wolfram/repo/car.h"
#include "wolfram/repo/commit.h"
#include "wolfram/repo/cid.h"
#include "wolfram/identity.h"
#include "wolfram/atproto_lex.h"

#include <stdlib.h>
#include <string.h>

/* Live path needs access to the agent's XRPC client and the auth-sync
 * helper. These live in the agent's private internal header. */
#include "agent/_internal.h"

wf_status wf_verify_record_commit(const char *signing_key_multibase,
                                  const uint8_t *commit_cbor,
                                  size_t commit_len,
                                  int *out_valid)
{
    if (!out_valid)
        return WF_ERR_INVALID_ARG;
    *out_valid = 0;

    if (!signing_key_multibase || signing_key_multibase[0] == '\0' ||
        !commit_cbor || commit_len == 0)
        return WF_ERR_INVALID_ARG;

    wf_car car;
    memset(&car, 0, sizeof(car));
    wf_status s = wf_car_parse(commit_cbor, commit_len, &car);
    if (s != WF_OK)
        return s;

    int valid = 0;

    if (car.root_count == 1) {
        wf_car_block *root = wf_car_find_block(&car, &car.roots[0]);
        if (root) {
            wf_commit commit;
            if (wf_commit_parse(root->data, root->data_len, &commit) == WF_OK) {
                /* Reuse the exact verification core behind
                 * wf_sync_verify_commit (wf_repo_verify): it checks the
                 * signature over the commit minus `sig` and the MST links,
                 * anchored to the expected DID parsed from the commit. */
                wf_repo_verify_options opts;
                memset(&opts, 0, sizeof(opts));
                opts.expected_did = commit.did;
                opts.signing_key = signing_key_multibase;

                wf_commit out;
                s = wf_repo_verify(&car, &opts, &out);
                if (s == WF_OK)
                    valid = 1;
            } else {
                s = WF_ERR_PARSE;
            }
        } else {
            s = WF_ERR_NOT_FOUND;
        }
    } else {
        s = WF_ERR_PARSE;
    }

    *out_valid = valid;
    wf_car_free(&car);
    return s;
}

wf_status wf_agent_verify_record(wf_agent *agent,
                                 const char *did,
                                 const char *collection,
                                 const char *rkey,
                                 int *out_valid)
{
    if (!out_valid)
        return WF_ERR_INVALID_ARG;
    *out_valid = 0;

    if (!agent || !did || did[0] == '\0' ||
        !collection || collection[0] == '\0' ||
        !rkey || rkey[0] == '\0')
        return WF_ERR_INVALID_ARG;

    /* TODO: live resolution + fetch path. Enable by building with
     * -DWF_TEST_LIVE and/or setting WF_TEST_LIVE=1 at runtime. Until then
     * this returns WF_ERR_INVALID_ARG rather than a fabricated result. */
#ifndef WF_TEST_LIVE
    (void)agent;
    (void)did;
    (void)collection;
    (void)rkey;
    return WF_ERR_INVALID_ARG;
#else
    if (!getenv("WF_TEST_LIVE"))
        return WF_ERR_INVALID_ARG;

    wf_agent_sync_auth(agent);

    wf_did_document doc;
    memset(&doc, 0, sizeof(doc));
    wf_status s = wf_did_resolve(agent->client, did, &doc);
    if (s != WF_OK)
        return s;
    if (!doc.signing_key) {
        wf_did_document_free(&doc);
        return WF_ERR_INVALID_ARG;
    }

    /* Confirm the record is retrievable. */
    wf_lex_com_atproto_repo_get_record_main_params rp = {0};
    rp.repo = did;
    rp.collection = collection;
    rp.rkey = rkey;
    wf_response rec = {0};
    s = wf_lex_com_atproto_repo_get_record_main_call(agent->client, &rp, &rec);
    wf_response_free(&rec);
    if (s != WF_OK) {
        wf_did_document_free(&doc);
        return s;
    }

    /* Fetch the commit CAR for the record and verify its signature. */
    wf_lex_com_atproto_sync_get_record_main_params sp = {0};
    sp.did = did;
    sp.collection = collection;
    sp.rkey = rkey;
    wf_response res = {0};
    s = wf_lex_com_atproto_sync_get_record_main_call(agent->client, &sp, &res);
    if (s != WF_OK) {
        wf_did_document_free(&doc);
        return s;
    }

    s = wf_verify_record_commit(doc.signing_key,
                                (const uint8_t *)res.body,
                                res.body_len,
                                out_valid);

    wf_response_free(&res);
    wf_did_document_free(&doc);
    return s;
#endif
}
