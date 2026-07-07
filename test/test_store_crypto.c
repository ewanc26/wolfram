/**
 * test_store_crypto.c — at-rest encryption tests for the optional
 * SQLite-backed store, built only when both WOLFRAM_BUILD_STORE and
 * WOLFRAM_BUILD_STORE_CRYPTO are ON.
 *
 * Fully offline, using an in-memory database (":memory:"), and exercises
 * the libsodium-backed path:
 *   - a session saved with a passphrase decrypts back to the original after
 *     a close/reopen when the same passphrase is supplied;
 *   - a session cannot be decrypted with the wrong passphrase (the
 *     authenticated cipher rejects it rather than yielding garbage);
 *   - saving/loading without a passphrase is rejected;
 *   - the (unencrypted) repo-mirror path still round-trips.
 */

#include "wolfram/store.h"
#include "test.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_pass = "correct horse battery staple";
static const char *k_wrong_pass = "turtle power";

/* A real on-disk file so the encrypted blob survives a close/reopen. An
 * in-memory (":memory:") database is private to each connection and would
 * be wiped on wf_store_close, defeating the round-trip test. */
static const char *k_db_path = "/tmp/wolfram_store_crypto_test.db";

static void remove_db(void) {
    remove(k_db_path);
}

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

static wf_session *make_session(void) {
    wf_session *s = wf_session_new("https://bsky.social");
    WF_CHECK(s != NULL);
    fill_session_data(s);
    return s;
}

static void assert_session_eq(const wf_session *a, const wf_session *b) {
    WF_CHECK(a != NULL && b != NULL);
    if (!a || !b) return;
    WF_CHECK(strcmp(a->data.access_jwt, b->data.access_jwt) == 0);
    WF_CHECK(strcmp(a->data.refresh_jwt, b->data.refresh_jwt) == 0);
    WF_CHECK(strcmp(a->data.handle, b->data.handle) == 0);
    WF_CHECK(strcmp(a->data.did, b->data.did) == 0);
    WF_CHECK(strcmp(a->data.email, b->data.email) == 0);
    WF_CHECK(strcmp(a->data.status, b->data.status) == 0);
    WF_CHECK(a->data.email_confirmed == b->data.email_confirmed);
    WF_CHECK(a->data.email_auth_factor == b->data.email_auth_factor);
    WF_CHECK(a->data.active == b->data.active);
    WF_CHECK(a->has_session == 1);
}

static void test_encrypted_roundtrip(void) {
    remove_db();

    /* Open, set passphrase, save. */
    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, k_db_path) == WF_OK);
    WF_CHECK(store != NULL);
    WF_CHECK(wf_store_set_passphrase(store, k_pass) == WF_OK);

    /* Saving without a keyed store would fail; we did set the passphrase. */
    wf_session *saved = make_session();
    WF_CHECK(wf_store_save_session(store, saved) == WF_OK);
    wf_session_free(saved);

    /* Close and reopen; the encrypted blob must survive and decrypt. */
    wf_store_close(store);
    store = NULL;
    WF_CHECK(wf_store_open(&store, k_db_path) == WF_OK);
    WF_CHECK(wf_store_set_passphrase(store, k_pass) == WF_OK);

    wf_session *loaded = NULL;
    WF_CHECK(wf_store_load_session(store, &loaded) == WF_OK);
    saved = make_session();
    assert_session_eq(loaded, saved);
    wf_session_free(saved);
    wf_session_free(loaded);

    wf_store_close(store);
    remove_db();
}

static void test_wrong_passphrase_fails(void) {
    remove_db();

    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, k_db_path) == WF_OK);
    WF_CHECK(wf_store_set_passphrase(store, k_pass) == WF_OK);

    wf_session *saved = make_session();
    WF_CHECK(wf_store_save_session(store, saved) == WF_OK);
    wf_session_free(saved);
    wf_store_close(store);

    /* Reopen with the WRONG passphrase: decryption must be rejected. */
    store = NULL;
    WF_CHECK(wf_store_open(&store, k_db_path) == WF_OK);
    WF_CHECK(wf_store_set_passphrase(store, k_wrong_pass) == WF_OK);

    wf_session *loaded = NULL;
    WF_CHECK(wf_store_load_session(store, &loaded) == WF_ERR_INVALID_ARG);
    WF_CHECK(loaded == NULL);
    wf_store_close(store);
    remove_db();
}

static void test_missing_passphrase_rejected(void) {
    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, ":memory:") == WF_OK);

    /* No passphrase set: saving and loading must both be rejected. */
    wf_session *saved = make_session();
    WF_CHECK(wf_store_save_session(store, saved) == WF_ERR_INVALID_ARG);
    wf_session_free(saved);

    wf_session *loaded = NULL;
    WF_CHECK(wf_store_load_session(store, &loaded) == WF_ERR_INVALID_ARG);
    WF_CHECK(loaded == NULL);

    /* A NULL passphrase is rejected outright. */
    WF_CHECK(wf_store_set_passphrase(store, NULL) == WF_ERR_INVALID_ARG);

    wf_store_close(store);
}

static void test_mirror_roundtrip(void) {
    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, ":memory:") == WF_OK);
    WF_CHECK(wf_store_set_passphrase(store, k_pass) == WF_OK);

    const char *did = "did:plc:repoowner";
    const char *head_cid = "bafyreigh2abc123def456";
    WF_CHECK(wf_store_save_mirror_head(store, did, head_cid) == WF_OK);

    char *loaded_cid = NULL;
    WF_CHECK(wf_store_load_mirror_head(store, did, &loaded_cid) == WF_OK);
    WF_CHECK(loaded_cid != NULL);
    WF_CHECK(strcmp(loaded_cid, head_cid) == 0);
    free(loaded_cid);

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

    wf_store_close(store);
}

int main(void) {
    test_missing_passphrase_rejected();
    test_encrypted_roundtrip();
    test_wrong_passphrase_fails();
    test_mirror_roundtrip();

    WF_TEST_SUMMARY();
}
