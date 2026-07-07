/*
 * test_agent_repo.c — agent-level verified incremental repo diff application.
 *
 * Builds a repo in memory (base + an incremental update), seeds the agent's
 * local mirror from the base, then routes the update CAR through the agent
 * API (wf_agent_apply_repo_diff) and checks the mirror advances correctly.
 * Also exercises operation inversion and error paths. No network required:
 * the account DID and verification key are injected directly.
 */

#include "wolfram/agent.h"
#include "wolfram/repo.h"
#include "wolfram/crypto.h"
#include "wolfram/session.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

static int check_cid(const wf_cid *left, const wf_cid *right) {
    return left && right && left->len == right->len && left->len == 36 &&
           memcmp(left->bytes, right->bytes, left->len) == 0;
}

static wf_cid make_cid(unsigned char marker) {
    wf_cid cid = {{0}, 0};
    cid.len = 36;
    cid.bytes[0] = 0x01;
    cid.bytes[1] = 0x71;
    cid.bytes[2] = 0x12;
    cid.bytes[3] = 0x20;
    cid.bytes[4] = marker;
    for (size_t i = 5; i < cid.len; i++) {
        cid.bytes[i] = (unsigned char)(marker + i);
    }
    return cid;
}

int main(void) {
    const char *did = "did:plc:repodifftest";
    const char *did_key =
        "did:key:zDnaepsL7AXenJkVYdkh5KuKsSU7Ykh7kyXaLLU7auN9FWSiZ";
    unsigned char record_one[]   = {0xA1, 0x64, 't', 'e', 's', 't', 0x01};
    unsigned char record_two[]   = {0xA1, 0x64, 't', 'e', 's', 't', 0x02};
    unsigned char record_three[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x03};
    unsigned char record_four[]  = {0xA1, 0x64, 't', 'e', 's', 't', 0x04};

    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);

    /* Inject identity offline (no network login needed for the diff path). */
    WF_CHECK(wf_agent_set_did(agent, did) == WF_OK);
    wf_agent_set_signing_key(agent, did_key);

    /* ── Build a base repo + an incremental update, as in test_diff.c ── */
    wf_signing_key key = {0};
    key.type = WF_KEY_TYPE_P256;
    key.bytes[31] = 1;

    wf_car working = {0};
    wf_cid commit_one = {0}, commit_base = {0}, commit_update = {0};
    wf_cid old_keep = {0}, old_delete = {0};
    wf_cid new_keep = {0}, new_add = {0};

    WF_CHECK(wf_repo_create_record(&working, NULL, did,
                                   "com.example.posts", "keep",
                                   record_one, sizeof(record_one), &key,
                                   &commit_one, &old_keep) == WF_OK);
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

    wf_cid intermediate = {0};
    WF_CHECK(wf_repo_update_record(&working, &commit_base, did,
                                   "com.example.posts", "keep",
                                   record_three, sizeof(record_three),
                                   &key, &intermediate, &new_keep) == WF_OK);
    wf_cid after_delete = {0};
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

    /* ── Apply through the agent API ── */

    /* Applying before seeding must be rejected. */
    WF_CHECK(wf_agent_apply_repo_diff(agent, update_bytes, update_len) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_seed_repo(agent, &base) == WF_OK);

    /* Head after seeding equals the base commit. */
    {
        char *head = NULL;
        char *expected = wf_cid_to_string(&commit_base);
        WF_CHECK(wf_agent_repo_head(agent, &head) == WF_OK);
        WF_CHECK(head != NULL && expected != NULL);
        WF_CHECK(strcmp(head, expected) == 0);
        free(head);
        free(expected);
    }

    {
        wf_repo_verify_options probe_opts = {0};
        probe_opts.expected_did = did;
        probe_opts.signing_key = did_key;
        wf_repo_diff probe_diff = {0};
        wf_status probe = wf_repo_diff_verify(&base, &commit_base,
                                               &update, &probe_opts, &probe_diff);
        fprintf(stderr, "direct verify status=%d\n", (int)probe);
        wf_repo_diff_free(&probe_diff);

        wf_car parsed_update = {0};
        wf_status p2 = wf_car_parse(update_bytes, update_len, &parsed_update);
        fprintf(stderr, "parse update bytes status=%d root_count=%zu\n",
                (int)p2, parsed_update.root_count);
        if (p2 == WF_OK) {
            wf_repo_diff probe_diff2 = {0};
            wf_status p3 = wf_repo_diff_verify(&base, &commit_base,
                                               &parsed_update, &probe_opts, &probe_diff2);
            fprintf(stderr, "verify parsed update status=%d\n", (int)p3);
            wf_repo_diff_free(&probe_diff2);
        }
        wf_car_free(&parsed_update);

        wf_status apply_status = wf_agent_apply_repo_diff(agent, update_bytes, update_len);
        if (apply_status != WF_OK) {
            fprintf(stderr, "apply_repo_diff status=%d\n", (int)apply_status);
        }
        WF_CHECK(apply_status == WF_OK);
    }

    /* Head after applying the diff equals the update commit. */
    {
        char *head = NULL;
        char *expected = wf_cid_to_string(&commit_update);
        WF_CHECK(wf_agent_repo_head(agent, &head) == WF_OK);
        WF_CHECK(head != NULL && expected != NULL);
        WF_CHECK(strcmp(head, expected) == 0);
        free(head);
        free(expected);
    }

    /* The mirror reflects the applied operations. */
    {
        unsigned char *found = NULL;
        size_t found_len = 0;
        WF_CHECK(wf_agent_mirror_get_record(agent, "com.example.posts", "keep",
                                            &found, &found_len) == WF_OK);
        WF_CHECK(found_len == sizeof(record_three) &&
                 memcmp(found, record_three, found_len) == 0);
        free(found);

        WF_CHECK(wf_agent_mirror_get_record(agent, "com.example.posts", "remove",
                                            &found, &found_len) ==
                 WF_ERR_NOT_FOUND);
        WF_CHECK(wf_agent_mirror_get_record(agent, "com.example.posts", "added",
                                            &found, &found_len) == WF_OK);
        WF_CHECK(found_len == sizeof(record_four) &&
                 memcmp(found, record_four, found_len) == 0);
        free(found);
    }

    /* Re-applying the same diff is a no-op (mirror already at target). */
    WF_CHECK(wf_agent_apply_repo_diff(agent, update_bytes, update_len) ==
             WF_OK);

    /* ── Operation inversion via the agent wrapper ── */
    {
        wf_repo_operation ops[2];
        memset(ops, 0, sizeof(ops));
        ops[0].action = WF_REPO_CREATE;
        ops[0].collection = "com.example.posts";
        ops[0].rkey = "a";
        ops[0].cid = make_cid(10);
        ops[1].action = WF_REPO_UPDATE;
        ops[1].collection = "com.example.posts";
        ops[1].rkey = "b";
        ops[1].cid = make_cid(11);
        ops[1].prev = make_cid(12);

        wf_repo_operation *inverse = NULL;
        WF_CHECK(wf_agent_invert_repo_operations(agent, ops, 2,
                                                  &inverse) == WF_OK);
        WF_CHECK(inverse != NULL);
        if (inverse) {
            /* wf_repo_operations_invert reverses order: inverse[0] is the
             * inverted last op (ops[1], an UPDATE), inverse[1] the first. */
            WF_CHECK(inverse[0].action == WF_REPO_UPDATE);
            WF_CHECK(check_cid(&inverse[0].cid, &ops[1].prev));
            WF_CHECK(check_cid(&inverse[0].prev, &ops[1].cid));
            WF_CHECK(inverse[1].action == WF_REPO_DELETE);
            wf_repo_operations_free(inverse, 2);
        }
    }

    free(update_bytes);
    free(update.blocks);
    wf_car_free(&base);
    wf_car_free(&working);
    wf_agent_free(agent);

    WF_TEST_SUMMARY();
}
