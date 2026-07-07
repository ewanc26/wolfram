/**
 * test_store.c — unit tests for the optional SQLite-backed store.
 *
 * Fully offline. Uses an in-memory database (":memory:") and rounds-trips
 * a wf_session and a repo-mirror head + block, asserting byte-for-byte
 * equality on load.
 *
 * Only built when WOLFRAM_BUILD_STORE=ON.
 */

#include "wolfram/store.h"
#include "test.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void fill_session_data(wf_session *s) {
    s->data.access_jwt = strdup("access.jwt.token");
    s->data.refresh_jwt = strdup("refresh.jwt.token");
    s->data.handle = strdup("alice.example.com");
    s->data.did = strdup("did:plc:abc123def456");
    s->data.email = strdup("alice@example.com");
    s->data.email_confirmed = 1;
    s->data.email_auth_factor = 0;
    s->data.active = 1;
    s->data.status = strdup("active");
    s->has_session = 1;
}

static void test_session_roundtrip(void) {
    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, ":memory:") == WF_OK);
    WF_CHECK(store != NULL);

    /* An empty store has no session. */
    wf_session *missing = NULL;
    WF_CHECK(wf_store_load_session(store, &missing) == WF_ERR_NOT_FOUND);

    wf_session *saved = wf_session_new("https://bsky.social");
    WF_CHECK(saved != NULL);
    fill_session_data(saved);

    WF_CHECK(wf_store_save_session(store, saved) == WF_OK);

    wf_session *none = NULL;
    WF_CHECK(wf_store_load_session(store, &none) == WF_OK);
    WF_CHECK(none != NULL);

    WF_CHECK(strcmp(none->data.access_jwt, saved->data.access_jwt) == 0);
    WF_CHECK(strcmp(none->data.refresh_jwt, saved->data.refresh_jwt) == 0);
    WF_CHECK(strcmp(none->data.handle, saved->data.handle) == 0);
    WF_CHECK(strcmp(none->data.did, saved->data.did) == 0);
    WF_CHECK(strcmp(none->data.email, saved->data.email) == 0);
    WF_CHECK(strcmp(none->data.status, saved->data.status) == 0);
    WF_CHECK(none->data.email_confirmed == saved->data.email_confirmed);
    WF_CHECK(none->data.email_auth_factor == saved->data.email_auth_factor);
    WF_CHECK(none->data.active == saved->data.active);
    WF_CHECK(none->has_session == 1);

    wf_session_free(none);
    wf_session_free(saved);

    wf_store_close(store);
}

static void test_mirror_roundtrip(void) {
    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, ":memory:") == WF_OK);

    const char *did = "did:plc:repoowner";
    const char *head_cid = "bafyreigh2abc123def456";

    WF_CHECK(wf_store_save_mirror_head(store, did, head_cid) == WF_OK);
    char *loaded_cid = NULL;
    WF_CHECK(wf_store_load_mirror_head(store, did, &loaded_cid) == WF_OK);
    WF_CHECK(loaded_cid != NULL);
    WF_CHECK(strcmp(loaded_cid, head_cid) == 0);
    free(loaded_cid);

    /* Unknown DID yields not-found. */
    char *missing = NULL;
    WF_CHECK(wf_store_load_mirror_head(store, "did:plc:nope", &missing) ==
             WF_ERR_NOT_FOUND);

    /* Block round-trip with a binary CID + binary block. */
    const uint8_t cid[] = {0x01, 0x55, 0x12, 0x20, 0xde, 0xad, 0xbe, 0xef};
    const uint8_t block[] = {0x61, 0x62, 0x63, 0x00, 0xff, 0x10};
    WF_CHECK(wf_store_save_mirror_block(store, did, cid, sizeof(cid),
                                        block, sizeof(block)) == WF_OK);

    uint8_t *out_block = NULL;
    size_t out_len = 0;
    WF_CHECK(wf_store_load_mirror_block(store, did, cid, sizeof(cid),
                                        &out_block, &out_len) == WF_OK);
    WF_CHECK(out_block != NULL);
    WF_CHECK(out_len == sizeof(block));
    WF_CHECK(memcmp(out_block, block, sizeof(block)) == 0);
    free(out_block);

    /* Wrong CID yields not-found. */
    const uint8_t bad_cid[] = {0x99};
    uint8_t *noblock = NULL;
    size_t noblock_len = 0;
    WF_CHECK(wf_store_load_mirror_block(store, did, bad_cid, sizeof(bad_cid),
                                        &noblock, &noblock_len) ==
             WF_ERR_NOT_FOUND);

    wf_store_close(store);
}

static void test_open_invalid(void) {
    wf_store *s = NULL;
    WF_CHECK(wf_store_open(NULL, ":memory:") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_store_open(&s, NULL) == WF_ERR_INVALID_ARG);
    wf_store_close(NULL);
}

int main(void) {
    test_open_invalid();
    test_session_roundtrip();
    test_mirror_roundtrip();

    WF_TEST_SUMMARY();
}
