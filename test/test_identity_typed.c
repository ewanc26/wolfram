/*
 * test_identity_typed.c — offline tests for the com.atproto.identity typed
 * parsers. Hardcodes representative response bodies and asserts the owned
 * structs are populated correctly, then freed. Agent wrappers require live
 * auth and are exercised only for NULL-argument validation.
 */

#include "wolfram/identity_typed.h"
#include "wolfram/plc.h"
#include "wolfram/crypto.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

/* com.atproto.identity.resolveHandle output. */
static const char *kResolveHandleJson =
    "{ \"did\": \"did:plc:alice00000000000000000000\" }";

/* com.atproto.identity.resolveDid output (didDoc with handle, a
 * verificationMethod, and a service). */
static const char *kResolveDidJson =
    "{"
    "  \"didDoc\": {"
    "    \"@context\": [\"https://www.w3.org/ns/did/v1\"],"
    "    \"id\": \"did:plc:alice00000000000000000000\","
    "    \"alsoKnownAs\": [\"at://alice.bsky.social\"],"
    "    \"verificationMethod\": ["
    "      {"
    "        \"id\": \"did:plc:alice00000000000000000000#atproto\","
    "        \"type\": \"Multikey\","
    "        \"controller\": \"did:plc:alice00000000000000000000\","
    "        \"publicKeyMultibase\": \"zAliceMultibaseValue\""
    "      }"
    "    ],"
    "    \"service\": ["
    "      {"
    "        \"id\": \"#atproto_pds\","
    "        \"type\": \"AtprotoPds\","
    "        \"serviceEndpoint\": \"https://pds.example.com\""
    "      }"
    "    ]"
    "  }"
    "}";

/* com.atproto.identity.getRecommendedDidCredentials output. */
static const char *kRecommendedJson =
    "{"
    "  \"rotationKeys\": [\"did:key:zRotationKey\"],"
    "  \"alsoKnownAs\": [\"at://alice.bsky.social\"],"
    "  \"verificationMethods\": { \"atproto\": \"did:key:zVerifyKey\" },"
    "  \"services\": { \"atproto_pds\": \"https://pds.example.com\" }"
    "}";

/* com.atproto.identity.signPlcOperation output. */
static const char *kSignPlcOpJson =
    "{"
    "  \"operation\": {"
    "    \"type\": \"plc_operation\","
    "    \"rotationKeys\": [\"did:key:zRotationKey\"],"
    "    \"verificationMethods\": { \"atproto\": \"did:key:zVerifyKey\" },"
    "    \"services\": { \"atproto_pds\": \"https://pds.example.com\" },"
    "    \"alsoKnownAs\": [\"at://alice.bsky.social\"],"
    "    \"prev\": null,"
    "    \"sig\": { \"did:key:zRotationKey\": \"AAAA\" }"
    "  }"
    "}";

/* com.atproto.identity.resolveIdentity output. */
static const char *kResolveIdentityJson =
    "{"
    "  \"did\": \"did:plc:alice00000000000000000000\","
    "  \"handle\": \"alice.bsky.social\","
    "  \"didDoc\": {"
    "    \"id\": \"did:plc:alice00000000000000000000\","
    "    \"alsoKnownAs\": [\"at://alice.bsky.social\"]"
    "  }"
    "}";

int main(void) {
    /* ---- Invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_identity_resolve_handle rh = {0};
    WF_CHECK(wf_identity_parse_resolve_handle(NULL, 0, &rh) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_parse_resolve_handle(kResolveHandleJson,
                                              strlen(kResolveHandleJson),
                                              NULL) == WF_ERR_INVALID_ARG);

    wf_identity_resolve_did rd = {0};
    WF_CHECK(wf_identity_parse_resolve_did(NULL, 0, &rd) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_parse_resolve_did(kResolveDidJson,
                                           strlen(kResolveDidJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_identity_recommended_credentials rc = {0};
    WF_CHECK(wf_identity_parse_get_recommended_did_credentials(
                 NULL, 0, &rc) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_parse_get_recommended_did_credentials(
                 kRecommendedJson, strlen(kRecommendedJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_identity_signed_operation so = {0};
    WF_CHECK(wf_identity_parse_sign_plc_operation(NULL, 0, &so) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_parse_sign_plc_operation(kSignPlcOpJson,
                                                  strlen(kSignPlcOpJson),
                                                  NULL) == WF_ERR_INVALID_ARG);

    wf_identity_resolve_identity ri = {0};
    WF_CHECK(wf_identity_parse_resolve_identity(NULL, 0, &ri) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_identity_parse_resolve_identity(kResolveIdentityJson,
                                                strlen(kResolveIdentityJson),
                                                NULL) == WF_ERR_INVALID_ARG);

    /* ---- resolveHandle ---- */
    WF_CHECK(wf_identity_parse_resolve_handle(
                 kResolveHandleJson, strlen(kResolveHandleJson), &rh) == WF_OK);
    WF_CHECK(rh.did &&
              strcmp(rh.did, "did:plc:alice00000000000000000000") == 0);
    wf_identity_resolve_handle_free(&rh);
    WF_CHECK(rh.did == NULL);

    /* ---- resolveDid ---- */
    WF_CHECK(wf_identity_parse_resolve_did(kResolveDidJson,
                                           strlen(kResolveDidJson), &rd) ==
             WF_OK);
    WF_CHECK(rd.handle && strcmp(rd.handle, "alice.bsky.social") == 0);
    WF_CHECK(rd.verification_method_count == 1);
    WF_CHECK(rd.verification_methods[0].id &&
              strcmp(rd.verification_methods[0].id,
                     "did:plc:alice00000000000000000000#atproto") == 0);
    WF_CHECK(rd.verification_methods[0].type &&
              strcmp(rd.verification_methods[0].type, "Multikey") == 0);
    WF_CHECK(rd.verification_methods[0].public_key_multibase &&
              strcmp(rd.verification_methods[0].public_key_multibase,
                     "zAliceMultibaseValue") == 0);
    WF_CHECK(rd.service_count == 1);
    WF_CHECK(rd.services[0].id &&
              strcmp(rd.services[0].id, "#atproto_pds") == 0);
    WF_CHECK(rd.services[0].type &&
              strcmp(rd.services[0].type, "AtprotoPds") == 0);
    WF_CHECK(rd.services[0].service_endpoint_json &&
              strcmp(rd.services[0].service_endpoint_json,
                     "\"https://pds.example.com\"") == 0);
    wf_identity_resolve_did_free(&rd);
    WF_CHECK(rd.handle == NULL && rd.verification_methods == NULL &&
             rd.services == NULL && rd.verification_method_count == 0 &&
             rd.service_count == 0);

    /* ---- getRecommendedDidCredentials ---- */
    WF_CHECK(wf_identity_parse_get_recommended_did_credentials(
                 kRecommendedJson, strlen(kRecommendedJson), &rc) == WF_OK);
    WF_CHECK(rc.rotation_key_count == 1);
    WF_CHECK(rc.rotation_keys[0] &&
              strcmp(rc.rotation_keys[0], "did:key:zRotationKey") == 0);
    WF_CHECK(rc.also_known_as_count == 1);
    WF_CHECK(rc.also_known_as[0] &&
              strcmp(rc.also_known_as[0], "at://alice.bsky.social") == 0);
    WF_CHECK(rc.verification_methods_json &&
              strcmp(rc.verification_methods_json,
                     "{\"atproto\":\"did:key:zVerifyKey\"}") == 0);
    WF_CHECK(rc.services_json &&
              strcmp(rc.services_json,
                     "{\"atproto_pds\":\"https://pds.example.com\"}") == 0);
    wf_identity_recommended_credentials_free(&rc);
    WF_CHECK(rc.rotation_keys == NULL && rc.also_known_as == NULL &&
             rc.verification_methods_json == NULL && rc.services_json == NULL);

    /* ---- signPlcOperation ---- */
    WF_CHECK(wf_identity_parse_sign_plc_operation(
                 kSignPlcOpJson, strlen(kSignPlcOpJson), &so) == WF_OK);
    WF_CHECK(so.operation_json && strstr(so.operation_json, "plc_operation") &&
             strstr(so.operation_json, "sig"));
    wf_identity_signed_operation_free(&so);
    WF_CHECK(so.operation_json == NULL);

    /* ---- resolveIdentity ---- */
    WF_CHECK(wf_identity_parse_resolve_identity(
                 kResolveIdentityJson, strlen(kResolveIdentityJson), &ri) ==
             WF_OK);
    WF_CHECK(ri.did &&
              strcmp(ri.did, "did:plc:alice00000000000000000000") == 0);
    WF_CHECK(ri.handle && strcmp(ri.handle, "alice.bsky.social") == 0);
    WF_CHECK(ri.did_doc_json && strstr(ri.did_doc_json, "did:plc:alice"));
    wf_identity_resolve_identity_free(&ri);
    WF_CHECK(ri.did == NULL && ri.handle == NULL && ri.did_doc_json == NULL);

    /* ---- Agent wrapper NULL validation (no live session; only NULL agents
       are passed so the wrappers never dereference a fake pointer). ---- */
    const char *keys[] = {"did:key:zRotationKey"};
    const char *akas[] = {"at://alice.bsky.social"};
    wf_identity_resolve_handle rh2 = {0};
    wf_identity_resolve_did rd2 = {0};
    wf_identity_recommended_credentials rc2 = {0};
    wf_identity_signed_operation so2 = {0};
    wf_identity_resolve_identity ri2 = {0};

    WF_CHECK(wf_agent_resolve_handle_typed(NULL, "h", &rh2) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_handle_typed(NULL, NULL, &rh2) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_handle_typed(NULL, "h", NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_did_typed(NULL, "did:plc:x", &rd2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_did_typed(NULL, NULL, &rd2) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_did_typed(NULL, "did:plc:x", NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_update_handle_typed(NULL, "h") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_update_handle_typed(NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_recommended_did_credentials_typed(NULL, &rc2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_recommended_did_credentials_typed(NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_request_plc_operation_signature_typed(NULL, "did:plc:x") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_request_plc_operation_signature_typed(NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_sign_plc_operation_typed(NULL, "tok", keys, 1, akas, 1,
                                         "{}", "{}", &so2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_sign_plc_operation_typed(NULL, NULL, NULL, 0, NULL, 0, NULL,
                                         NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_submit_plc_operation_typed(NULL, "{}") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_submit_plc_operation_typed(NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_identity(NULL, "id", &ri2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_identity(NULL, NULL, &ri2) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_resolve_identity(NULL, "id", NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_refresh_identity(NULL, "id") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_refresh_identity(NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_identity_rotate_handle(NULL, "h", "tok") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_identity_rotate_handle(NULL, NULL, "tok") ==
             WF_ERR_INVALID_ARG);
    /* Missing out-of-band token returns an honest error (no crash). */
    WF_CHECK(wf_agent_identity_rotate_handle(NULL, "h", NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_identity_rotate_handle(NULL, "h", "") ==
             WF_ERR_INVALID_ARG);

    /* ---- Offline handle-rotation build + sign (no network). Exercises the
       same wf_plc_operation_build input path used inside
       wf_agent_identity_rotate_handle, then signs locally with a real key and
       verifies the signature, proving the produced plc_operation is well-formed
       and signed. (The live function delegates signing to the PDS via
       signPlcOperation; this offline check validates the build and the
       signature primitive it feeds.) ---- */
    {
        const char *rotation_keys[] = {"did:key:zRotationKey"};
        const char *aka[1];
        char aka_buf[256];
        snprintf(aka_buf, sizeof(aka_buf), "at://%s", "new.example.com");
        aka[0] = aka_buf;
        wf_plc_operation_update rot = {
            .rotation_keys = rotation_keys,
            .rotation_keys_count = 1,
            .verification_methods_json = "{\"atproto\":\"did:key:zVerifyKey\"}",
            .services_json = "{\"atproto_pds\":\"https://pds.example.com\"}",
            .also_known_as = aka,
            .also_known_as_count = 1,
        };
        char *op_json = NULL;
        wf_status bstatus = wf_plc_operation_build(&rot, &op_json);
        WF_CHECK(bstatus == WF_OK);
        WF_CHECK(op_json != NULL);
        if (op_json) {
            cJSON *op = cJSON_Parse(op_json);
            int ok_type = 0, ok_rkeys = 0, ok_aka = 0, ok_unsigned = 0;
            WF_CHECK(op != NULL);
            if (op) {
                const cJSON *type =
                    cJSON_GetObjectItemCaseSensitive(op, "type");
                ok_type = type && cJSON_IsString(type) &&
                          strcmp(type->valuestring, "plc_operation") == 0;
                const cJSON *rkeys =
                    cJSON_GetObjectItemCaseSensitive(op, "rotationKeys");
                ok_rkeys = rkeys && cJSON_IsArray(rkeys) &&
                           cJSON_GetArraySize(rkeys) == 1;
                const cJSON *aka_arr =
                    cJSON_GetObjectItemCaseSensitive(op, "alsoKnownAs");
                ok_aka = aka_arr && cJSON_IsArray(aka_arr) &&
                         aka_arr->child && cJSON_IsString(aka_arr->child) &&
                         strcmp(aka_arr->child->valuestring,
                                "at://new.example.com") == 0;
                ok_unsigned =
                    cJSON_GetObjectItemCaseSensitive(op, "sig") == NULL;
                cJSON_Delete(op);
            }
            WF_CHECK(ok_type);
            WF_CHECK(ok_rkeys);
            WF_CHECK(ok_aka);
            WF_CHECK(ok_unsigned);
            /* Sign locally + verify to prove the op is signable. */
            wf_signing_key key;
            wf_status gstatus = wf_signing_key_generate(WF_KEY_TYPE_P256, &key);
            WF_CHECK(gstatus == WF_OK);
            if (gstatus == WF_OK) {
                char *signed_json = NULL;
                wf_status sstatus =
                    wf_plc_operation_sign(op_json, &key, &signed_json);
                WF_CHECK(sstatus == WF_OK);
                WF_CHECK(signed_json != NULL);
                if (signed_json) {
                    char *signer = NULL;
                    wf_status vstatus =
                        wf_plc_operation_verify(signed_json, &signer);
                    WF_CHECK(vstatus == WF_OK);
                    free(signer);
                }
                wf_plc_operation_free(signed_json);
            }
        }
        wf_plc_operation_free(op_json);
    }

    printf("identity_typed: all checks passed\n");
    return 0;
}
