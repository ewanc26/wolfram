/*
 * test_mirror_persist.c — persist the agent's offline repo mirror to SQLite.
 *
 * Builds a repo in memory, seeds the agent's mirror from a base CAR (which
 * auto-persists HEAD + blocks to an attached store), applies an incremental
 * diff (also auto-persisted), then closes the store + agent, reopens the
 * store, reloads the mirror into a fresh agent, and asserts the HEAD and
 * record bytes match offline. Gated behind WOLFRAM_BUILD_STORE.
 */

#include "wolfram/agent.h"
#include "wolfram/repo.h"
#include "wolfram/repo/car.h"
#include "wolfram/repo/cid.h"
#include "wolfram/crypto.h"
#include "wolfram/store.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static int check_cid(const wf_cid *a, const wf_cid *b) {
    return a && b && a->len == b->len && a->len == 36 &&
           memcmp(a->bytes, b->bytes, a->len) == 0;
}

static char *tmp_db_path(void) {
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    static char path[512];
    snprintf(path, sizeof(path), "%s/wolfram_mirror_persist_test.db", dir);
    return path;
}

int main(void) {
    const char *did = "did:plc:mirrorpersist";
    const char *did_key =
        "did:key:zDnaepsL7AXenJkVYdkh5KuKsSU7Ykh7kyXaLLU7auN9FWSiZ";
    unsigned char record_one[]   = {0xA1, 0x64, 't', 'e', 's', 't', 0x01};
    unsigned char record_two[]   = {0xA1, 0x64, 't', 'e', 's', 't', 0x02};
    unsigned char record_three[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x03};
    unsigned char record_four[]  = {0xA1, 0x64, 't', 'e', 's', 't', 0x04};

    wf_signing_key key = {0};
    key.type = WF_KEY_TYPE_P256;
    key.bytes[31] = 1;

    /* ── Build a base repo CAR + an incremental update CAR ── */
    wf_car working = {0};
    wf_cid commit_one = {0}, commit_base = {0}, commit_update = {0};
    wf_cid old_keep = {0}, old_delete = {0};

    WF_CHECK(wf_repo_create_record(&working, NULL, did, "com.example.posts",
                                   "keep", record_one, sizeof(record_one),
                                   &key, &commit_one, &old_keep) == WF_OK);
    WF_CHECK(wf_repo_create_record(&working, &commit_one, did,
                                   "com.example.posts", "remove",
                                   record_two, sizeof(record_two), &key,
                                   &commit_base, &old_delete) == WF_OK);

    wf_car base = {0};
    {
        unsigned char *base_bytes = NULL;
        size_t base_len = 0;
        working.roots = malloc(sizeof(wf_cid));
        WF_CHECK(working.roots != NULL);
        working.roots[0] = commit_base;
        working.root_count = 1;
        WF_CHECK(wf_car_write(&working, &base_bytes, &base_len) == WF_OK);
        WF_CHECK(wf_car_parse(base_bytes, base_len, &base) == WF_OK);
        free(base_bytes);
    }

    size_t first_update_block = working.block_count;
    wf_cid intermediate = {0}, after_delete = {0}, new_keep = {0}, new_add = {0};
    WF_CHECK(wf_repo_update_record(&working, &commit_base, did,
                                   "com.example.posts", "keep",
                                   record_three, sizeof(record_three), &key,
                                   &intermediate, &new_keep) == WF_OK);
    WF_CHECK(wf_repo_delete_record(&working, &intermediate, did,
                                   "com.example.posts", "remove",
                                   &key, &after_delete) == WF_OK);
    WF_CHECK(wf_repo_create_record(&working, &after_delete, did,
                                   "com.example.posts", "added",
                                   record_four, sizeof(record_four), &key,
                                   &commit_update, &new_add) == WF_OK);

    wf_car update = {0};
    update.roots = &commit_update;
    update.root_count = 1;
    update.blocks = calloc(working.block_count - first_update_block,
                           sizeof(*update.blocks));
    WF_CHECK(update.blocks != NULL);
    for (size_t i = first_update_block; i < working.block_count; i++) {
        int duplicate = 0;
        for (size_t j = 0; j < update.block_count; j++) {
            if (check_cid(&working.blocks[i].cid, &update.blocks[j].cid)) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) update.blocks[update.block_count++] = working.blocks[i];
    }
    unsigned char *update_bytes = NULL;
    size_t update_len = 0;
    WF_CHECK(wf_car_write(&update, &update_bytes, &update_len) == WF_OK);

    char *path = tmp_db_path();
    remove(path);

    /* ── Session 1: seed + apply, persisting to the store ── */
    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, path) == WF_OK);

    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);
    WF_CHECK(wf_agent_set_did(agent, did) == WF_OK);
    wf_agent_set_signing_key(agent, did_key);
    WF_CHECK(wf_agent_attach_store(agent, store) == WF_OK);

    WF_CHECK(wf_agent_seed_repo(agent, &base) == WF_OK);

    char *seed_head = NULL;
    WF_CHECK(wf_agent_repo_head(agent, &seed_head) == WF_OK);
    char *expect_base = wf_cid_to_string(&commit_base);
    WF_CHECK(strcmp(seed_head, expect_base) == 0);
    free(expect_base);

    WF_CHECK(wf_agent_apply_repo_diff(agent, update_bytes, update_len) == WF_OK);

    char *applied_head = NULL;
    WF_CHECK(wf_agent_repo_head(agent, &applied_head) == WF_OK);
    char *expect_update = wf_cid_to_string(&commit_update);
    WF_CHECK(strcmp(applied_head, expect_update) == 0);

    /* Live (in-memory) record checks before reload. */
    {
        unsigned char *data = NULL;
        size_t len = 0;
        WF_CHECK(wf_agent_mirror_get_record(agent, "com.example.posts", "keep",
                                            &data, &len) == WF_OK);
        WF_CHECK(len == sizeof(record_three) &&
                 memcmp(data, record_three, len) == 0);
        free(data);
    }

    wf_agent_free(agent);
    wf_store_close(store);
    free(seed_head);
    free(applied_head);

    /* ── Session 2: reopen the store and reload the mirror ── */
    wf_store *store2 = NULL;
    WF_CHECK(wf_store_open(&store2, path) == WF_OK);

    wf_agent *agent2 = wf_agent_new("https://example.com");
    WF_CHECK(agent2 != NULL);
    WF_CHECK(wf_agent_set_did(agent2, did) == WF_OK);
    wf_agent_set_signing_key(agent2, did_key);
    WF_CHECK(wf_agent_attach_store(agent2, store2) == WF_OK);

    WF_CHECK(wf_agent_mirror_load_from_store(agent2) == WF_OK);

    char *reload_head = NULL;
    WF_CHECK(wf_agent_repo_head(agent2, &reload_head) == WF_OK);
    WF_CHECK(strcmp(reload_head, expect_update) == 0);
    free(expect_update);

    /* Reloaded record bytes must match what was persisted. */
    {
        unsigned char *data = NULL;
        size_t len = 0;
        WF_CHECK(wf_agent_mirror_get_record(agent2, "com.example.posts", "keep",
                                            &data, &len) == WF_OK);
        WF_CHECK(len == sizeof(record_three) &&
                 memcmp(data, record_three, len) == 0);
        free(data);

        WF_CHECK(wf_agent_mirror_get_record(agent2, "com.example.posts", "added",
                                            &data, &len) == WF_OK);
        WF_CHECK(len == sizeof(record_four) &&
                 memcmp(data, record_four, len) == 0);
        free(data);

        /* Deleted record is absent after the diff was applied. */
        WF_CHECK(wf_agent_mirror_get_record(agent2, "com.example.posts", "remove",
                                            &data, &len) == WF_ERR_NOT_FOUND);
    }

    /* Best-effort guard: loading without an attached store is a clean error. */
    {
        wf_agent *agent3 = wf_agent_new("https://example.com");
        WF_CHECK(agent3 != NULL);
        WF_CHECK(wf_agent_set_did(agent3, did) == WF_OK);
        WF_CHECK(wf_agent_mirror_load_from_store(agent3) ==
                 WF_ERR_INVALID_ARG);
        wf_agent_free(agent3);
    }

    free(reload_head);
    wf_agent_free(agent2);
    wf_store_close(store2);
    remove(path);

    WF_TEST_SUMMARY();
}
