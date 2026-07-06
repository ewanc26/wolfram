#include "wolfram/repo/diff.h"
#include "wolfram/repo/cbor.h"
#include "wolfram/repo/cid.h"
#include "wolfram/repo/car.h"
#include "wolfram/repo/commit.h"
#include "wolfram/repo/mst.h"
#include "cbor_build.h"
#include "cbor_map_find.h"

#include <stdlib.h>
#include <string.h>

typedef struct repo_leaf {
    unsigned char *key;
    size_t key_len;
    wf_cid cid;
} repo_leaf;

typedef struct repo_leaf_list {
    repo_leaf *items;
    size_t count;
} repo_leaf_list;

typedef struct repo_cid_list {
    wf_cid *items;
    size_t count;
} repo_cid_list;

static int repo_key_cmp(const unsigned char *a, size_t a_len,
                        const unsigned char *b, size_t b_len) {
    size_t min = a_len < b_len ? a_len : b_len;
    int cmp = memcmp(a, b, min);
    if (cmp != 0) return cmp;
    return (int)(a_len - b_len);
}

static void repo_leaf_list_free(repo_leaf_list *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i].key);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int repo_cid_list_has(const repo_cid_list *list, const wf_cid *cid) {
    for (size_t i = 0; list && i < list->count; i++)
        if (cid_equal(&list->items[i], cid)) return 1;
    return 0;
}

static wf_status repo_cid_list_add(repo_cid_list *list, const wf_cid *cid) {
    if (repo_cid_list_has(list, cid)) return WF_OK;
    wf_cid *items = realloc(list->items, (list->count + 1) * sizeof(*items));
    if (!items) return WF_ERR_ALLOC;
    list->items = items;
    list->items[list->count++] = *cid;
    return WF_OK;
}

static wf_status repo_leaf_add(repo_leaf_list *list,
                               const unsigned char *key, size_t key_len,
                               const wf_cid *cid) {
    if (key_len == 0 || !cid || cid->len != 36) return WF_ERR_PARSE;
    if (list->count > 0) {
        repo_leaf *previous = &list->items[list->count - 1];
        if (repo_key_cmp(previous->key, previous->key_len,
                          key, key_len) >= 0) return WF_ERR_PARSE;
    }
    repo_leaf *items = realloc(list->items,
                               (list->count + 1) * sizeof(*items));
    if (!items) return WF_ERR_ALLOC;
    list->items = items;
    repo_leaf *leaf = &list->items[list->count];
    memset(leaf, 0, sizeof(*leaf));
    leaf->key = malloc(key_len);
    if (!leaf->key) return WF_ERR_ALLOC;
    memcpy(leaf->key, key, key_len);
    leaf->key_len = key_len;
    leaf->cid = *cid;
    list->count++;
    return WF_OK;
}

static wf_status repo_collect_mst(const wf_car *car, const wf_cid *root,
                                  repo_leaf_list *leaves,
                                  repo_cid_list *nodes, size_t depth) {
    if (!car || !root || root->len != 36 || depth > car->block_count)
        return WF_ERR_PARSE;
    if (repo_cid_list_has(nodes, root)) return WF_ERR_PARSE;
    wf_status status = repo_cid_list_add(nodes, root);
    if (status != WF_OK) return status;
    wf_car_block *block = wf_car_find_block((wf_car *)car, root);
    if (!block) return WF_ERR_NOT_FOUND;
    wf_mst_node node = {0};
    status = wf_mst_node_parse(block->data, block->data_len, root, &node);
    if (status != WF_OK) return status;
    if (node.left.len)
        status = repo_collect_mst(car, &node.left, leaves, nodes, depth + 1);
    for (size_t i = 0; status == WF_OK && i < node.count; i++) {
        if (!wf_car_find_block((wf_car *)car, &node.entries[i].value)) {
            status = WF_ERR_NOT_FOUND;
            break;
        }
        status = repo_leaf_add(leaves, node.entries[i].key,
                               node.entries[i].key_len,
                               &node.entries[i].value);
        if (status == WF_OK && node.entries[i].subtree.len)
            status = repo_collect_mst(car, &node.entries[i].subtree,
                                      leaves, nodes, depth + 1);
    }
    wf_mst_node_free(&node);
    return status;
}

static wf_status repo_car_validate_blocks(const wf_car *car) {
    if (!car) return WF_ERR_INVALID_ARG;
    for (size_t i = 0; i < car->block_count; i++) {
        wf_cid computed = {0};
        if (!car->blocks[i].data || car->blocks[i].data_len == 0 ||
            wf_cid_of_block(car->blocks[i].data, car->blocks[i].data_len,
                            &computed) != WF_OK ||
            !cid_equal(&computed, &car->blocks[i].cid)) return WF_ERR_PARSE;
        for (size_t j = 0; j < i; j++)
            if (cid_equal(&car->blocks[i].cid, &car->blocks[j].cid))
                return WF_ERR_PARSE;
    }
    return WF_OK;
}

static wf_status repo_car_add_copy(wf_car *car, const wf_car_block *block) {
    wf_car_block *existing = wf_car_find_block(car, &block->cid);
    if (existing) {
        return existing->data_len == block->data_len &&
               memcmp(existing->data, block->data, block->data_len) == 0
            ? WF_OK : WF_ERR_PARSE;
    }
    wf_car_block *blocks = realloc(car->blocks,
        (car->block_count + 1) * sizeof(*blocks));
    if (!blocks) return WF_ERR_ALLOC;
    car->blocks = blocks;
    wf_car_block *copy = &car->blocks[car->block_count];
    memset(copy, 0, sizeof(*copy));
    copy->data = malloc(block->data_len);
    if (!copy->data) return WF_ERR_ALLOC;
    memcpy(copy->data, block->data, block->data_len);
    copy->data_len = block->data_len;
    copy->cid = block->cid;
    car->block_count++;
    return WF_OK;
}

static wf_status repo_car_add_cid_copy(wf_car *destination,
                                       const wf_car *source,
                                       const wf_cid *cid) {
    wf_car_block *block = wf_car_find_block((wf_car *)source, cid);
    return block ? repo_car_add_copy(destination, block) : WF_ERR_NOT_FOUND;
}

static wf_status repo_composite_car(const wf_car *base, const wf_car *update,
                                    wf_car *out) {
    memset(out, 0, sizeof(*out));
    out->roots = update->roots;
    out->root_count = update->root_count;
    size_t capacity = base->block_count + update->block_count;
    out->blocks = capacity ? calloc(capacity, sizeof(*out->blocks)) : NULL;
    if (capacity && !out->blocks) return WF_ERR_ALLOC;
    for (size_t pass = 0; pass < 2; pass++) {
        const wf_car *source = pass == 0 ? update : base;
        for (size_t i = 0; i < source->block_count; i++) {
            wf_car_block *present = wf_car_find_block(out,
                                                      &source->blocks[i].cid);
            if (present) {
                if (present->data_len != source->blocks[i].data_len ||
                    memcmp(present->data, source->blocks[i].data,
                           present->data_len) != 0) {
                    free(out->blocks);
                    memset(out, 0, sizeof(*out));
                    return WF_ERR_PARSE;
                }
                continue;
            }
            out->blocks[out->block_count++] = source->blocks[i];
        }
    }
    return WF_OK;
}

static char *repo_string_copy(const char *value) {
    size_t len = strlen(value);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, value, len + 1);
    return copy;
}

void wf_repo_operations_free(wf_repo_operation *operations, size_t count) {
    if (!operations) return;
    for (size_t i = 0; i < count; i++) {
        free(operations[i].collection);
        free(operations[i].rkey);
    }
    free(operations);
}

wf_status wf_repo_operations_invert(const wf_repo_operation *operations,
                                    size_t count,
                                    wf_repo_operation **out_operations) {
    if ((!operations && count > 0) || !out_operations)
        return WF_ERR_INVALID_ARG;
    *out_operations = NULL;
    if (count == 0) return WF_OK;
    wf_repo_operation *inverted = calloc(count, sizeof(*inverted));
    if (!inverted) return WF_ERR_ALLOC;
    for (size_t i = 0; i < count; i++) {
        const wf_repo_operation *source = &operations[count - i - 1];
        wf_repo_operation *target = &inverted[i];
        if (!source->collection || !source->collection[0] ||
            !source->rkey || !source->rkey[0] || source->cid.len != 36 ||
            (source->action == WF_REPO_UPDATE && source->prev.len != 36) ||
            source->action < WF_REPO_CREATE ||
            source->action > WF_REPO_DELETE) {
            wf_repo_operations_free(inverted, count);
            return WF_ERR_INVALID_ARG;
        }
        target->collection = repo_string_copy(source->collection);
        target->rkey = repo_string_copy(source->rkey);
        if (!target->collection || !target->rkey) {
            wf_repo_operations_free(inverted, count);
            return WF_ERR_ALLOC;
        }
        if (source->action == WF_REPO_CREATE) {
            target->action = WF_REPO_DELETE;
            target->cid = source->cid;
        } else if (source->action == WF_REPO_DELETE) {
            target->action = WF_REPO_CREATE;
            target->cid = source->cid;
        } else {
            target->action = WF_REPO_UPDATE;
            target->cid = source->prev;
            target->prev = source->cid;
        }
    }
    *out_operations = inverted;
    return WF_OK;
}

void wf_repo_diff_free(wf_repo_diff *diff) {
    if (!diff) return;
    wf_repo_operations_free(diff->operations, diff->operation_count);
    wf_car_free(&diff->new_blocks);
    free(diff->removed_cids);
    memset(diff, 0, sizeof(*diff));
}

static wf_status repo_operation_from_leaf(wf_repo_operation *operation,
                                          wf_repo_operation_action action,
                                          const repo_leaf *leaf) {
    const unsigned char *slash = memchr(leaf->key, '/', leaf->key_len);
    if (!slash || slash == leaf->key || slash == leaf->key + leaf->key_len - 1)
        return WF_ERR_PARSE;
    size_t collection_len = (size_t)(slash - leaf->key);
    size_t rkey_len = leaf->key_len - collection_len - 1;
    if (memchr(slash + 1, '/', rkey_len) ||
        memchr(leaf->key, '\0', leaf->key_len)) return WF_ERR_PARSE;
    operation->collection = malloc(collection_len + 1);
    operation->rkey = malloc(rkey_len + 1);
    if (!operation->collection || !operation->rkey) {
        free(operation->collection);
        free(operation->rkey);
        operation->collection = NULL;
        operation->rkey = NULL;
        return WF_ERR_ALLOC;
    }
    memcpy(operation->collection, leaf->key, collection_len);
    operation->collection[collection_len] = '\0';
    memcpy(operation->rkey, slash + 1, rkey_len);
    operation->rkey[rkey_len] = '\0';
    operation->action = action;
    operation->cid = leaf->cid;
    return WF_OK;
}

static wf_status repo_diff_operations(const repo_leaf_list *previous,
                                      const repo_leaf_list *current,
                                      wf_repo_diff *out) {
    size_t capacity = previous->count + current->count;
    out->operations = capacity ? calloc(capacity, sizeof(*out->operations))
                               : NULL;
    if (capacity && !out->operations) return WF_ERR_ALLOC;
    size_t old_index = 0, new_index = 0;
    while (old_index < previous->count || new_index < current->count) {
        int comparison;
        if (old_index == previous->count) comparison = 1;
        else if (new_index == current->count) comparison = -1;
        else comparison = repo_key_cmp(previous->items[old_index].key,
                                       previous->items[old_index].key_len,
                                       current->items[new_index].key,
                                       current->items[new_index].key_len);
        wf_repo_operation *operation = &out->operations[out->operation_count];
        wf_status status;
        if (comparison < 0) {
            status = repo_operation_from_leaf(operation, WF_REPO_DELETE,
                                              &previous->items[old_index++]);
        } else if (comparison > 0) {
            status = repo_operation_from_leaf(operation, WF_REPO_CREATE,
                                              &current->items[new_index++]);
        } else {
            const repo_leaf *old_leaf = &previous->items[old_index++];
            const repo_leaf *new_leaf = &current->items[new_index++];
            if (cid_equal(&old_leaf->cid, &new_leaf->cid)) continue;
            status = repo_operation_from_leaf(operation, WF_REPO_UPDATE,
                                              new_leaf);
            if (status == WF_OK) operation->prev = old_leaf->cid;
        }
        if (status != WF_OK) return status;
        out->operation_count++;
    }
    return WF_OK;
}

static int repo_leaves_have_cid(const repo_leaf_list *leaves,
                                const wf_cid *cid) {
    for (size_t i = 0; i < leaves->count; i++)
        if (cid_equal(&leaves->items[i].cid, cid)) return 1;
    return 0;
}

static wf_status repo_diff_add_removed(wf_repo_diff *diff,
                                       const wf_cid *cid) {
    repo_cid_list view = {diff->removed_cids, diff->removed_count};
    if (repo_cid_list_has(&view, cid)) return WF_OK;
    wf_cid *items = realloc(diff->removed_cids,
                            (diff->removed_count + 1) * sizeof(*items));
    if (!items) return WF_ERR_ALLOC;
    diff->removed_cids = items;
    diff->removed_cids[diff->removed_count++] = *cid;
    return WF_OK;
}

static wf_status commit_unsigned_bytes(const wf_commit *commit,
                                       unsigned char **out,
                                       size_t *out_len) {
    wf_cbor_item *map = calloc(1, sizeof(*map));
    if (!map) return WF_ERR_ALLOC;
    map->type = WF_CBOR_MAP;
    map->map.count = 5;
    map->map.pairs = calloc(5, sizeof(wf_cbor_pair));
    if (!map->map.pairs) { free(map); return WF_ERR_ALLOC; }
    map->map.pairs[0].key = cbor_str("did");
    map->map.pairs[0].value = cbor_str(commit->did);
    map->map.pairs[1].key = cbor_str("rev");
    map->map.pairs[1].value = cbor_str(commit->rev);
    map->map.pairs[2].key = cbor_str("data");
    map->map.pairs[2].value = cbor_cid(&commit->data);
    map->map.pairs[3].key = cbor_str("prev");
    map->map.pairs[3].value = commit->has_prev
        ? cbor_cid(&commit->prev) : cbor_null();
    map->map.pairs[4].key = cbor_str("version");
    map->map.pairs[4].value = cbor_uint((uint64_t)commit->version);
    *out = wf_cbor_serialize(map, out_len);
    wf_cbor_free(map);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status verify_mst_links(const wf_car *car, const wf_cid *cid,
                                  size_t depth) {
    if (depth > car->block_count) return WF_ERR_PARSE;
    wf_car_block *block = wf_car_find_block((wf_car *)car, cid);
    if (!block) return WF_ERR_NOT_FOUND;
    wf_mst_node node;
    wf_status status = wf_mst_node_parse(block->data, block->data_len,
                                         cid, &node);
    if (status != WF_OK) return status;
    if (node.left.len) {
        status = verify_mst_links(car, &node.left, depth + 1);
    }
    for (size_t i = 0; status == WF_OK && i < node.count; i++) {
        if (!wf_car_find_block((wf_car *)car, &node.entries[i].value)) {
            status = WF_ERR_NOT_FOUND;
            break;
        }
        if (node.entries[i].subtree.len) {
            status = verify_mst_links(car, &node.entries[i].subtree, depth + 1);
        }
    }
    wf_mst_node_free(&node);
    return status;
}

wf_status wf_repo_verify(const wf_car *car,
                         const wf_repo_verify_options *options,
                         wf_commit *out_commit) {
    if (!car || !options || !options->expected_did ||
        options->expected_did[0] == '\0' || !options->signing_key ||
        options->signing_key[0] == '\0' || !out_commit) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out_commit, 0, sizeof(*out_commit));
    if (car->root_count != 1 || car->roots[0].len != 36) return WF_ERR_PARSE;

    for (size_t i = 0; i < car->block_count; i++) {
        wf_cid computed;
        if (!car->blocks[i].data || car->blocks[i].data_len == 0 ||
            wf_cid_of_block(car->blocks[i].data, car->blocks[i].data_len,
                            &computed) != WF_OK ||
            !cid_equal(&computed, &car->blocks[i].cid)) return WF_ERR_PARSE;
        for (size_t j = 0; j < i; j++) {
            if (cid_equal(&car->blocks[i].cid, &car->blocks[j].cid))
                return WF_ERR_PARSE;
        }
    }

    wf_car_block *root = wf_car_find_block((wf_car *)car, &car->roots[0]);
    if (!root) return WF_ERR_NOT_FOUND;
    wf_commit commit;
    wf_status status = wf_commit_parse(root->data, root->data_len, &commit);
    if (status != WF_OK || commit.version != 3 || commit.sig_len != 64)
        return WF_ERR_PARSE;
    wf_cbor_item *root_object = wf_cbor_parse(root->data, root->data_len);
    if (!root_object || root_object->type != WF_CBOR_MAP ||
        root_object->map.count != 6 ||
        !wf_cbor_map_find(root_object, "prev", 4)) {
        wf_cbor_free(root_object);
        return WF_ERR_PARSE;
    }
    wf_cbor_free(root_object);
    commit.cid = car->roots[0];
    if (strcmp(commit.did, options->expected_did) != 0) return WF_ERR_PARSE;
    if (options->expected_prev &&
        (!commit.has_prev || !cid_equal(&commit.prev, options->expected_prev)))
        return WF_ERR_PARSE;

    unsigned char *unsigned_commit = NULL;
    size_t unsigned_len = 0;
    status = commit_unsigned_bytes(&commit, &unsigned_commit, &unsigned_len);
    if (status != WF_OK) return status;
    status = wf_verify(options->signing_key, unsigned_commit, unsigned_len,
                       commit.sig, commit.sig_len);
    free(unsigned_commit);
    if (status != WF_OK) return status;

    status = verify_mst_links(car, &commit.data, 0);
    if (status != WF_OK) return status;
    *out_commit = commit;
    return WF_OK;
}

wf_status wf_repo_import(const unsigned char *bytes, size_t len,
                         const wf_repo_verify_options *options,
                         wf_car *out_car, wf_commit *out_commit) {
    if (!bytes || len == 0 || !options || !out_car || !out_commit)
        return WF_ERR_INVALID_ARG;
    memset(out_car, 0, sizeof(*out_car));
    memset(out_commit, 0, sizeof(*out_commit));
    wf_status status = wf_car_parse(bytes, len, out_car);
    if (status != WF_OK) return status;
    status = wf_repo_verify(out_car, options, out_commit);
    if (status != WF_OK) {
        wf_car_free(out_car);
        memset(out_commit, 0, sizeof(*out_commit));
    }
    return status;
}

wf_status wf_repo_diff_verify(const wf_car *base,
                              const wf_cid *base_commit,
                              const wf_car *update,
                              const wf_repo_verify_options *options,
                              wf_repo_diff *out) {
    if (!base || !base_commit || base_commit->len != 36 || !update ||
        !options || !out || !options->expected_did ||
        !options->signing_key) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (base->root_count != 1 || !cid_equal(&base->roots[0], base_commit) ||
        update->root_count != 1 || update->roots[0].len != 36)
        return WF_ERR_PARSE;

    wf_repo_verify_options base_options = *options;
    base_options.expected_prev = NULL;
    wf_commit previous_commit = {0};
    wf_status status = wf_repo_verify(base, &base_options, &previous_commit);
    if (status != WF_OK) return status;
    status = repo_car_validate_blocks(update);
    if (status != WF_OK) return status;

    wf_car composite = {0};
    status = repo_composite_car(base, update, &composite);
    if (status != WF_OK) return status;
    wf_commit current_commit = {0};
    status = wf_repo_verify(&composite, options, &current_commit);
    if (status != WF_OK) {
        free(composite.blocks);
        return status;
    }

    repo_leaf_list previous_leaves = {0}, current_leaves = {0};
    repo_cid_list previous_nodes = {0}, current_nodes = {0};
    status = repo_collect_mst(&composite, &previous_commit.data,
                              &previous_leaves, &previous_nodes, 0);
    if (status == WF_OK)
        status = repo_collect_mst(&composite, &current_commit.data,
                                  &current_leaves, &current_nodes, 0);
    if (status == WF_OK)
        status = repo_diff_operations(&previous_leaves, &current_leaves, out);
    if (status != WF_OK) goto cleanup_diff;

    out->commit = current_commit;
    out->previous_commit = *base_commit;
    memcpy(out->since, previous_commit.rev, sizeof(out->since));
    out->new_blocks.roots = malloc(sizeof(wf_cid));
    if (!out->new_blocks.roots) { status = WF_ERR_ALLOC; goto cleanup_diff; }
    out->new_blocks.roots[0] = current_commit.cid;
    out->new_blocks.root_count = 1;

    for (size_t i = 0; i < current_nodes.count; i++) {
        if (!repo_cid_list_has(&previous_nodes, &current_nodes.items[i])) {
            status = repo_car_add_cid_copy(&out->new_blocks, &composite,
                                           &current_nodes.items[i]);
            if (status != WF_OK) goto cleanup_diff;
        }
    }
    for (size_t i = 0; i < out->operation_count; i++) {
        wf_repo_operation *operation = &out->operations[i];
        if (operation->action == WF_REPO_DELETE) continue;
        wf_car_block *leaf = wf_car_find_block((wf_car *)update,
                                               &operation->cid);
        if (!leaf) { status = WF_ERR_NOT_FOUND; goto cleanup_diff; }
        status = repo_car_add_copy(&out->new_blocks, leaf);
        if (status != WF_OK) goto cleanup_diff;
    }
    if (!cid_equal(&current_commit.cid, base_commit)) {
        status = repo_car_add_cid_copy(&out->new_blocks, &composite,
                                       &current_commit.cid);
        if (status != WF_OK) goto cleanup_diff;
    }
    for (size_t i = 0; i < previous_nodes.count; i++) {
        if (!repo_cid_list_has(&current_nodes, &previous_nodes.items[i])) {
            status = repo_diff_add_removed(out, &previous_nodes.items[i]);
            if (status != WF_OK) goto cleanup_diff;
        }
    }
    for (size_t i = 0; i < out->operation_count; i++) {
        wf_repo_operation *operation = &out->operations[i];
        const wf_cid *old_cid = operation->action == WF_REPO_UPDATE
            ? &operation->prev : &operation->cid;
        if (operation->action != WF_REPO_CREATE &&
            !repo_leaves_have_cid(&current_leaves, old_cid)) {
            status = repo_diff_add_removed(out, old_cid);
            if (status != WF_OK) goto cleanup_diff;
        }
    }
    if (!cid_equal(base_commit, &current_commit.cid))
        status = repo_diff_add_removed(out, base_commit);

cleanup_diff:
    free(composite.blocks);
    repo_leaf_list_free(&previous_leaves);
    repo_leaf_list_free(&current_leaves);
    free(previous_nodes.items);
    free(current_nodes.items);
    if (status != WF_OK) wf_repo_diff_free(out);
    return status;
}

static int repo_removed_has(const wf_repo_diff *diff, const wf_cid *cid) {
    for (size_t i = 0; i < diff->removed_count; i++)
        if (cid_equal(&diff->removed_cids[i], cid)) return 1;
    return 0;
}

wf_status wf_repo_diff_apply(wf_car *repo, const wf_repo_diff *diff) {
    if (!repo || !diff || (diff->operation_count && !diff->operations) ||
        (diff->removed_count && !diff->removed_cids) ||
        diff->previous_commit.len != 36 || diff->commit.cid.len != 36 ||
        repo->root_count != 1 ||
        !cid_equal(&repo->roots[0], &diff->previous_commit) ||
        diff->new_blocks.root_count != 1 ||
        !cid_equal(&diff->new_blocks.roots[0], &diff->commit.cid))
        return WF_ERR_INVALID_ARG;

    wf_status status = repo_car_validate_blocks(&diff->new_blocks);
    if (status != WF_OK) return status;
    wf_car staged = {0};
    staged.roots = malloc(sizeof(wf_cid));
    if (!staged.roots) return WF_ERR_ALLOC;
    staged.roots[0] = diff->commit.cid;
    staged.root_count = 1;
    for (size_t i = 0; i < repo->block_count; i++) {
        if (repo_removed_has(diff, &repo->blocks[i].cid)) continue;
        status = repo_car_add_copy(&staged, &repo->blocks[i]);
        if (status != WF_OK) goto apply_fail;
    }
    for (size_t i = 0; i < diff->new_blocks.block_count; i++) {
        status = repo_car_add_copy(&staged, &diff->new_blocks.blocks[i]);
        if (status != WF_OK) goto apply_fail;
    }
    wf_car_block *commit_block = wf_car_find_block(&staged, &diff->commit.cid);
    wf_commit parsed = {0};
    if (!commit_block) { status = WF_ERR_NOT_FOUND; goto apply_fail; }
    status = wf_commit_parse(commit_block->data, commit_block->data_len,
                             &parsed);
    if (status != WF_OK || strcmp(parsed.did, diff->commit.did) != 0 ||
        strcmp(parsed.rev, diff->commit.rev) != 0 ||
        parsed.version != diff->commit.version ||
        parsed.has_prev != diff->commit.has_prev ||
        (parsed.has_prev && !cid_equal(&parsed.prev, &diff->commit.prev)) ||
        parsed.sig_len != diff->commit.sig_len ||
        memcmp(parsed.sig, diff->commit.sig, parsed.sig_len) != 0 ||
        !cid_equal(&parsed.data, &diff->commit.data)) {
        status = WF_ERR_PARSE;
        goto apply_fail;
    }
    status = verify_mst_links(&staged, &parsed.data, 0);
    if (status != WF_OK) goto apply_fail;
    wf_car_free(repo);
    *repo = staged;
    return WF_OK;

apply_fail:
    wf_car_free(&staged);
    return status;
}
