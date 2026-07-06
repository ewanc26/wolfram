#include "wolfram/repo/cbor.h"
#include "wolfram/repo/cid.h"
#include "wolfram/repo/car.h"
#include "wolfram/repo/commit.h"
#include "wolfram/repo/mst.h"
#include "wolfram/repo/diff.h"
#include "wolfram/repo/record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int repo_mst_key_cmp(const unsigned char *a, size_t a_len,
                            const unsigned char *b, size_t b_len) {
    size_t min = a_len < b_len ? a_len : b_len;
    int cmp = memcmp(a, b, min);
    if (cmp != 0) return cmp;
    return (int)(a_len - b_len);
}

static size_t repo_mst_find_ge(const wf_mst_node *node,
                               const unsigned char *key, size_t key_len) {
    for (size_t i = 0; i < node->count; i++) {
        if (repo_mst_key_cmp(key, key_len,
                             node->entries[i].key,
                             node->entries[i].key_len) <= 0)
            return i;
    }
    return node->count;
}

static void repo_free_entries(wf_mst_entry *entries, size_t count) {
    for (size_t i = 0; i < count; i++)
        free(entries[i].key);
}

static wf_status repo_mst_load_node_depth(wf_car *car, const wf_cid *cid,
                                          wf_mst_node *out, size_t depth) {
    if (depth > car->block_count) return WF_ERR_PARSE;
    wf_car_block *block = wf_car_find_block(car, cid);
    if (!block) return WF_ERR_PARSE;
    wf_status s = wf_mst_node_parse(block->data, block->data_len, cid, out);
    if (s != WF_OK || out->count > 0 || out->left.len == 0) return s;

    wf_mst_node child = {0};
    s = repo_mst_load_node_depth(car, &out->left, &child, depth + 1);
    if (s == WF_OK) out->layer = child.layer + 1;
    wf_mst_node_free(&child);
    if (s != WF_OK) wf_mst_node_free(out);
    return s;
}

static wf_status repo_mst_load_node(wf_car *car, const wf_cid *cid,
                                    wf_mst_node *out) {
    return repo_mst_load_node_depth(car, cid, out, 0);
}

static wf_status mst_update_value(wf_car *car, const wf_cid *node_cid,
                                  const unsigned char *key, size_t key_len,
                                  const wf_cid *value, wf_cid *new_cid) {
    wf_mst_node node;
    wf_status s = repo_mst_load_node(car, node_cid, &node);
    if (s != WF_OK) return s;

    size_t idx = repo_mst_find_ge(&node, key, key_len);
    int found = idx < node.count &&
        repo_mst_key_cmp(key, key_len, node.entries[idx].key,
                         node.entries[idx].key_len) == 0;
    int use_left = idx == 0;
    size_t parent_idx = idx > 0 ? idx - 1 : 0;
    wf_cid updated_child = {{0}, 0};

    if (!found) {
        const wf_cid *child = use_left ? &node.left
                                       : &node.entries[parent_idx].subtree;
        if (child->len == 0) {
            wf_mst_node_free(&node);
            return WF_ERR_NOT_FOUND;
        }
        s = mst_update_value(car, child, key, key_len, value, &updated_child);
        if (s != WF_OK) {
            wf_mst_node_free(&node);
            return s;
        }
    }

    wf_mst_entry *entries = calloc(node.count, sizeof(*entries));
    if (!entries) {
        wf_mst_node_free(&node);
        return WF_ERR_ALLOC;
    }
    for (size_t i = 0; i < node.count; i++) {
        entries[i].key = malloc(node.entries[i].key_len);
        if (!entries[i].key) {
            repo_free_entries(entries, i);
            free(entries);
            wf_mst_node_free(&node);
            return WF_ERR_ALLOC;
        }
        memcpy(entries[i].key, node.entries[i].key, node.entries[i].key_len);
        entries[i].key_len = node.entries[i].key_len;
        entries[i].value = node.entries[i].value;
        entries[i].subtree = node.entries[i].subtree;
    }

    wf_cid left = node.left;
    if (found)
        entries[idx].value = *value;
    else if (use_left)
        left = updated_child;
    else
        entries[parent_idx].subtree = updated_child;

    wf_mst_node replacement;
    s = wf_mst_node_build(node.layer, &left, entries, node.count, &replacement);
    repo_free_entries(entries, node.count);
    free(entries);
    wf_mst_node_free(&node);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&replacement, car);
    if (s == WF_OK) *new_cid = replacement.cid;
    wf_mst_node_free(&replacement);
    return s;
}

wf_status wf_repo_create_record(wf_car *car,
                                const wf_cid *prev_commit,
                                const char *did,
                                const char *collection,
                                const char *rkey,
                                const unsigned char *record_cbor,
                                size_t record_cbor_len,
                                const wf_signing_key *key,
                                wf_cid *out_commit,
                                wf_cid *out_record) {
    if (!car || !did || !collection || !rkey || !record_cbor ||
        record_cbor_len == 0 || !key || !out_commit || !out_record) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out_commit, 0, sizeof(*out_commit));
    memset(out_record, 0, sizeof(*out_record));

    wf_status s = wf_cid_of_block(record_cbor, record_cbor_len, out_record);
    if (s != WF_OK) return s;

    {
        wf_car_block *new_blocks = realloc(car->blocks,
            (car->block_count + 1) * sizeof(wf_car_block));
        if (!new_blocks) return WF_ERR_ALLOC;
        car->blocks = new_blocks;
        wf_car_block *blk = &car->blocks[car->block_count];
        blk->cid = *out_record;
        blk->data_len = record_cbor_len;
        blk->data = malloc(record_cbor_len);
        if (!blk->data) return WF_ERR_ALLOC;
        memcpy(blk->data, record_cbor, record_cbor_len);
        car->block_count++;
    }

    wf_cid mst_root = {{0}, 0};
    if (prev_commit && prev_commit->len > 0) {
        wf_car_block *block = wf_car_find_block(car, prev_commit);
        if (!block) return WF_ERR_PARSE;
        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        s = wf_commit_parse(block->data, block->data_len, &commit);
        if (s != WF_OK) return s;
        mst_root = commit.data;
    }

    size_t col_len = strlen(collection);
    size_t rkey_len = strlen(rkey);
    size_t key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    wf_cid new_mst_root = {{0}, 0};
    s = wf_mst_add(car, &mst_root, mst_key, key_len, out_record, &new_mst_root);
    free(mst_key);
    if (s != WF_OK) return s;

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    char rev[64];
    snprintf(rev, sizeof(rev), "3z%08x", (unsigned)time(NULL));
    s = wf_commit_create(did, rev, &new_mst_root,
                         (prev_commit && prev_commit->len > 0) ? prev_commit : NULL,
                         key, car, &commit);
    if (s != WF_OK) return s;

    *out_commit = commit.cid;
    return WF_OK;
}

wf_status wf_repo_get_record(wf_car *car,
                             const wf_cid *commit_cid,
                             const char *collection,
                             const char *rkey,
                             unsigned char **out_data,
                             size_t *out_len,
                             wf_cid *out_record_cid) {
    if (!car || !commit_cid || !collection || !rkey ||
        !out_data || !out_len || !out_record_cid) {
        return WF_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_len = 0;
    memset(out_record_cid, 0, sizeof(*out_record_cid));

    wf_car_block *block = wf_car_find_block(car, commit_cid);
    if (!block) return WF_ERR_PARSE;

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    wf_status s = wf_commit_parse(block->data, block->data_len, &commit);
    if (s != WF_OK) return s;

    size_t col_len = strlen(collection);
    size_t rkey_len = strlen(rkey);
    size_t key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    wf_cid found;
    memset(&found, 0, sizeof(found));
    s = wf_mst_find(car, &commit.data, mst_key, key_len, &found);
    free(mst_key);
    if (s != WF_OK) return s;

    wf_car_block *rec_block = wf_car_find_block(car, &found);
    if (!rec_block) return WF_ERR_PARSE;

    *out_data = malloc(rec_block->data_len);
    if (!*out_data && rec_block->data_len > 0) return WF_ERR_ALLOC;
    if (rec_block->data_len > 0)
        memcpy(*out_data, rec_block->data, rec_block->data_len);
    *out_len = rec_block->data_len;
    *out_record_cid = found;

    return WF_OK;
}

wf_status wf_repo_update_record(wf_car *car,
                                const wf_cid *prev_commit,
                                const char *did,
                                const char *collection,
                                const char *rkey,
                                const unsigned char *record_cbor,
                                size_t record_cbor_len,
                                const wf_signing_key *key,
                                wf_cid *out_commit,
                                wf_cid *out_record) {
    if (!car || !prev_commit || prev_commit->len == 0 || !did ||
        !collection || !rkey || !record_cbor || record_cbor_len == 0 ||
        !key || !out_commit || !out_record) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out_commit, 0, sizeof(*out_commit));
    memset(out_record, 0, sizeof(*out_record));

    wf_car_block *commit_block = wf_car_find_block(car, prev_commit);
    if (!commit_block) return WF_ERR_PARSE;
    wf_commit previous;
    memset(&previous, 0, sizeof(previous));
    wf_status s = wf_commit_parse(commit_block->data, commit_block->data_len,
                                  &previous);
    if (s != WF_OK) return s;

    size_t col_len = strlen(collection), rkey_len = strlen(rkey);
    size_t mst_key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(mst_key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    s = wf_cid_of_block(record_cbor, record_cbor_len, out_record);
    if (s != WF_OK) { free(mst_key); return s; }

    wf_cid new_mst_root = {{0}, 0};
    s = mst_update_value(car, &previous.data, mst_key, mst_key_len,
                         out_record, &new_mst_root);
    free(mst_key);
    if (s != WF_OK) return s;

    wf_car_block *blocks = realloc(car->blocks,
        (car->block_count + 1) * sizeof(*blocks));
    if (!blocks) return WF_ERR_ALLOC;
    car->blocks = blocks;
    wf_car_block *record_block = &car->blocks[car->block_count];
    record_block->cid = *out_record;
    record_block->data = malloc(record_cbor_len);
    if (!record_block->data) return WF_ERR_ALLOC;
    memcpy(record_block->data, record_cbor, record_cbor_len);
    record_block->data_len = record_cbor_len;
    car->block_count++;

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    char rev[64];
    snprintf(rev, sizeof(rev), "3z%08x", (unsigned)time(NULL));
    s = wf_commit_create(did, rev, &new_mst_root, prev_commit, key, car,
                         &commit);
    if (s != WF_OK) return s;
    *out_commit = commit.cid;
    return WF_OK;
}

wf_status wf_repo_delete_record(wf_car *car,
                                const wf_cid *prev_commit,
                                const char *did,
                                const char *collection,
                                const char *rkey,
                                const wf_signing_key *key,
                                wf_cid *out_commit) {
    if (!car || !did || !collection || !rkey || !key || !out_commit) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out_commit, 0, sizeof(*out_commit));

    wf_cid mst_root = {{0}, 0};
    if (prev_commit && prev_commit->len > 0) {
        wf_car_block *block = wf_car_find_block(car, prev_commit);
        if (!block) return WF_ERR_PARSE;
        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        wf_status s = wf_commit_parse(block->data, block->data_len, &commit);
        if (s != WF_OK) return s;
        mst_root = commit.data;
    }

    size_t col_len = strlen(collection);
    size_t rkey_len = strlen(rkey);
    size_t key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    wf_cid new_mst_root = {{0}, 0};
    wf_status s = wf_mst_delete(car, &mst_root, mst_key, key_len, &new_mst_root);
    free(mst_key);
    if (s != WF_OK) return s;

    if (new_mst_root.len == 0) {
        wf_cid empty = {{0}, 0};
        wf_mst_node empty_node;
        s = wf_mst_node_build(0, &empty, NULL, 0, &empty_node);
        if (s != WF_OK) return s;
        s = wf_mst_node_finalize(&empty_node, car);
        if (s == WF_OK) new_mst_root = empty_node.cid;
        wf_mst_node_free(&empty_node);
        if (s != WF_OK) return s;
    }

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    char rev[64];
    snprintf(rev, sizeof(rev), "3z%08x", (unsigned)time(NULL));
    s = wf_commit_create(did, rev, &new_mst_root,
                         (prev_commit && prev_commit->len > 0) ? prev_commit : NULL,
                         key, car, &commit);
    if (s != WF_OK) return s;

    *out_commit = commit.cid;
    return WF_OK;
}
