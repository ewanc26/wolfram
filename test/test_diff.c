/*
 * test_diff.c — unit tests for repository diff application and inversion.
 */

#include "wolfram/repo.h"
#include "wolfram/crypto.h"
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

static char *dup_cstr(const char *value) {
    size_t len = strlen(value) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, value, len);
    return copy;
}

static void init_operation(wf_repo_operation *op,
                           wf_repo_operation_action action,
                           const char *collection,
                           const char *rkey,
                           wf_cid cid,
                           wf_cid prev) {
    memset(op, 0, sizeof(*op));
    op->action = action;
    op->collection = (char *)collection;
    op->rkey = (char *)rkey;
    op->cid = cid;
    op->prev = prev;
}

int main(void) {
    /* ── wf_repo_operations_invert ── */

    /* NULL operations with count 0 returns OK and NULL output. */
    {
        wf_repo_operation *out = (wf_repo_operation *)0x1;
        WF_CHECK(wf_repo_operations_invert(NULL, 0, &out) == WF_OK);
        WF_CHECK(out == NULL);
    }

    /* NULL operations with count > 0 is invalid. */
    {
        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(NULL, 1, &out) == WF_ERR_INVALID_ARG);
    }

    /* Inverting a single CREATE operation produces a DELETE. */
    {
        wf_repo_operation op = {0};
        init_operation(&op, WF_REPO_CREATE,
                       "com.example.posts", "r1",
                       make_cid(1), (wf_cid){{0}, 0});

        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(&op, 1, &out) == WF_OK);
        WF_CHECK(out != NULL);
        WF_CHECK(out[0].action == WF_REPO_DELETE);
        WF_CHECK(strcmp(out[0].collection, "com.example.posts") == 0);
        WF_CHECK(strcmp(out[0].rkey, "r1") == 0);
        WF_CHECK(check_cid(&out[0].cid, &op.cid));
        WF_CHECK(out[0].prev.len == 0);
        wf_repo_operations_free(out, 1);
    }

    /* Inverting a single DELETE operation produces a CREATE. */
    {
        wf_repo_operation op = {0};
        init_operation(&op, WF_REPO_DELETE,
                       "com.example.posts", "r2",
                       make_cid(2), (wf_cid){{0}, 0});

        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(&op, 1, &out) == WF_OK);
        WF_CHECK(out != NULL);
        WF_CHECK(out[0].action == WF_REPO_CREATE);
        WF_CHECK(strcmp(out[0].collection, "com.example.posts") == 0);
        WF_CHECK(strcmp(out[0].rkey, "r2") == 0);
        WF_CHECK(check_cid(&out[0].cid, &op.cid));
        WF_CHECK(out[0].prev.len == 0);
        wf_repo_operations_free(out, 1);
    }

    /* Inverting a single UPDATE operation swaps cid and prev. */
    {
        wf_repo_operation op = {0};
        init_operation(&op, WF_REPO_UPDATE,
                       "com.example.posts", "r3",
                       make_cid(3), make_cid(4));

        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(&op, 1, &out) == WF_OK);
        WF_CHECK(out != NULL);
        WF_CHECK(out[0].action == WF_REPO_UPDATE);
        WF_CHECK(strcmp(out[0].collection, "com.example.posts") == 0);
        WF_CHECK(strcmp(out[0].rkey, "r3") == 0);
        WF_CHECK(check_cid(&out[0].cid, &op.prev));
        WF_CHECK(check_cid(&out[0].prev, &op.cid));
        wf_repo_operations_free(out, 1);
    }

    /* Inverting multiple operations reverses their order. */
    {
        wf_repo_operation ops[3];
        init_operation(&ops[0], WF_REPO_CREATE,
                       "com.example.posts", "a",
                       make_cid(10), (wf_cid){{0}, 0});
        init_operation(&ops[1], WF_REPO_UPDATE,
                       "com.example.posts", "b",
                       make_cid(11), make_cid(12));
        init_operation(&ops[2], WF_REPO_DELETE,
                       "com.example.posts", "c",
                       make_cid(13), (wf_cid){{0}, 0});

        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(ops, 3, &out) == WF_OK);
        WF_CHECK(out != NULL);

        WF_CHECK(out[0].action == WF_REPO_CREATE);
        WF_CHECK(strcmp(out[0].rkey, "c") == 0);
        WF_CHECK(check_cid(&out[0].cid, &ops[2].cid));

        WF_CHECK(out[1].action == WF_REPO_UPDATE);
        WF_CHECK(strcmp(out[1].rkey, "b") == 0);
        WF_CHECK(check_cid(&out[1].cid, &ops[1].prev));
        WF_CHECK(check_cid(&out[1].prev, &ops[1].cid));

        WF_CHECK(out[2].action == WF_REPO_DELETE);
        WF_CHECK(strcmp(out[2].rkey, "a") == 0);
        WF_CHECK(check_cid(&out[2].cid, &ops[0].cid));

        wf_repo_operations_free(out, 3);
    }

    /* NULL collection or rkey is rejected. */
    {
        wf_repo_operation ops[1];
        ops[0].action = WF_REPO_CREATE;
        ops[0].collection = NULL;
        ops[0].rkey = "r1";
        ops[0].cid = make_cid(20);
        ops[0].prev = (wf_cid){{0}, 0};
        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(ops, 1, &out) == WF_ERR_INVALID_ARG);
    }
    {
        wf_repo_operation ops[1];
        ops[0].action = WF_REPO_CREATE;
        ops[0].collection = "com.example.posts";
        ops[0].rkey = NULL;
        ops[0].cid = make_cid(21);
        ops[0].prev = (wf_cid){{0}, 0};
        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(ops, 1, &out) == WF_ERR_INVALID_ARG);
    }

    /* Invalid action values are rejected. */
    {
        wf_repo_operation ops[1];
        ops[0].action = (wf_repo_operation_action)99;
        ops[0].collection = "com.example.posts";
        ops[0].rkey = "r1";
        ops[0].cid = make_cid(22);
        ops[0].prev = (wf_cid){{0}, 0};
        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(ops, 1, &out) == WF_ERR_INVALID_ARG);
    }

    /* Inverted operations can be freed with wf_repo_operations_free. */
    {
        wf_repo_operation ops[2];
        init_operation(&ops[0], WF_REPO_CREATE,
                       "com.example.posts", "free1",
                       make_cid(30), (wf_cid){{0}, 0});
        init_operation(&ops[1], WF_REPO_UPDATE,
                       "com.example.posts", "free2",
                       make_cid(31), make_cid(32));

        wf_repo_operation *out = NULL;
        WF_CHECK(wf_repo_operations_invert(ops, 2, &out) == WF_OK);
        WF_CHECK(out != NULL);
        wf_repo_operations_free(out, 2);
    }

    /* ── wf_repo_diff_apply validation ── */

    WF_CHECK(wf_repo_diff_apply(NULL, NULL) == WF_ERR_INVALID_ARG);

    {
        wf_car repo = {0};
        WF_CHECK(wf_repo_diff_apply(&repo, NULL) == WF_ERR_INVALID_ARG);
    }

    /* Root CID mismatch is rejected before any mutation happens. */
    {
        wf_car repo = {0};
        repo.roots = malloc(sizeof(wf_cid));
        WF_CHECK(repo.roots != NULL);
        repo.root_count = 1;
        repo.roots[0] = make_cid(40);

        wf_repo_diff diff = {0};
        diff.previous_commit = make_cid(41);
        diff.commit.cid = make_cid(42);
        diff.new_blocks.roots = malloc(sizeof(wf_cid));
        WF_CHECK(diff.new_blocks.roots != NULL);
        diff.new_blocks.root_count = 1;
        diff.new_blocks.roots[0] = diff.commit.cid;

        WF_CHECK(wf_repo_diff_apply(&repo, &diff) == WF_ERR_INVALID_ARG);

        free(diff.new_blocks.roots);
        free(repo.roots);
    }

    /* Zero operations and zero new blocks are rejected on an empty diff. */
    {
        wf_car repo = {0};
        repo.roots = malloc(sizeof(wf_cid));
        WF_CHECK(repo.roots != NULL);
        repo.root_count = 1;
        repo.roots[0] = make_cid(50);

        wf_repo_diff diff = {0};
        diff.previous_commit = repo.roots[0];
        diff.commit.cid = make_cid(51);
        diff.new_blocks.roots = malloc(sizeof(wf_cid));
        WF_CHECK(diff.new_blocks.roots != NULL);
        diff.new_blocks.root_count = 0;

        WF_CHECK(wf_repo_diff_apply(&repo, &diff) == WF_ERR_INVALID_ARG);

        free(diff.new_blocks.roots);
        free(repo.roots);
    }

    /* ── wf_repo_diff_free ── */

    /* Zeroed struct is safe to free. */
    {
        wf_repo_diff diff = {0};
        wf_repo_diff_free(&diff);
        WF_CHECK(diff.operations == NULL);
        WF_CHECK(diff.new_blocks.blocks == NULL);
        WF_CHECK(diff.removed_cids == NULL);
        WF_CHECK(diff.operation_count == 0);
        WF_CHECK(diff.new_blocks.block_count == 0);
        WF_CHECK(diff.removed_count == 0);
    }

    /* Free operations, new_blocks, and removed_cids. */
    {
        wf_repo_diff diff = {0};
        diff.operation_count = 1;
        diff.operations = calloc(1, sizeof(*diff.operations));
        WF_CHECK(diff.operations != NULL);
        diff.operations[0].action = WF_REPO_CREATE;
        diff.operations[0].collection = dup_cstr("com.example.posts");
        diff.operations[0].rkey = dup_cstr("free-me");
        diff.operations[0].cid = make_cid(60);

        diff.commit.cid = make_cid(61);
        diff.previous_commit = make_cid(62);

        diff.new_blocks.root_count = 1;
        diff.new_blocks.roots = malloc(sizeof(wf_cid));
        WF_CHECK(diff.new_blocks.roots != NULL);
        diff.new_blocks.roots[0] = diff.commit.cid;
        diff.new_blocks.block_count = 1;
        diff.new_blocks.blocks = calloc(1, sizeof(*diff.new_blocks.blocks));
        WF_CHECK(diff.new_blocks.blocks != NULL);
        diff.new_blocks.blocks[0].cid = make_cid(63);
        diff.new_blocks.blocks[0].data_len = 1;
        diff.new_blocks.blocks[0].data = malloc(1);
        WF_CHECK(diff.new_blocks.blocks[0].data != NULL);
        diff.new_blocks.blocks[0].data[0] = 0x00;

        diff.removed_count = 1;
        diff.removed_cids = malloc(sizeof(wf_cid));
        WF_CHECK(diff.removed_cids != NULL);
        diff.removed_cids[0] = make_cid(64);

        wf_repo_diff_free(&diff);
        WF_CHECK(diff.operations == NULL);
        WF_CHECK(diff.new_blocks.roots == NULL);
        WF_CHECK(diff.new_blocks.blocks == NULL);
        WF_CHECK(diff.removed_cids == NULL);
        WF_CHECK(diff.operation_count == 0);
        WF_CHECK(diff.new_blocks.block_count == 0);
        WF_CHECK(diff.removed_count == 0);
    }

    /* ── Round-trip: verify, apply, and invert a repository diff. ── */

    {
        wf_signing_key key = {0};
        key.type = WF_KEY_TYPE_P256;
        key.bytes[31] = 1;
        const char *did = "did:plc:repodifftest";
        const char *did_key =
            "did:key:zDnaepsL7AXenJkVYdkh5KuKsSU7Ykh7kyXaLLU7auN9FWSiZ";
        unsigned char record_one[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x01};
        unsigned char record_two[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x02};
        unsigned char record_three[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x03};
        unsigned char record_four[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x04};
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
        working.roots = malloc(sizeof(wf_cid));
        WF_CHECK(working.roots != NULL);
        working.roots[0] = commit_base;
        working.root_count = 1;

        unsigned char *base_bytes = NULL;
        size_t base_len = 0;
        WF_CHECK(wf_car_write(&working, &base_bytes, &base_len) == WF_OK);
        wf_car base = {0};
        WF_CHECK(wf_car_parse(base_bytes, base_len, &base) == WF_OK);
        free(base_bytes);

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
        if (!update.blocks) {
            wf_car_free(&base);
            wf_car_free(&working);
            return 1;
        }

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

        wf_repo_verify_options options = {did, did_key, NULL};
        wf_repo_diff diff = {0};
        wf_status diff_status = wf_repo_diff_verify(&base, &commit_base,
                                                    &update, &options, &diff);
        WF_CHECK(diff_status == WF_OK);
        if (diff_status != WF_OK) {
            free(update.blocks);
            wf_car_free(&base);
            wf_car_free(&working);
            return 1;
        }

        WF_CHECK(diff.operation_count == 3);
        WF_CHECK(check_cid(&diff.previous_commit, &commit_base));
        WF_CHECK(check_cid(&diff.commit.cid, &commit_update));
        WF_CHECK(diff.new_blocks.root_count == 1);
        WF_CHECK(diff.new_blocks.block_count > 0);
        WF_CHECK(diff.removed_count > 0);

        const wf_repo_operation *create = NULL;
        const wf_repo_operation *change = NULL;
        const wf_repo_operation *remove = NULL;
        for (size_t i = 0; i < diff.operation_count; i++) {
            const wf_repo_operation *operation = &diff.operations[i];
            WF_CHECK(strcmp(operation->collection, "com.example.posts") == 0);
            if (operation->action == WF_REPO_CREATE) create = operation;
            if (operation->action == WF_REPO_UPDATE) change = operation;
            if (operation->action == WF_REPO_DELETE) remove = operation;
        }
        WF_CHECK(create && strcmp(create->rkey, "added") == 0 &&
                 check_cid(&create->cid, &new_add));
        WF_CHECK(change && strcmp(change->rkey, "keep") == 0 &&
                 check_cid(&change->prev, &old_keep) &&
                 check_cid(&change->cid, &new_keep));
        WF_CHECK(remove && strcmp(remove->rkey, "remove") == 0 &&
                 check_cid(&remove->cid, &old_delete));

        wf_repo_operation *inverse = NULL;
        WF_CHECK(wf_repo_operations_invert(diff.operations,
                                           diff.operation_count,
                                           &inverse) == WF_OK);
        WF_CHECK(inverse != NULL);
        if (inverse) {
            for (size_t i = 0; i < diff.operation_count; i++) {
                const wf_repo_operation *original =
                    &diff.operations[diff.operation_count - i - 1];
                if (original->action == WF_REPO_CREATE) {
                    WF_CHECK(inverse[i].action == WF_REPO_DELETE);
                } else if (original->action == WF_REPO_DELETE) {
                    WF_CHECK(inverse[i].action == WF_REPO_CREATE);
                } else {
                    WF_CHECK(inverse[i].action == WF_REPO_UPDATE);
                    WF_CHECK(check_cid(&inverse[i].cid, &original->prev));
                    WF_CHECK(check_cid(&inverse[i].prev, &original->cid));
                }
                WF_CHECK(strcmp(inverse[i].collection, original->collection) == 0);
                WF_CHECK(strcmp(inverse[i].rkey, original->rkey) == 0);
            }
            wf_repo_operations_free(inverse, diff.operation_count);
        }

        WF_CHECK(wf_repo_diff_apply(&base, &diff) == WF_OK);
        WF_CHECK(check_cid(&base.roots[0], &commit_update));

        unsigned char *found_data = NULL;
        size_t found_len = 0;
        wf_cid found_cid = {0};
        WF_CHECK(wf_repo_get_record(&base, &commit_update,
                                    "com.example.posts", "keep",
                                    &found_data, &found_len, &found_cid) == WF_OK);
        WF_CHECK(found_len == sizeof(record_three));
        WF_CHECK(memcmp(found_data, record_three, found_len) == 0);
        free(found_data);

        WF_CHECK(wf_repo_get_record(&base, &commit_update,
                                    "com.example.posts", "remove",
                                    &found_data, &found_len, &found_cid) == WF_ERR_NOT_FOUND);
        WF_CHECK(wf_repo_get_record(&base, &commit_update,
                                    "com.example.posts", "added",
                                    &found_data, &found_len, &found_cid) == WF_OK);
        WF_CHECK(found_len == sizeof(record_four));
        WF_CHECK(memcmp(found_data, record_four, found_len) == 0);
        free(found_data);

        /* Applying the same diff twice is rejected. */
        WF_CHECK(wf_repo_diff_apply(&base, &diff) == WF_ERR_INVALID_ARG);
        WF_CHECK(check_cid(&base.roots[0], &commit_update));

        wf_repo_diff_free(&diff);
        free(update.blocks);
        wf_car_free(&base);
        wf_car_free(&working);
    }

    WF_TEST_SUMMARY();
}
