/**
 * mint_did.c — standalone tool to mint a new did:plc genesis operation.
 *
 * Usage: set env vars or edit defaults below, then build/run via CMake.
 */

#define _POSIX_C_SOURCE 200809L

#include "wolfram/plc.h"
#include "wolfram/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

static const char *default_handle = "plctest.bear.croft.click";
static const char *default_email = "contact@ewancroft.uk";
static const char *default_pds = "https://bear.croft.click";
static const char *default_plc = "https://plc.directory";

static int mint(const char *handle, const char *email, const char *pds_endpoint, const char *plc_url) {
    wf_signing_key rotation_key;
    wf_signing_key acct_key;
    char *rotation_didkey = NULL;
    char *acct_didkey = NULL;
    char *unsigned_json = NULL;
    char *signed_json = NULL;
    char *plc_did = NULL;
    int rc = 1;

    memset(&rotation_key, 0, sizeof(rotation_key));
    memset(&acct_key, 0, sizeof(acct_key));

    if (wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &rotation_key) != WF_OK) {
        fprintf(stderr, "failed to generate rotation key\n");
        goto cleanup;
    }
    if (wf_signing_key_public_didkey(&rotation_key, &rotation_didkey) != WF_OK) {
        fprintf(stderr, "failed to derive rotation did:key\n");
        goto cleanup;
    }
    if (wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &acct_key) != WF_OK) {
        fprintf(stderr, "failed to generate account key\n");
        goto cleanup;
    }
    if (wf_signing_key_public_didkey(&acct_key, &acct_didkey) != WF_OK) {
        fprintf(stderr, "failed to derive account did:key\n");
        goto cleanup;
    }

    char aka_buf[256];
    char services_buf[512];
    snprintf(aka_buf, sizeof(aka_buf), "at://%s", handle);
    snprintf(services_buf, sizeof(services_buf),
             "{\"atproto_pds\":{\"type\":\"AtprotoPersonalDataServer\","
             "\"endpoint\":\"%s\"}}",
             pds_endpoint);

    const char *rotation_keys[] = { rotation_didkey };
    wf_plc_operation_update update = {
        .rotation_keys = rotation_keys,
        .rotation_keys_count = 1,
        .verification_methods_json = NULL,
        .services_json = services_buf,
        .also_known_as = (const char *const[]){ aka_buf },
        .also_known_as_count = 1,
        .prev = NULL,
    };

    if (wf_plc_operation_build(&update, &unsigned_json) != WF_OK) {
        fprintf(stderr, "failed to build PLC operation\n");
        goto cleanup;
    }

    cJSON *root = cJSON_Parse(unsigned_json);
    if (!root) {
        fprintf(stderr, "failed to parse unsigned operation JSON\n");
        goto cleanup;
    }
    cJSON *verification = cJSON_GetObjectItemCaseSensitive(root, "verificationMethods");
    if (!cJSON_IsObject(verification)) {
        cJSON_Delete(root);
        fprintf(stderr, "unsigned operation missing verificationMethods\n");
        goto cleanup;
    }
    {
        cJSON *old = cJSON_DetachItemFromObjectCaseSensitive(verification, "atproto");
        if (old) cJSON_Delete(old);
    }
    if (!cJSON_AddStringToObject(verification, "atproto", acct_didkey)) {
        cJSON_Delete(root);
        fprintf(stderr, "failed to add atproto verification method\n");
        goto cleanup;
    }
    char *unsigned_with_key = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!unsigned_with_key) {
        fprintf(stderr, "failed to serialize unsigned operation\n");
        goto cleanup;
    }

    if (wf_plc_operation_sign(unsigned_with_key, &rotation_key, &signed_json) != WF_OK) {
        fprintf(stderr, "failed to sign PLC operation\n");
        goto cleanup;
    }

    if (wf_plc_operation_compute_did(signed_json, &plc_did) != WF_OK) {
        fprintf(stderr, "failed to compute PLC DID\n");
        goto cleanup;
    }

    fprintf(stderr, "submitting to %s for DID %s\n", plc_url, plc_did);
    if (wf_plc_submit_operation_raw(plc_url, plc_did, signed_json) != WF_OK) {
        fprintf(stderr, "failed to submit PLC operation to directory\n");
        goto cleanup;
    }

    printf("%s\t%s\t%s\n", plc_did, aka_buf, email);
    rc = 0;

cleanup:
    free(plc_did);
    free(signed_json);
    free(unsigned_with_key);
    free(unsigned_json);
    free(acct_didkey);
    free(rotation_didkey);
    memset(&acct_key, 0, sizeof(acct_key));
    memset(&rotation_key, 0, sizeof(rotation_key));
    return rc;
}

int main(void) {
    const char *handle = getenv("MINT_HANDLE") ? getenv("MINT_HANDLE") : default_handle;
    const char *email = getenv("MINT_EMAIL") ? getenv("MINT_EMAIL") : default_email;
    const char *pds = getenv("MINT_PDS_ENDPOINT") ? getenv("MINT_PDS_ENDPOINT") : default_pds;
    const char *plc = getenv("MINT_PLC_URL") ? getenv("MINT_PLC_URL") : default_plc;

    fprintf(stderr, "Minting did:plc for handle=%s email=%s pds=%s plc=%s\n",
            handle, email, pds, plc);
    return mint(handle, email, pds, plc);
}
