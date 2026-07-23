/**
 * test_plc.c — offline tests for PLC operation build / sign / verify.
 *
 * No network: exercises wf_plc_operation_build, wf_plc_operation_sign and
 * wf_plc_operation_verify round-tripping through the crypto primitives, plus
 * a did:key derivation check.
 */

#include "wolfram/plc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/crypto.h"

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static int build_and_sign_roundtrip(wf_key_type key_type) {
    wf_signing_key key;
    char *didkey = NULL;
    char *didkey2 = NULL;
    const char *also_known_as[] = {"at://alice.example", "at://alice.bsky.social"};
    const char *services_json =
        "{\"atproto_pds\":\"https://pds.example.com\"}";
    wf_plc_operation_update update = {
        .rotation_keys = NULL,
        .rotation_keys_count = 0,
        .verification_methods_json = NULL,
        .services_json = services_json,
        .also_known_as = also_known_as,
        .also_known_as_count = 2,
        .prev = "bafyreigh2abc123prevoperationcid",
    };
    char *op_json = NULL;
    char *signed_json = NULL;
    wf_status status;
    cJSON *op = NULL;
    cJSON *signed_root = NULL;
    int rc = 1;

    status = wf_signing_key_generate(key_type, &key);
    CHECK(status == WF_OK, "wf_signing_key_generate");

    status = wf_signing_key_public_didkey(&key, &didkey);
    CHECK(status == WF_OK, "wf_signing_key_public_didkey");
    CHECK(didkey != NULL, "didkey allocated");
    if (didkey) {
        CHECK(strncmp(didkey, "did:key:z", 9) == 0, "didkey has did:key:z prefix");
    }

    const char *rotation_keys[] = { didkey, "did:key:zQ3shABCrotation" };
    update.rotation_keys = rotation_keys;
    update.rotation_keys_count = 2;

    status = wf_plc_operation_build(&update, &op_json);
    CHECK(status == WF_OK, "wf_plc_operation_build");
    CHECK(op_json != NULL, "op_json allocated");

    op = cJSON_Parse(op_json);
    CHECK(op != NULL, "op_json parses");
    CHECK(cJSON_GetObjectItemCaseSensitive(op, "sig") == NULL,
          "unsigned op has no sig");
    CHECK(cJSON_GetObjectItemCaseSensitive(op, "type") != NULL,
          "op has type");
    cJSON_Delete(op);
    op = NULL;

    status = wf_plc_operation_sign(op_json, &key, &signed_json);
    CHECK(status == WF_OK, "wf_plc_operation_sign");
    CHECK(signed_json != NULL, "signed_json allocated");

    signed_root = cJSON_Parse(signed_json);
    CHECK(signed_root != NULL, "signed_json parses");
    if (signed_root) {
        const cJSON *sig =
            cJSON_GetObjectItemCaseSensitive(signed_root, "sig");
        CHECK(sig != NULL && cJSON_IsString(sig), "signed op has sig string");
        if (sig && didkey) {
            CHECK(sig->valuestring != NULL && sig->valuestring[0] != '\0',
                  "sig value is non-empty string");
        }
    }
    cJSON_Delete(signed_root);
    signed_root = NULL;

    /* Verify the signed operation; signer did:key must match derived key. */
    status = wf_plc_operation_verify(signed_json, &didkey2);
    CHECK(status == WF_OK, "wf_plc_operation_verify");
    CHECK(didkey2 != NULL, "verify returns signer didkey");
    if (didkey && didkey2) {
        CHECK(strcmp(didkey, didkey2) == 0,
              "verify signer didkey matches derived didkey");
    }

    /* Tamper detection: flipping a field must break verification. */
    {
        cJSON *t = cJSON_Parse(signed_json);
        cJSON *aka = cJSON_GetObjectItemCaseSensitive(t, "alsoKnownAs");
        if (aka && cJSON_IsArray(aka) && aka->child) {
            cJSON_SetValuestring(aka->child, "at://tampered.example");
        }
        char *tampered = cJSON_PrintUnformatted(t);
        cJSON_Delete(t);
        char *tamper_did = NULL;
        wf_status tstatus = wf_plc_operation_verify(tampered, &tamper_did);
        CHECK(tstatus != WF_OK, "tampered op fails verification");
        free(tamper_did);
        free(tampered);
    }

    rc = failures == 0 ? 0 : 1;

    wf_plc_operation_free(op_json);
    wf_plc_operation_free(signed_json);
    free(didkey);
    free(didkey2);
    return rc;
}

int main(void) {
    /* P-256 is always available (OpenSSL). */
    build_and_sign_roundtrip(WF_KEY_TYPE_P256);
#ifdef HAVE_LIBSECP256K1
    build_and_sign_roundtrip(WF_KEY_TYPE_SECP256K1);
#endif
    if (failures == 0) {
        printf("plc: all tests passed\n");
        return 0;
    }
    printf("plc: %d check(s) failed\n", failures);
    return 1;
}
