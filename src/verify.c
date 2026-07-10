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

/* Process-wide DID-key resolver. NULL until wf_verify_set_key_resolver is
 * called. Stored as plain global state: install once at startup before any
 * verification runs. */
static wf_verify_key_resolver g_resolver = NULL;
static void *g_resolver_userdata = NULL;

void wf_verify_set_key_resolver(wf_verify_key_resolver cb, void *userdata)
{
    g_resolver = cb;
    g_resolver_userdata = userdata;
}

/* Resolve `did`'s signing key strictly through the installed resolver. No
 * network fallback: absence of a resolver is the honest "no capability"
 * error. On WF_OK, *out_key is heap-allocated and owned by the caller. */
static wf_status resolve_via_resolver(const char *did,
                                      const char *key_id,
                                      char **out_key)
{
    if (!out_key)
        return WF_ERR_INVALID_ARG;
    *out_key = NULL;

    if (!did || did[0] == '\0')
        return WF_ERR_INVALID_ARG;

    if (!g_resolver)
        return WF_ERR_INVALID_ARG; /* honest: no resolver, no key source */

    return g_resolver(did, key_id, g_resolver_userdata, out_key);
}

wf_status wf_verify_resolve_via_did(const char *did,
                                    const char *key_id,
                                    void *userdata,
                                    char **out_key)
{
    (void)key_id;

    wf_xrpc_client *client = (wf_xrpc_client *)userdata;
    if (!did || did[0] == '\0' || !out_key)
        return WF_ERR_INVALID_ARG;
    if (!client)
        return WF_ERR_INVALID_ARG;
    *out_key = NULL;

    wf_did_document doc;
    memset(&doc, 0, sizeof(doc));
    wf_status s = wf_did_resolve(client, did, &doc);
    if (s != WF_OK)
        return s;
    if (!doc.signing_key) {
        wf_did_document_free(&doc);
        return WF_ERR_NOT_FOUND;
    }

    /* Steal the signing_key string out of the document so the caller owns
     * it directly (wf_did_document_free would otherwise free it). */
    *out_key = doc.signing_key;
    doc.signing_key = NULL;
    wf_did_document_free(&doc);
    return WF_OK;
}

wf_status wf_verify_resolve_signing_key(const char *did,
                                        const char *key_id,
                                        wf_xrpc_client *client,
                                        char **out_key)
{
    if (!out_key)
        return WF_ERR_INVALID_ARG;
    *out_key = NULL;

    /* Prefer the injected resolver. */
    wf_status s = resolve_via_resolver(did, key_id, out_key);
    if (s != WF_ERR_INVALID_ARG)
        return s;

    /* No resolver installed: fall back to a live wf_did_resolve when a
     * transport client is available. Without both, resolution is impossible. */
    if (!client)
        return WF_ERR_INVALID_ARG;

    wf_did_document doc;
    memset(&doc, 0, sizeof(doc));
    s = wf_did_resolve(client, did, &doc);
    if (s != WF_OK)
        return s;
    if (!doc.signing_key) {
        wf_did_document_free(&doc);
        return WF_ERR_NOT_FOUND;
    }
    *out_key = doc.signing_key;
    doc.signing_key = NULL;
    wf_did_document_free(&doc);
    return WF_OK;
}

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

wf_status wf_verify_record_commit_resolved(const char *did,
                                           const uint8_t *commit_cbor,
                                           size_t commit_len,
                                           int *out_valid)
{
    if (!out_valid)
        return WF_ERR_INVALID_ARG;
    *out_valid = 0;

    if (!did || did[0] == '\0' ||
        !commit_cbor || commit_len == 0)
        return WF_ERR_INVALID_ARG;

    /* Fetch the DID's signing key from the injected resolver. With no
     * resolver installed this returns an honest WF_ERR_INVALID_ARG — no
     * fabricated key, no false pass. */
    char *signing_key = NULL;
    wf_status s = resolve_via_resolver(did, NULL, &signing_key);
    if (s != WF_OK)
        return s;

    s = wf_verify_record_commit(signing_key, commit_cbor, commit_len, out_valid);

    free(signing_key);
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

    /* Resolve the DID's signing key via the injected resolver. Without a
     * resolver wolfram cannot fetch the key, so it returns an honest error
     * rather than fabricating a signature check. */
    char *signing_key = NULL;
    wf_status s = resolve_via_resolver(did, NULL, &signing_key);
    if (s != WF_OK)
        return s;

    /* Confirm the record is retrievable and fetch its commit CAR. */
    wf_agent_sync_auth(agent);

    wf_lex_com_atproto_repo_get_record_main_params rp = {0};
    rp.repo = did;
    rp.collection = collection;
    rp.rkey = rkey;
    wf_response rec = {0};
    s = wf_lex_com_atproto_repo_get_record_main_call(agent->client, &rp, &rec);
    wf_response_free(&rec);
    if (s != WF_OK) {
        free(signing_key);
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
        free(signing_key);
        return s;
    }

    s = wf_verify_record_commit(signing_key,
                                (const uint8_t *)res.body,
                                res.body_len,
                                out_valid);

    wf_response_free(&res);
    free(signing_key);
    return s;
}
