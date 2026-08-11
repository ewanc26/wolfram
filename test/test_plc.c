/**
 * test_plc.c — offline tests for PLC operation build / sign / verify.
 *
 * No network: exercises wf_plc_operation_build, wf_plc_operation_sign and
 * wf_plc_operation_verify round-tripping through the crypto primitives, plus
 * a did:key derivation check. wf_plc_get_last_op / wf_plc_build_handle_update
 * are exercised via wf_xrpc_set_handler's offline test seam, same as the
 * identity resolver tests -- no real network I/O.
 */

#include "wolfram/plc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/crypto.h"
#include "wolfram/repo/cid.h"
#include "wolfram/xrpc.h"

static int failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int build_and_sign_roundtrip(wf_key_type key_type) {
    wf_signing_key key;
    char *didkey = NULL;
    char *didkey2 = NULL;
    const char *also_known_as[] = {"at://alice.example",
                                   "at://alice.bsky.social"};
    const char *services_json = "{\"atproto_pds\":\"https://pds.example.com\"}";
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
        CHECK(strncmp(didkey, "did:key:z", 9) == 0,
              "didkey has did:key:z prefix");
    }

    const char *rotation_keys[] = {didkey, "did:key:zQ3shABCrotation"};
    update.rotation_keys = rotation_keys;
    update.rotation_keys_count = 2;

    status = wf_plc_operation_build(&update, &op_json);
    CHECK(status == WF_OK, "wf_plc_operation_build");
    CHECK(op_json != NULL, "op_json allocated");

    op = cJSON_Parse(op_json);
    CHECK(op != NULL, "op_json parses");
    CHECK(cJSON_GetObjectItemCaseSensitive(op, "sig") == NULL,
          "unsigned op has no sig");
    CHECK(cJSON_GetObjectItemCaseSensitive(op, "type") != NULL, "op has type");
    cJSON_Delete(op);
    op = NULL;

    status = wf_plc_operation_sign(op_json, &key, &signed_json);
    CHECK(status == WF_OK, "wf_plc_operation_sign");
    CHECK(signed_json != NULL, "signed_json allocated");

    signed_root = cJSON_Parse(signed_json);
    CHECK(signed_root != NULL, "signed_json parses");
    if (signed_root) {
        const cJSON *sig = cJSON_GetObjectItemCaseSensitive(signed_root, "sig");
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

/* Canned "current operation" served by log_last_handler below. Set by the
 * test before installing the handler -- its rotationKeys must actually name
 * the key the test signs the handle-update op with, or verification will
 * (correctly) reject a signer that the op itself doesn't list. */
static char *g_canned_last_op = NULL;

/* Test seam handler (see wf_xrpc_set_handler): serves the canned operation
 * above for any GET, so wf_plc_get_last_op / wf_plc_build_handle_update can
 * be exercised without a real PLC directory. */
static wf_status log_last_handler(void *userdata, const char *method,
                                  const char *url, const char *content_type,
                                  const char *body, size_t body_len,
                                  const wf_http_header *headers,
                                  size_t header_count, wf_response *out) {
    (void)userdata;
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;

    size_t len = strlen(g_canned_last_op);
    out->body = malloc(len + 1);
    if (!out->body) return WF_ERR_ALLOC;
    memcpy(out->body, g_canned_last_op, len + 1);
    out->body_len = len;
    out->status = 200;
    return WF_OK;
}

static void test_get_last_op_and_build_handle_update(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("https://plc.example.invalid");
    CHECK(client != NULL, "wf_xrpc_client_new");
    if (!client) return;

    /* The rotation key must be generated before the canned op is built: the
     * canned op's rotationKeys must actually name it, or a real
     * wf_plc_operation_verify (correctly) rejects a signer the op doesn't
     * list -- this is not a fake for the test's sake, it is how PLC
     * verification is supposed to work. */
    wf_signing_key rotation_key;
    CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &rotation_key) == WF_OK,
          "wf_signing_key_generate (handle update)");
    char *rotation_didkey = NULL;
    CHECK(wf_signing_key_public_didkey(&rotation_key, &rotation_didkey) ==
              WF_OK,
          "wf_signing_key_public_didkey (handle update)");
    if (!rotation_didkey) {
        wf_xrpc_client_free(client);
        return;
    }

    char canned[1024];
    snprintf(canned, sizeof(canned),
             "{\"type\":\"plc_operation\",\"rotationKeys\":[\"%s\"],"
             "\"verificationMethods\":{\"atproto\":\"did:key:"
             "zQ3shExampleAtprotoKey\"},\"services\":{\"atproto_pds\":{"
             "\"type\":\"AtprotoPersonalDataServer\",\"endpoint\":\"https://"
             "old.example.com\"}},\"alsoKnownAs\":[\"at://old-handle."
             "example\"],\"prev\":null,\"sig\":\"fakeSigValueForTestingOnly\"}",
             rotation_didkey);
    g_canned_last_op = canned;
    wf_xrpc_set_handler(client, log_last_handler, NULL);

    char *cid = NULL;
    char *op_json = NULL;
    wf_status status =
        wf_plc_get_last_op(client, "https://plc.example.invalid",
                           "did:plc:testaccount123456789", &cid, &op_json);
    CHECK(status == WF_OK, "wf_plc_get_last_op");
    CHECK(cid != NULL && cid[0] != '\0', "wf_plc_get_last_op returns a CID");
    if (cid) {
        wf_cid parsed;
        CHECK(wf_cid_from_string(cid, &parsed) == WF_OK,
              "returned CID string parses as a real CID");
    }
    CHECK(op_json != NULL && strcmp(op_json, g_canned_last_op) == 0,
          "wf_plc_get_last_op returns the served operation verbatim");
    free(cid);
    free(op_json);

    char *signed_json = NULL;
    status = wf_plc_build_handle_update(
        client, "https://plc.example.invalid", "did:plc:testaccount123456789",
        "new-handle.example", &rotation_key, &signed_json);
    CHECK(status == WF_OK, "wf_plc_build_handle_update");
    CHECK(signed_json != NULL, "wf_plc_build_handle_update output allocated");

    if (signed_json) {
        cJSON *op = cJSON_Parse(signed_json);
        CHECK(op != NULL, "handle update op parses");
        if (op) {
            /* Preserved from the canned last op, unchanged. */
            cJSON *rk = cJSON_GetObjectItemCaseSensitive(op, "rotationKeys");
            CHECK(cJSON_IsArray(rk) && cJSON_GetArraySize(rk) == 1,
                  "rotationKeys preserved (count)");
            if (cJSON_IsArray(rk) && cJSON_GetArraySize(rk) == 1) {
                cJSON *first = cJSON_GetArrayItem(rk, 0);
                CHECK(cJSON_IsString(first) &&
                          strcmp(first->valuestring, rotation_didkey) == 0,
                      "rotationKeys preserved (value)");
            }
            cJSON *vm =
                cJSON_GetObjectItemCaseSensitive(op, "verificationMethods");
            cJSON *vm_atproto =
                vm ? cJSON_GetObjectItemCaseSensitive(vm, "atproto") : NULL;
            CHECK(cJSON_IsString(vm_atproto) &&
                      strcmp(vm_atproto->valuestring,
                             "did:key:zQ3shExampleAtprotoKey") == 0,
                  "verificationMethods preserved");
            cJSON *services = cJSON_GetObjectItemCaseSensitive(op, "services");
            cJSON *pds =
                services
                    ? cJSON_GetObjectItemCaseSensitive(services, "atproto_pds")
                    : NULL;
            cJSON *endpoint =
                pds ? cJSON_GetObjectItemCaseSensitive(pds, "endpoint") : NULL;
            CHECK(cJSON_IsString(endpoint) &&
                      strcmp(endpoint->valuestring,
                             "https://old.example.com") == 0,
                  "services preserved");

            /* Changed: alsoKnownAs, and prev now points at the canned op. */
            cJSON *aka = cJSON_GetObjectItemCaseSensitive(op, "alsoKnownAs");
            CHECK(cJSON_IsArray(aka) && cJSON_GetArraySize(aka) == 1,
                  "alsoKnownAs has exactly the new handle");
            if (cJSON_IsArray(aka) && cJSON_GetArraySize(aka) == 1) {
                cJSON *first = cJSON_GetArrayItem(aka, 0);
                CHECK(cJSON_IsString(first) &&
                          strcmp(first->valuestring,
                                 "at://new-handle.example") == 0,
                      "alsoKnownAs is at://new-handle.example");
            }
            cJSON *prev = cJSON_GetObjectItemCaseSensitive(op, "prev");
            CHECK(cJSON_IsString(prev) && prev->valuestring[0] != '\0',
                  "prev is set to the fetched last-op CID");

            /* Signed with the given rotation key, and verifiable. */
            char *verify_didkey = NULL;
            wf_status vstatus =
                wf_plc_operation_verify(signed_json, &verify_didkey);
            CHECK(vstatus == WF_OK, "handle update op verifies");
            if (verify_didkey && rotation_didkey) {
                CHECK(strcmp(verify_didkey, rotation_didkey) == 0,
                      "handle update op signed by the given rotation key");
            }
            free(verify_didkey);
        }
        cJSON_Delete(op);
    }

    free(rotation_didkey);
    wf_plc_operation_free(signed_json);
    wf_xrpc_client_free(client);
    g_canned_last_op = NULL; /* was pointing at this function's stack buffer */
}

int main(void) {
    /* P-256 is always available (OpenSSL). */
    build_and_sign_roundtrip(WF_KEY_TYPE_P256);
#ifdef HAVE_LIBSECP256K1
    build_and_sign_roundtrip(WF_KEY_TYPE_SECP256K1);
#endif
    test_get_last_op_and_build_handle_update();
    if (failures == 0) {
        printf("plc: all tests passed\n");
        return 0;
    }
    printf("plc: %d check(s) failed\n", failures);
    return 1;
}
