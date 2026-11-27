/*
 * test_space_kem.c -- spaces/kem.h against test/vectors/spaces/kem.json.
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/spaces/kem.h"
#include "test.h"
#include "spaces_vectors.h"

static void run_case(const cJSON *vec) {
    wf_signing_key key;
    unsigned char secret[WF_SPACE_SECRET_BYTES];
    const char *public_key = vec_str(vec, "public_key");

    WF_CHECK(wf_signing_key_from_hex(WF_KEY_TYPE_P256,
                                     vec_str(vec, "private_key_hex"),
                                     &key) == WF_OK);
    if (public_key) {
        char *derived = NULL;
        WF_CHECK(wf_signing_key_public_didkey(&key, &derived) == WF_OK);
        WF_CHECK(derived && strcmp(derived, public_key) == 0);
        free(derived);
    }
    WF_CHECK(wf_space_kem_decapsulate(&key, vec_str(vec, "key_share"),
                                      secret) == WF_OK);
    WF_CHECK(vec_hex_eq(secret, sizeof(secret), vec_str(vec, "secret")));
}

static void test_round_trip(void) {
    wf_signing_key author;
    wf_signing_key k256 = {WF_KEY_TYPE_SECP256K1, {1}};
    char *author_key = NULL;
    char *share_a = NULL;
    char *share_b = NULL;
    unsigned char sent_a[32];
    unsigned char sent_b[32];
    unsigned char received[32];
    unsigned char zero[32] = {0};

    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &author) == WF_OK);
    WF_CHECK(wf_signing_key_public_didkey(&author, &author_key) == WF_OK);
    WF_CHECK(wf_space_kem_encapsulate(author_key, &share_a, sent_a) == WF_OK);
    WF_CHECK(wf_space_kem_encapsulate(author_key, &share_b, sent_b) == WF_OK);
    /* Every encapsulation is new. */
    WF_CHECK(share_a && share_b && strcmp(share_a, share_b) != 0);
    WF_CHECK(memcmp(sent_a, sent_b, 32) != 0);
    WF_CHECK(wf_space_kem_decapsulate(&author, share_a, received) == WF_OK);
    WF_CHECK(memcmp(sent_a, received, 32) == 0);

    /* Wrong key types and undecodable shares fail and leave no secret. */
    WF_CHECK(wf_space_kem_decapsulate(&k256, share_a, received) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(memcmp(received, zero, 32) == 0);
    WF_CHECK(wf_space_kem_decapsulate(&author, "did:key:nope", received) !=
             WF_OK);
    WF_CHECK(memcmp(received, zero, 32) == 0);
    free(share_a);
    share_a = (char *)1;
    WF_CHECK(wf_space_kem_encapsulate(
                 "did:key:zQ3shtrHbFC5PwLhynynQQQftQcnZeUJ79kx2PvLacgJ5NpLq",
                 &share_a, sent_a) == WF_ERR_INVALID_ARG);
    WF_CHECK(share_a == NULL && memcmp(sent_a, zero, 32) == 0);

    free(share_b);
    free(author_key);
}

int main(void) {
    cJSON *root = vec_load("kem.json");
    const cJSON *item;
    int cases = 0;

    WF_CHECK(root != NULL);
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "cases")) {
        run_case(item);
        cases++;
    }
    WF_CHECK(cases == 2);
    test_round_trip();
    cJSON_Delete(root);
    WF_TEST_SUMMARY();
}
