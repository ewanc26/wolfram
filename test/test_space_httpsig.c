/*
 * test_space_httpsig.c -- spaces/httpsig.h against
 * test/vectors/spaces/httpsig.json, plus the rejection rules.
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/spaces/httpsig.h"
#include "test.h"
#include "spaces_vectors.h"

#define CRED_INPUT                                                             \
    "sig=(\"authorization\" \"atproto-space-audience\");"                      \
    "tag=\"atproto-space-credential\""

static char *authorization_for(const char *credential) {
    size_t len = strlen(credential) + sizeof("Atproto-Space-Credential ");
    char *value = malloc(len);
    if (value) {
        snprintf(value, len, "Atproto-Space-Credential %s", credential);
    }
    return value;
}

static void test_credential_vectors(const cJSON *root) {
    const cJSON *vec;
    int count = 0;

    cJSON_ArrayForEach(
        vec, cJSON_GetObjectItemCaseSensitive(root, "credential_signatures")) {
        char *authorization = authorization_for(vec_str(vec, "credential"));
        char signature[128];
        wf_status status;

        snprintf(signature, sizeof(signature),
                 "sig=:%s:", vec_str(vec, "signature"));
        status = wf_space_credential_verify(
            authorization, vec_str(vec, "audience"), CRED_INPUT, signature,
            vec_str(vec, "cnf_kid"));
        WF_CHECK(status == WF_OK);
        if (status != WF_OK) {
            fprintf(stderr, "  vector: %s\n", vec_str(vec, "name"));
        }
        free(authorization);
        count++;
    }
    WF_CHECK(count == 5);
}

static void test_credential_rules(const cJSON *root) {
    const cJSON *vec = cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(root, "credential_signatures"), 0);
    char *authorization = authorization_for(vec_str(vec, "credential"));
    const char *aud = vec_str(vec, "audience");
    const char *kid = vec_str(vec, "cnf_kid");
    const char *other_kid =
        "did:key:zDnaerESMGLw8NRXkwfGkQvpQ9u2R26RMXChzjkUbp9M1K7iw";
    char signature[128];
    char relabelled[128];
    char two[512];
    char two_sigs[256];
    wf_signing_key key;
    wf_space_signature made;

    snprintf(signature, sizeof(signature),
             "sig=:%s:", vec_str(vec, "signature"));
    snprintf(relabelled, sizeof(relabelled),
             "proxy=:%s:", vec_str(vec, "signature"));

#define VERIFY(auth, audience, input, sig, k)                                  \
    wf_space_credential_verify(auth, audience, input, sig, k)

    /* Found by tag under any label. */
    WF_CHECK(VERIFY(authorization, aud,
                    "proxy=(\"authorization\" \"atproto-space-audience\");"
                    "tag=\"atproto-space-credential\"",
                    relabelled, kid) == WF_OK);
    /* Another audience, key, or missing header. */
    WF_CHECK(VERIFY(authorization, "did:web:other.example", CRED_INPUT,
                    signature, kid) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud, CRED_INPUT, signature, other_kid) ==
             WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud, NULL, signature, kid) ==
             WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud, CRED_INPUT, NULL, kid) ==
             WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud, CRED_INPUT, relabelled, kid) ==
             WF_ERR_VALIDATION);
    /* Wrong components, order, tag, alg, keyid, or duplicate tag. */
    WF_CHECK(VERIFY(authorization, aud,
                    "sig=(\"atproto-space-audience\" \"authorization\");"
                    "tag=\"atproto-space-credential\"",
                    signature, kid) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud,
                    "sig=(\"authorization\");"
                    "tag=\"atproto-space-credential\"",
                    signature, kid) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud,
                    "sig=(\"authorization\" \"atproto-space-audience\");"
                    "tag=\"other\"",
                    signature, kid) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud, CRED_INPUT ";alg=\"hmac-sha256\"",
                    signature, kid) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(authorization, aud, CRED_INPUT ";keyid=\"did:key:zx\"",
                    signature, kid) == WF_ERR_VALIDATION);
    snprintf(two, sizeof(two),
             CRED_INPUT ", b=(\"authorization\" \"atproto-space-audience\");"
                        "tag=\"atproto-space-credential\"");
    snprintf(two_sigs, sizeof(two_sigs), "%s, b=:AAAA:", signature);
    WF_CHECK(VERIFY(authorization, aud, two, two_sigs, kid) ==
             WF_ERR_VALIDATION);
    /* A line break must not be able to forge lines of the base. */
    WF_CHECK(VERIFY("a\nb", aud, CRED_INPUT, signature, kid) ==
             WF_ERR_VALIDATION);

    /* What we sign, we accept. */
    WF_CHECK(wf_signing_key_from_hex(WF_KEY_TYPE_P256,
                                     vec_str(root, "credential_key_hex"),
                                     &key) == WF_OK);
    WF_CHECK(wf_space_credential_sign(&key, authorization, aud, &made) ==
             WF_OK);
    WF_CHECK(strcmp(made.signature_input, CRED_INPUT) == 0);
    WF_CHECK(VERIFY(authorization, aud, made.signature_input, made.signature,
                    kid) == WF_OK);
    WF_CHECK(wf_space_credential_sign(&key, authorization, "a\r\nb", &made) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(made.signature == NULL);
#undef VERIFY
    free(authorization);
}

static void test_commit_mac(const cJSON *mac, const unsigned char secret[32]) {
    const cJSON *vec = cJSON_GetObjectItemCaseSensitive(mac, "commit");
    const char *key_share = vec_str(mac, "key_share");
    const char *header = vec_str(vec, "header");
    const char *signature = vec_str(vec, "signature");
    char *json =
        cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(vec, "json"));
    char *accept = NULL;
    char input[512];
    char loose[1024];
    wf_space_commit commit;
    wf_space_commit wrong;
    wf_space_mac_request request;
    wf_space_mac_headers made;
    unsigned char bad_secret[32];
    size_t i;
    size_t at = 0;

    WF_CHECK(wf_space_commit_parse_json(json, strlen(json), &commit) == WF_OK);
    WF_CHECK(wf_space_mac_request_format(WF_SPACE_MAC_COMMIT, key_share,
                                         &accept) == WF_OK);
    WF_CHECK(accept && strcmp(accept, vec_str(vec, "accept_signature")) == 0);

    /* Host: parse the request and produce the published headers. */
    WF_CHECK(wf_space_mac_request_parse(accept, WF_SPACE_MAC_COMMIT,
                                        &request) == WF_OK);
    WF_CHECK(strcmp(request.key_share, key_share) == 0);
    WF_CHECK(wf_space_commit_mac(&commit, &request, secret, &made) == WF_OK);
    WF_CHECK(made.covered && strcmp(made.covered, header) == 0);
    WF_CHECK(made.signature && strcmp(made.signature, signature) == 0);
    snprintf(input, sizeof(input), "mac=%s", request.params);
    WF_CHECK(made.signature_input && strcmp(made.signature_input, input) == 0);

#define VERIFY(c, repo, space, share, key, h)                                  \
    wf_space_commit_verify(c, repo, space, share, key, h, input, signature)

    WF_CHECK(VERIFY(&commit, commit.repo, commit.space, key_share, secret,
                    header) == WF_OK);

    /* `sf` covers the canonical form, so extra whitespace changes nothing. */
    for (i = 0; header[i] && at + 3 < sizeof(loose); i++) {
        loose[at++] = header[i];
        if (header[i] == ',') {
            loose[at++] = '\t';
            loose[at++] = ' ';
        }
    }
    loose[at] = '\0';
    WF_CHECK(VERIFY(&commit, commit.repo, commit.space, key_share, secret,
                    loose) == WF_OK);

    /* Another repo, space, key share, secret, commit or header. */
    WF_CHECK(VERIFY(&commit, "did:web:other.example", commit.space, key_share,
                    secret, header) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(&commit, commit.repo, "at:space/did:web:a.example/a.b.c/d",
                    key_share, secret, header) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(&commit, commit.repo, commit.space, "did:key:zother",
                    secret, header) == WF_ERR_VALIDATION);
    memcpy(bad_secret, secret, 32);
    bad_secret[31] ^= 1;
    WF_CHECK(VERIFY(&commit, commit.repo, commit.space, key_share, bad_secret,
                    header) == WF_ERR_VALIDATION);
    wrong = commit;
    wrong.data[0] ^= 1;
    WF_CHECK(VERIFY(&wrong, commit.repo, commit.space, key_share, secret,
                    header) == WF_ERR_VALIDATION);
    wrong = commit;
    wrong.rev = "3n2gtxdkr222p";
    WF_CHECK(VERIFY(&wrong, commit.repo, commit.space, key_share, secret,
                    header) == WF_ERR_VALIDATION);
    wrong = commit;
    wrong.ver = 2;
    WF_CHECK(VERIFY(&wrong, commit.repo, commit.space, key_share, secret,
                    header) == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(&commit, commit.repo, commit.space, key_share, secret,
                    "ver=1") == WF_ERR_VALIDATION);
    WF_CHECK(VERIFY(&commit, commit.repo, commit.space, key_share, secret,
                    NULL) == WF_ERR_VALIDATION);
#undef VERIFY
    /* A commit MAC is not a record MAC. */
    WF_CHECK(wf_space_record_verify("at:space/x", "y", key_share, secret,
                                    header, input,
                                    signature) == WF_ERR_VALIDATION);
    WF_CHECK(wf_space_record_mac("u", "c", &request, secret, &made) ==
             WF_ERR_INVALID_ARG);

    wf_space_mac_request_free(&request);
    wf_space_commit_free(&commit);
    free(accept);
    cJSON_free(json);
}

static void test_record_mac(const cJSON *mac, const unsigned char secret[32]) {
    const cJSON *vec = cJSON_GetObjectItemCaseSensitive(mac, "record");
    const char *key_share = vec_str(mac, "key_share");
    const char *uri = vec_str(vec, "uri");
    const char *cid = vec_str(vec, "cid");
    wf_space_mac_request request;
    wf_space_mac_headers made;

    WF_CHECK(wf_space_mac_request_parse(vec_str(vec, "accept_signature"),
                                        WF_SPACE_MAC_RECORD,
                                        &request) == WF_OK);
    WF_CHECK(wf_space_record_mac(uri, cid, &request, secret, &made) == WF_OK);
    WF_CHECK(strcmp(made.covered, vec_str(vec, "header")) == 0);
    WF_CHECK(strcmp(made.signature, vec_str(vec, "signature")) == 0);
    WF_CHECK(wf_space_record_verify(uri, cid, key_share, secret, made.covered,
                                    made.signature_input,
                                    made.signature) == WF_OK);
    WF_CHECK(wf_space_record_verify(uri, "bafyother", key_share, secret,
                                    made.covered, made.signature_input,
                                    made.signature) == WF_ERR_VALIDATION);
    wf_space_mac_headers_free(&made);
    wf_space_mac_request_free(&request);
}

static void test_request_rules(void) {
    static const char *bad[] = {
        "",
        "mac",
        "mac=(\"atproto-space-commit\";sf);tag=\"atproto-space-commit\"",
        "mac=(\"atproto-space-commit\");keyid=\"did:key:zx\";"
        "tag=\"atproto-space-commit\"",
        "mac=(\"atproto-space-record\";sf);keyid=\"did:key:zx\";"
        "tag=\"atproto-space-commit\"",
        "mac=(\"atproto-space-commit\";sf);keyid=\"did:key:zx\";"
        "tag=\"atproto-space-commit\";nonce=\"n\"",
        "mac=(\"atproto-space-commit\";sf);keyid=\"did:key:zx\";"
        "tag=\"atproto-space-commit\";alg=\"ecdsa-p256-sha256\"",
        "a=(\"atproto-space-commit\";sf);keyid=\"did:key:zx\";"
        "tag=\"atproto-space-commit\", b=(\"atproto-space-commit\";sf);"
        "keyid=\"did:key:zy\";tag=\"atproto-space-commit\"",
        "mac=(\"atproto-space-commit\";sf);keyid=\"did:key:zx\";"
        "tag=\"atproto-space-commit\",",
        "mac=(\"atproto-space-commit\";sf",
        "mac=(\"atproto-space-commit\";sf);keyid=nope;"
        "tag=\"atproto-space-commit\"",
    };
    wf_space_mac_request request;
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        wf_status status =
            wf_space_mac_request_parse(bad[i], WF_SPACE_MAC_COMMIT, &request);
        WF_CHECK(status == WF_ERR_VALIDATION);
        WF_CHECK(request.key_share == NULL);
        if (status != WF_ERR_VALIDATION) {
            fprintf(stderr, "  request: %s\n", bad[i]);
        }
    }
    /* Requests with other tags are ignored, whatever they ask for. */
    WF_CHECK(wf_space_mac_request_parse(
                 "x=(\"@method\");created=1;tag=\"other\", y=?1, "
                 "m=(\"atproto-space-commit\";sf);keyid=\"did:key:zx\";"
                 "alg=\"hmac-sha256\";tag=\"atproto-space-commit\"",
                 WF_SPACE_MAC_COMMIT, &request) == WF_OK);
    WF_CHECK(request.label && strcmp(request.label, "m") == 0);
    wf_space_mac_request_free(&request);
    WF_CHECK(wf_space_mac_request_parse(NULL, WF_SPACE_MAC_COMMIT, &request) ==
             WF_ERR_VALIDATION);
}

static void test_commit_json_rules(void) {
    static const char *bad[] = {
        "{}",
        "[]",
        "not json",
        "{\"ver\":2,\"repo\":\"did:web:a.example\",\"space\":"
        "\"at:space/did:web:a.example/a.b.c/d\",\"rev\":\"3n2gtxdkr232p\","
        "\"data\":{\"$bytes\":\"eVD7vKgDQrLKfOvN3VZJHYX0HGz158ocKa5oRd3kaYw\"}"
        "}",
        "{\"ver\":1,\"repo\":\"did:web:a.example\",\"space\":"
        "\"at:space/did:web:a.example/a.b.c/d\",\"rev\":\"3n2gtxdkr232p\","
        "\"data\":{\"$bytes\":\"AAAA\"}}",
        "{\"ver\":1,\"repo\":\"did:web:a.example\",\"space\":"
        "\"at://did:web:a.example/a.b.c/d\",\"rev\":\"3n2gtxdkr232p\","
        "\"data\":{\"$bytes\":\"eVD7vKgDQrLKfOvN3VZJHYX0HGz158ocKa5oRd3kaYw\"}"
        "}",
    };
    wf_space_commit commit;
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        WF_CHECK(wf_space_commit_parse_json(bad[i], strlen(bad[i]), &commit) ==
                 WF_ERR_PARSE);
        WF_CHECK(commit.repo == NULL);
    }
}

int main(void) {
    cJSON *root = vec_load("httpsig.json");
    const cJSON *mac;
    unsigned char secret[32];
    const char *hex;
    unsigned i;

    WF_CHECK(root != NULL);
    mac = cJSON_GetObjectItemCaseSensitive(root, "mac");
    hex = vec_str(mac, "secret");
    for (i = 0; hex && i < 32; i++) {
        unsigned byte = 0;
        WF_CHECK(sscanf(hex + 2 * i, "%2x", &byte) == 1);
        secret[i] = (unsigned char)byte;
    }
    test_credential_vectors(root);
    test_credential_rules(root);
    test_commit_mac(mac, secret);
    test_record_mac(mac, secret);
    test_request_rules();
    test_commit_json_rules();
    cJSON_Delete(root);
    WF_TEST_SUMMARY();
}
