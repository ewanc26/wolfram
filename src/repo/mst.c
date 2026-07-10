#include "wolfram/repo/mst.h"
#include "wolfram/repo/cbor.h"
#include "wolfram/repo/cid.h"
#include "wolfram/repo/car.h"
#include "cbor_build.h"
#include "cbor_map_find.h"

#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>

unsigned wf_mst_key_layer(const unsigned char *key, size_t key_len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(key, key_len, hash);

    unsigned count = 0;
    for (int i = 0; i < (int)sizeof(hash); i++) {
        unsigned char b = hash[i];
        if (b <  64) count++;
        if (b <  16) count++;
        if (b <   4) count++;
        if (b ==  0) count++;
        else break;
    }
    return count;
}

static int wf_mst_key_cmp(const unsigned char *a, size_t a_len,
                          const unsigned char *b, size_t b_len) {
    size_t min = a_len < b_len ? a_len : b_len;
    int cmp = memcmp(a, b, min);
    if (cmp != 0) return cmp;
    return (int)(a_len - b_len);
}

wf_status wf_mst_node_parse(const unsigned char *cbor, size_t len,
                            const wf_cid *cid, wf_mst_node *out) {
    if (!cbor || len == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (cid) out->cid = *cid;

    wf_cbor_item *obj = wf_cbor_parse(cbor, len);
    if (!obj) return WF_ERR_PARSE;
    if (obj->type != WF_CBOR_MAP) { wf_cbor_free(obj); return WF_ERR_PARSE; }

    wf_cbor_item *l_item = wf_cbor_map_find(obj, "l", 1);
    if (l_item && l_item->type == WF_CBOR_LINK && l_item->bytes.len == 36) {
        memcpy(out->left.bytes, l_item->bytes.data, 36);
        out->left.len = 36;
    }

    wf_cbor_item *e_item = wf_cbor_map_find(obj, "e", 1);
    if (!e_item || e_item->type != WF_CBOR_ARRAY) {
        wf_cbor_free(obj);
        return WF_ERR_PARSE;
    }

    out->count = e_item->children.count;
    if (out->count == 0) {
        wf_cbor_free(obj);
        return WF_OK;
    }

    out->entries = calloc(out->count, sizeof(wf_mst_entry));
    if (!out->entries) { wf_cbor_free(obj); return WF_ERR_ALLOC; }

    unsigned char *prev_key = NULL;
    size_t prev_key_len = 0;

    for (size_t i = 0; i < out->count; i++) {
        wf_cbor_item *entry = e_item->children.items[i];
        if (entry->type != WF_CBOR_MAP) {
            wf_mst_node_free(out);
            wf_cbor_free(obj);
            return WF_ERR_PARSE;
        }

        wf_mst_entry *ent = &out->entries[i];

        wf_cbor_item *p_item = wf_cbor_map_find(entry, "p", 1);
        if (!p_item || p_item->type != WF_CBOR_UNSIGNED) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }
        uint64_t prefix_len = p_item->uinteger;
        if (prefix_len > prev_key_len) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }

        wf_cbor_item *k_item = wf_cbor_map_find(entry, "k", 1);
        if (!k_item || k_item->type != WF_CBOR_BYTES) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }

        size_t full_len = (size_t)prefix_len + k_item->bytes.len;
        ent->key = malloc(full_len);
        if (!ent->key) { wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_ALLOC; }
        if (prefix_len > 0 && prev_key)
            memcpy(ent->key, prev_key, (size_t)prefix_len);
        if (k_item->bytes.len > 0)
            memcpy(ent->key + prefix_len, k_item->bytes.data, k_item->bytes.len);
        ent->key_len = full_len;

        wf_cbor_item *v_item = wf_cbor_map_find(entry, "v", 1);
        if (!v_item || v_item->type != WF_CBOR_LINK || v_item->bytes.len != 36) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }
        memcpy(ent->value.bytes, v_item->bytes.data, 36);
        ent->value.len = 36;

        wf_cbor_item *t_item = wf_cbor_map_find(entry, "t", 1);
        if (t_item && t_item->type == WF_CBOR_LINK && t_item->bytes.len == 36) {
            memcpy(ent->subtree.bytes, t_item->bytes.data, 36);
            ent->subtree.len = 36;
        }

        free(prev_key);
        prev_key = malloc(full_len);
        if (prev_key) {
            memcpy(prev_key, ent->key, full_len);
            prev_key_len = full_len;
        } else {
            prev_key_len = 0;
        }
    }

    free(prev_key);

    for (size_t i = 1; i < out->count; i++) {
        if (wf_mst_key_cmp(out->entries[i - 1].key, out->entries[i - 1].key_len,
                           out->entries[i].key, out->entries[i].key_len) >= 0) {
            wf_mst_node_free(out);
            wf_cbor_free(obj);
            return WF_ERR_PARSE;
        }
    }

    out->layer = (out->count > 0)
        ? wf_mst_key_layer(out->entries[0].key, out->entries[0].key_len)
        : 0;

    wf_cbor_free(obj);
    return WF_OK;
}

void wf_mst_node_free(wf_mst_node *node) {
    if (!node) return;
    for (size_t i = 0; i < node->count; i++)
        free(node->entries[i].key);
    free(node->entries);
    node->entries = NULL;
    node->count = 0;
}

wf_status wf_mst_find(wf_car *car, const wf_cid *root_cid,
                      const unsigned char *key, size_t key_len,
                      wf_cid *out) {
    if (!car || !root_cid || !key || key_len == 0 || !out)
        return WF_ERR_INVALID_ARG;

    wf_cid current = *root_cid;

    while (1) {
        wf_car_block *block = wf_car_find_block(car, &current);
        if (!block) return WF_ERR_PARSE;

        wf_mst_node node;
        wf_status s = wf_mst_node_parse(block->data, block->data_len,
                                        &current, &node);
        if (s != WF_OK) return s;

        size_t idx = 0;
        for (; idx < node.count; idx++) {
            int cmp = wf_mst_key_cmp(key, key_len,
                                      node.entries[idx].key,
                                      node.entries[idx].key_len);
            if (cmp == 0) {
                *out = node.entries[idx].value;
                wf_mst_node_free(&node);
                return WF_OK;
            }
            if (cmp < 0) break;
        }

        wf_cid *subtree = NULL;
        if (idx == 0) {
            if (node.left.len > 0) subtree = &node.left;
        } else {
            if (node.entries[idx - 1].subtree.len > 0)
                subtree = &node.entries[idx - 1].subtree;
        }

        if (!subtree && idx >= node.count && node.count > 0) {
            if (node.entries[node.count - 1].subtree.len > 0)
                subtree = &node.entries[node.count - 1].subtree;
        }

        if (subtree && subtree->len > 0) {
            current = *subtree;
            wf_mst_node_free(&node);
            continue;
        }

        wf_mst_node_free(&node);
        return WF_ERR_NOT_FOUND;
    }
}

wf_status wf_mst_node_build(unsigned layer, const wf_cid *left,
                            wf_mst_entry *entries, size_t count,
                            wf_mst_node *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->layer = layer;
    if (left) out->left = *left;
    out->count = count;
    if (count > 0) {
        out->entries = calloc(count, sizeof(wf_mst_entry));
        if (!out->entries) return WF_ERR_ALLOC;
        for (size_t i = 0; i < count; i++) {
            out->entries[i].key = entries[i].key;
            out->entries[i].key_len = entries[i].key_len;
            out->entries[i].value = entries[i].value;
            out->entries[i].subtree = entries[i].subtree;
            entries[i].key = NULL;
        }
    }
    return WF_OK;
}

wf_status wf_mst_node_finalize(wf_mst_node *node, wf_car *car) {
    if (!node || !car) return WF_ERR_INVALID_ARG;
    if (node->cid.len > 0) return WF_OK;

    unsigned char *prev_key = NULL;
    size_t prev_key_len = 0;
    int err = 0;

    wf_cbor_item *arr = calloc(1, sizeof(*arr));
    if (!arr) return WF_ERR_ALLOC;
    arr->type = WF_CBOR_ARRAY;
    arr->children.count = node->count;
    if (node->count > 0) {
        arr->children.items = calloc(node->count, sizeof(wf_cbor_item *));
        if (!arr->children.items) { free(arr); return WF_ERR_ALLOC; }
    }

    for (size_t i = 0; i < node->count; i++) {
        wf_mst_entry *src = &node->entries[i];
        uint64_t prefix = 0;
        if (prev_key) {
            size_t min = prev_key_len < src->key_len ? prev_key_len : src->key_len;
            while (prefix < min && prev_key[prefix] == src->key[prefix])
                prefix++;
        }

        wf_cbor_item *entry = calloc(1, sizeof(*entry));
        if (!entry) { err = 1; break; }
        entry->type = WF_CBOR_MAP;
        entry->map.count = 4;
        entry->map.pairs = calloc(4, sizeof(wf_cbor_pair));
        if (!entry->map.pairs) { free(entry); err = 1; break; }

        entry->map.pairs[0].key = cbor_str("k");
        entry->map.pairs[0].value = cbor_bytes(src->key + prefix,
                                               src->key_len - (size_t)prefix);
        entry->map.pairs[1].key = cbor_str("p");
        entry->map.pairs[1].value = cbor_uint(prefix);
        entry->map.pairs[2].key = cbor_str("t");
        entry->map.pairs[2].value = (src->subtree.len > 0)
            ? cbor_cid(&src->subtree) : cbor_null();
        entry->map.pairs[3].key = cbor_str("v");
        entry->map.pairs[3].value = cbor_cid(&src->value);

        free(prev_key);
        prev_key = malloc(src->key_len);
        if (prev_key) {
            memcpy(prev_key, src->key, src->key_len);
            prev_key_len = src->key_len;
        }
        arr->children.items[i] = entry;
    }
    free(prev_key);
    if (err) {
        wf_cbor_free(arr);
        return WF_ERR_ALLOC;
    }

    wf_cbor_item *map = calloc(1, sizeof(*map));
    if (!map) { wf_cbor_free(arr); return WF_ERR_ALLOC; }
    map->type = WF_CBOR_MAP;
    map->map.count = 2;
    map->map.pairs = calloc(2, sizeof(wf_cbor_pair));
    if (!map->map.pairs) { free(map); wf_cbor_free(arr); return WF_ERR_ALLOC; }

    map->map.pairs[0].key = cbor_str("e");
    map->map.pairs[0].value = arr;
    map->map.pairs[1].key = cbor_str("l");
    map->map.pairs[1].value = (node->left.len > 0)
        ? cbor_cid(&node->left) : cbor_null();

    size_t cbor_len;
    unsigned char *cbor = wf_cbor_serialize(map, &cbor_len);
    wf_cbor_free(map);
    if (!cbor) return WF_ERR_ALLOC;

    wf_cid cid;
    wf_status s = wf_cid_of_block(cbor, cbor_len, &cid);
    if (s != WF_OK) { free(cbor); return s; }

    /* Content-addressed store: if a block with this CID already exists in the
     * CAR (e.g. a shared MST subtree reused across writes), reuse it rather
     * than appending a duplicate. A CAR with two blocks sharing a CID is
     * malformed and rejected by wf_repo_verify. */
    if (wf_car_find_block(car, &cid)) {
        free(cbor);
        node->cid = cid;
        return WF_OK;
    }

    wf_car_block *new_blocks = realloc(car->blocks,
        (car->block_count + 1) * sizeof(wf_car_block));
    if (!new_blocks) { free(cbor); return WF_ERR_ALLOC; }
    car->blocks = new_blocks;
    car->blocks[car->block_count].cid = cid;
    car->blocks[car->block_count].data = cbor;
    car->blocks[car->block_count].data_len = cbor_len;
    car->block_count++;

    node->cid = cid;
    return WF_OK;
}

static size_t mst_find_ge(const wf_mst_node *node,
                          const unsigned char *key, size_t key_len) {
    for (size_t i = 0; i < node->count; i++) {
        if (wf_mst_key_cmp(key, key_len,
                           node->entries[i].key,
                           node->entries[i].key_len) <= 0)
            return i;
    }
    return node->count;
}

static wf_status mst_load_node_depth(wf_car *car, const wf_cid *cid,
                                     wf_mst_node *out, size_t depth) {
    if (depth > car->block_count) return WF_ERR_PARSE;
    wf_car_block *block = wf_car_find_block(car, cid);
    if (!block) return WF_ERR_PARSE;
    wf_status s = wf_mst_node_parse(block->data, block->data_len, cid, out);
    if (s != WF_OK || out->count > 0 || out->left.len == 0) return s;

    wf_mst_node child = {0};
    s = mst_load_node_depth(car, &out->left, &child, depth + 1);
    if (s == WF_OK) out->layer = child.layer + 1;
    wf_mst_node_free(&child);
    if (s != WF_OK) wf_mst_node_free(out);
    return s;
}

static wf_status mst_load_node(wf_car *car, const wf_cid *cid,
                               wf_mst_node *out) {
    return mst_load_node_depth(car, cid, out, 0);
}

static void free_entries(wf_mst_entry *entries, size_t count) {
    for (size_t i = 0; i < count; i++)
        free(entries[i].key);
}

static wf_status mst_add_at_layer(wf_car *car, const wf_mst_node *node,
                                  const unsigned char *key, size_t key_len,
                                  const wf_cid *value, wf_cid *new_cid) {
    size_t idx = mst_find_ge(node, key, key_len);

    if (idx < node->count) {
        int cmp = wf_mst_key_cmp(key, key_len,
                                 node->entries[idx].key,
                                 node->entries[idx].key_len);
        if (cmp == 0) {
            *new_cid = node->cid;
            return WF_OK;
        }
    }

    size_t new_count = node->count + 1;
    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0, j = 0; i < node->count; i++, j++) {
        if (j == idx) {
            new_entries[j].key = malloc(key_len);
            if (!new_entries[j].key) { free_entries(new_entries, j); free(new_entries); return WF_ERR_ALLOC; }
            memcpy(new_entries[j].key, key, key_len);
            new_entries[j].key_len = key_len;
            new_entries[j].value = *value;
            j++;
        }
        new_entries[j].key = malloc(node->entries[i].key_len);
        if (!new_entries[j].key) { free_entries(new_entries, j); free(new_entries); return WF_ERR_ALLOC; }
        memcpy(new_entries[j].key, node->entries[i].key, node->entries[i].key_len);
        new_entries[j].key_len = node->entries[i].key_len;
        new_entries[j].value = node->entries[i].value;
        new_entries[j].subtree = node->entries[i].subtree;
    }
    if (idx == node->count) {
        new_entries[idx].key = malloc(key_len);
        if (!new_entries[idx].key) { free_entries(new_entries, idx); free(new_entries); return WF_ERR_ALLOC; }
        memcpy(new_entries[idx].key, key, key_len);
        new_entries[idx].key_len = key_len;
        new_entries[idx].value = *value;
    }

    wf_mst_node new_node;
    wf_status s = wf_mst_node_build(node->layer, &node->left,
                                    new_entries, new_count, &new_node);
    free_entries(new_entries, new_count);
    free(new_entries);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }

    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

static wf_status mst_raise_subtree(wf_car *car, wf_cid *cid,
                                   unsigned child_layer,
                                   unsigned parent_layer);

static wf_status mst_add_at_lower_layer(wf_car *car, const wf_mst_node *node,
                                        const unsigned char *key,
                                        size_t key_len, const wf_cid *value,
                                        unsigned key_layer, wf_cid *new_cid) {
    (void)key_layer;
    size_t idx = mst_find_ge(node, key, key_len);

    wf_cid new_subtree;
    wf_status s;

    int use_left = (idx == 0);
    size_t use_prev = (size_t)-1;
    if (idx > 0) {
        use_left = 0;
        use_prev = (idx > node->count) ? node->count - 1 : idx - 1;
    } else if (idx >= node->count && node->count > 0) {
        use_left = 0;
        use_prev = node->count - 1;
    }

    const wf_cid *target_cid = use_left ? &node->left : &node->entries[use_prev].subtree;

    if (target_cid->len > 0) {
        if (use_left) {
            wf_mst_node child;
            s = mst_load_node(car, target_cid, &child);
            if (s != WF_OK) return s;
            s = wf_mst_add(car, target_cid, key, key_len, value, &new_subtree);
            wf_mst_node_free(&child);
        } else {
            s = wf_mst_add(car, target_cid, key, key_len, value, &new_subtree);
        }
    } else {
        wf_cid zero = {{0}, 0};
        s = wf_mst_add(car, &zero, key, key_len, value, &new_subtree);
        if (s == WF_OK)
            s = mst_raise_subtree(car, &new_subtree, key_layer, node->layer);
    }
    if (s != WF_OK) return s;

    size_t new_count = node->count;
    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0; i < new_count; i++) {
        new_entries[i].key = malloc(node->entries[i].key_len);
        if (!new_entries[i].key) { free_entries(new_entries, i); free(new_entries); return WF_ERR_ALLOC; }
        memcpy(new_entries[i].key, node->entries[i].key, node->entries[i].key_len);
        new_entries[i].key_len = node->entries[i].key_len;
        new_entries[i].value = node->entries[i].value;
        new_entries[i].subtree = node->entries[i].subtree;
    }

    wf_mst_node new_node;
    if (use_left) {
        s = wf_mst_node_build(node->layer, &new_subtree,
                              new_entries, new_count, &new_node);
    } else {
        new_entries[use_prev].subtree = new_subtree;
        s = wf_mst_node_build(node->layer, &node->left,
                              new_entries, new_count, &new_node);
    }
    free_entries(new_entries, new_count);
    free(new_entries);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

static wf_status mst_raise_subtree(wf_car *car, wf_cid *cid,
                                   unsigned child_layer,
                                   unsigned parent_layer) {
    if (cid->len == 0) return WF_OK;
    for (unsigned layer = child_layer + 1; layer < parent_layer; layer++) {
        wf_mst_node structural;
        wf_status s = wf_mst_node_build(layer, cid, NULL, 0, &structural);
        if (s != WF_OK) return s;
        s = wf_mst_node_finalize(&structural, car);
        if (s == WF_OK) *cid = structural.cid;
        wf_mst_node_free(&structural);
        if (s != WF_OK) return s;
    }
    return WF_OK;
}

static wf_status mst_add_at_higher_layer(wf_car *car,
                                         const wf_mst_node *node,
                                         const unsigned char *key,
                                         size_t key_len, const wf_cid *value,
                                         unsigned key_layer,
                                         wf_cid *new_cid) {
    size_t idx = mst_find_ge(node, key, key_len);
    size_t left_count = idx;
    size_t right_count = node->count - idx;

    wf_mst_entry *left_entries = NULL;
    wf_mst_entry *right_entries = NULL;

    if (left_count > 0) {
        left_entries = calloc(left_count, sizeof(wf_mst_entry));
        if (!left_entries) return WF_ERR_ALLOC;
        for (size_t i = 0; i < left_count; i++) {
            left_entries[i].key_len = node->entries[i].key_len;
            left_entries[i].key = malloc(left_entries[i].key_len);
            if (!left_entries[i].key) {
                for (size_t j = 0; j < i; j++) free(left_entries[j].key);
                free(left_entries);
                free(right_entries);
                return WF_ERR_ALLOC;
            }
            memcpy(left_entries[i].key, node->entries[i].key, left_entries[i].key_len);
            left_entries[i].value = node->entries[i].value;
            left_entries[i].subtree = node->entries[i].subtree;
        }
    }
    if (right_count > 0) {
        right_entries = calloc(right_count, sizeof(wf_mst_entry));
        if (!right_entries) {
            for (size_t i = 0; i < left_count; i++) free(left_entries[i].key);
            free(left_entries);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < right_count; i++) {
            right_entries[i].key_len = node->entries[idx + i].key_len;
            right_entries[i].key = malloc(right_entries[i].key_len);
            if (!right_entries[i].key) {
                for (size_t j = 0; j < i; j++) free(right_entries[j].key);
                for (size_t j = 0; j < left_count; j++) free(left_entries[j].key);
                free(right_entries);
                free(left_entries);
                return WF_ERR_ALLOC;
            }
            memcpy(right_entries[i].key, node->entries[idx + i].key, right_entries[i].key_len);
            right_entries[i].value = node->entries[idx + i].value;
            right_entries[i].subtree = node->entries[idx + i].subtree;
        }
    }

    wf_cid left_cid = {{0}, 0};
    if (left_count > 0) {
        wf_mst_node left_node;
        wf_status st = wf_mst_node_build(node->layer, &node->left,
                                         left_entries, left_count, &left_node);
        free(left_entries);
        left_entries = NULL;
        if (st != WF_OK) {
            if (right_entries) {
                for (size_t i = 0; i < right_count; i++) free(right_entries[i].key);
                free(right_entries);
            }
            return st;
        }
        st = wf_mst_node_finalize(&left_node, car);
        if (st != WF_OK) {
            wf_mst_node_free(&left_node);
            if (right_entries) {
                for (size_t i = 0; i < right_count; i++) free(right_entries[i].key);
                free(right_entries);
            }
            return st;
        }
        left_cid = left_node.cid;
        wf_mst_node_free(&left_node);
        st = mst_raise_subtree(car, &left_cid, node->layer, key_layer);
        if (st != WF_OK) {
            if (right_entries) {
                for (size_t i = 0; i < right_count; i++) free(right_entries[i].key);
                free(right_entries);
            }
            return st;
        }
    }

    wf_cid right_cid = {{0}, 0};
    if (right_count > 0) {
        wf_mst_node right_node;
        wf_cid right_left = {{0}, 0};
        wf_status st = wf_mst_node_build(node->layer, &right_left,
                                         right_entries, right_count, &right_node);
        free(right_entries);
        right_entries = NULL;
        if (st != WF_OK) return st;
        st = wf_mst_node_finalize(&right_node, car);
        if (st != WF_OK) { wf_mst_node_free(&right_node); return st; }
        right_cid = right_node.cid;
        wf_mst_node_free(&right_node);
        st = mst_raise_subtree(car, &right_cid, node->layer, key_layer);
        if (st != WF_OK) return st;
    }

    wf_mst_entry leaf_entry;
    leaf_entry.key_len = key_len;
    leaf_entry.value = *value;
    leaf_entry.subtree = right_cid;
    leaf_entry.key = malloc(key_len);
    if (!leaf_entry.key) return WF_ERR_ALLOC;
    memcpy(leaf_entry.key, key, key_len);

    wf_mst_entry mid_entries[1];
    mid_entries[0].key = leaf_entry.key;
    mid_entries[0].key_len = leaf_entry.key_len;
    mid_entries[0].value = leaf_entry.value;
    mid_entries[0].subtree = leaf_entry.subtree;

    wf_mst_node root_node;
    wf_status st = wf_mst_node_build(key_layer, &left_cid,
                                     mid_entries, 1, &root_node);
    if (st != WF_OK) { free(leaf_entry.key); return st; }
    st = wf_mst_node_finalize(&root_node, car);
    if (st != WF_OK) { wf_mst_node_free(&root_node); free(leaf_entry.key); return st; }
    *new_cid = root_node.cid;
    wf_mst_node_free(&root_node);
    return WF_OK;
}

wf_status wf_mst_add(wf_car *car, const wf_cid *root_cid,
                     const unsigned char *key, size_t key_len,
                     const wf_cid *value, wf_cid *new_root) {
    if (!car || !root_cid || !key || key_len == 0 || !value || !new_root)
        return WF_ERR_INVALID_ARG;

    unsigned key_layer = wf_mst_key_layer(key, key_len);

    if (root_cid->len == 0) {
        wf_mst_entry entry;
        memset(&entry, 0, sizeof(entry));
        entry.key = malloc(key_len);
        if (!entry.key) return WF_ERR_ALLOC;
        memcpy(entry.key, key, key_len);
        entry.key_len = key_len;
        entry.value = *value;

        wf_cid empty_left = {{0}, 0};
        wf_mst_node node;
        wf_status s = wf_mst_node_build(key_layer, &empty_left,
                                        &entry, 1, &node);
        free(entry.key);
        entry.key = NULL;
        if (s != WF_OK) return s;
        s = wf_mst_node_finalize(&node, car);
        if (s != WF_OK) { wf_mst_node_free(&node); return s; }
        *new_root = node.cid;
        wf_mst_node_free(&node);
        return WF_OK;
    }

    wf_mst_node node;
    wf_status s = mst_load_node(car, root_cid, &node);
    if (s != WF_OK) return s;

    if (key_layer == node.layer) {
        s = mst_add_at_layer(car, &node, key, key_len, value, new_root);
    } else if (key_layer > node.layer) {
        s = mst_add_at_higher_layer(car, &node, key, key_len,
                                    value, key_layer, new_root);
    } else {
        s = mst_add_at_lower_layer(car, &node, key, key_len,
                                   value, key_layer, new_root);
    }

    wf_mst_node_free(&node);
    return s;
}

static wf_status mst_merge_subtrees(wf_car *car, const wf_cid *a,
                                    const wf_cid *b, wf_cid *out) {
    if (a->len == 0) { *out = *b; return WF_OK; }
    if (b->len == 0) { *out = *a; return WF_OK; }

    wf_mst_node left_node, right_node;
    wf_status s = mst_load_node(car, a, &left_node);
    if (s != WF_OK) return s;
    s = mst_load_node(car, b, &right_node);
    if (s != WF_OK) {
        wf_mst_node_free(&left_node);
        return s;
    }
    if (left_node.layer != right_node.layer ||
        (left_node.count > 0 && right_node.count > 0 &&
         wf_mst_key_cmp(left_node.entries[left_node.count - 1].key,
                        left_node.entries[left_node.count - 1].key_len,
                        right_node.entries[0].key,
                        right_node.entries[0].key_len) >= 0)) {
        wf_mst_node_free(&left_node);
        wf_mst_node_free(&right_node);
        return WF_ERR_PARSE;
    }

    size_t count = left_node.count + right_node.count;
    wf_mst_entry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) {
        wf_mst_node_free(&left_node);
        wf_mst_node_free(&right_node);
        return WF_ERR_ALLOC;
    }
    size_t copied = 0;
    for (size_t side = 0; side < 2; side++) {
        const wf_mst_node *node = side == 0 ? &left_node : &right_node;
        for (size_t i = 0; i < node->count; i++, copied++) {
            entries[copied].key = malloc(node->entries[i].key_len);
            if (!entries[copied].key) {
                free_entries(entries, copied);
                free(entries);
                wf_mst_node_free(&left_node);
                wf_mst_node_free(&right_node);
                return WF_ERR_ALLOC;
            }
            memcpy(entries[copied].key, node->entries[i].key,
                   node->entries[i].key_len);
            entries[copied].key_len = node->entries[i].key_len;
            entries[copied].value = node->entries[i].value;
            entries[copied].subtree = node->entries[i].subtree;
        }
    }

    wf_cid new_left = left_node.left;
    wf_cid boundary;
    if (left_node.count > 0) {
        s = mst_merge_subtrees(car,
                               &left_node.entries[left_node.count - 1].subtree,
                               &right_node.left, &boundary);
        if (s == WF_OK) entries[left_node.count - 1].subtree = boundary;
    } else {
        s = mst_merge_subtrees(car, &left_node.left, &right_node.left,
                               &new_left);
    }

    wf_mst_node merged_node;
    if (s == WF_OK)
        s = wf_mst_node_build(left_node.layer, &new_left, entries, count,
                              &merged_node);
    free_entries(entries, count);
    free(entries);
    wf_mst_node_free(&left_node);
    wf_mst_node_free(&right_node);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&merged_node, car);
    if (s == WF_OK) *out = merged_node.cid;
    wf_mst_node_free(&merged_node);
    return s;
}

static wf_status mst_delete_at_layer(wf_car *car, const wf_mst_node *node,
                                     const unsigned char *key, size_t key_len,
                                     wf_cid *new_cid) {
    size_t idx = mst_find_ge(node, key, key_len);
    if (idx >= node->count) {
        *new_cid = node->cid;
        return WF_OK;
    }
    int cmp = wf_mst_key_cmp(key, key_len,
                             node->entries[idx].key,
                             node->entries[idx].key_len);
    if (cmp != 0) {
        *new_cid = node->cid;
        return WF_OK;
    }

    size_t new_count = node->count - 1;

    wf_cid left_subtree, right_subtree;
    if (idx == 0) {
        left_subtree = node->left;
        right_subtree = node->entries[0].subtree;
    } else {
        left_subtree = node->entries[idx - 1].subtree;
        right_subtree = node->entries[idx].subtree;
    }

    wf_cid merged;
    wf_status s = mst_merge_subtrees(car, &left_subtree, &right_subtree,
                                     &merged);
    if (s != WF_OK) return s;

    if (new_count == 0) {
        *new_cid = merged;
        return WF_OK;
    }

    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0, j = 0; i < node->count; i++) {
        if (i == idx) continue;
        new_entries[j].key_len = node->entries[i].key_len;
        new_entries[j].key = malloc(new_entries[j].key_len);
        if (!new_entries[j].key) {
            free_entries(new_entries, j);
            free(new_entries);
            return WF_ERR_ALLOC;
        }
        memcpy(new_entries[j].key, node->entries[i].key, new_entries[j].key_len);
        new_entries[j].value = node->entries[i].value;
        new_entries[j].subtree = node->entries[i].subtree;
        j++;
    }

    wf_cid new_left = node->left;
    if (idx == 0) {
        new_left = merged;
    } else {
        new_entries[idx - 1].subtree = merged;
    }

    wf_mst_node new_node;
    s = wf_mst_node_build(node->layer, &new_left,
                          new_entries, new_count, &new_node);
    free_entries(new_entries, new_count);
    free(new_entries);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

static wf_status mst_delete_recursive(wf_car *car, const wf_mst_node *node,
                                      const unsigned char *key, size_t key_len,
                                      unsigned key_layer, wf_cid *new_cid) {
    wf_status s;

    if (key_layer == node->layer) {
        size_t idx = mst_find_ge(node, key, key_len);
        int found_here = 0;
        if (idx < node->count) {
            int cmp = wf_mst_key_cmp(key, key_len,
                                     node->entries[idx].key,
                                     node->entries[idx].key_len);
            found_here = (cmp == 0);
        }

        if (found_here) {
            return mst_delete_at_layer(car, node, key, key_len, new_cid);
        }

        wf_cid subtree;
        int use_left = 0;
        size_t sub_idx = 0;

        if (idx == 0) {
            if (node->left.len > 0) {
                use_left = 1;
            } else {
                memset(new_cid, 0, sizeof(*new_cid));
                return WF_ERR_NOT_FOUND;
            }
        } else {
            sub_idx = (idx >= node->count) ? node->count - 1 : idx - 1;
            if (node->entries[sub_idx].subtree.len == 0) {
                memset(new_cid, 0, sizeof(*new_cid));
                return WF_ERR_NOT_FOUND;
            }
        }

        if (use_left) {
            wf_mst_node child;
            s = mst_load_node(car, &node->left, &child);
            if (s != WF_OK) return s;
            s = mst_delete_recursive(car, &child, key, key_len, key_layer, &subtree);
            wf_mst_node_free(&child);
        } else {
            wf_mst_node sub_node;
            s = mst_load_node(car, &node->entries[sub_idx].subtree, &sub_node);
            if (s != WF_OK) return s;
            s = mst_delete_recursive(car, &sub_node, key, key_len, key_layer, &subtree);
            wf_mst_node_free(&sub_node);
        }
        if (s != WF_OK) return s;

        size_t new_count = node->count;
        wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
        if (!new_entries) return WF_ERR_ALLOC;

        for (size_t i = 0; i < new_count; i++) {
            new_entries[i].key_len = node->entries[i].key_len;
            new_entries[i].key = malloc(new_entries[i].key_len);
            if (!new_entries[i].key) {
                free_entries(new_entries, i);
                free(new_entries);
                return WF_ERR_ALLOC;
            }
            memcpy(new_entries[i].key, node->entries[i].key, new_entries[i].key_len);
            new_entries[i].value = node->entries[i].value;
            new_entries[i].subtree = node->entries[i].subtree;
        }

        wf_mst_node new_node;
        if (use_left) {
            s = wf_mst_node_build(node->layer, &subtree,
                                  new_entries, new_count, &new_node);
        } else {
            new_entries[sub_idx].subtree = subtree;
            s = wf_mst_node_build(node->layer, &node->left,
                                  new_entries, new_count, &new_node);
        }
        free_entries(new_entries, new_count);
        free(new_entries);
        if (s != WF_OK) return s;

        s = wf_mst_node_finalize(&new_node, car);
        if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
        *new_cid = new_node.cid;
        wf_mst_node_free(&new_node);
        return WF_OK;
    }

    if (key_layer > node->layer) {
        memset(new_cid, 0, sizeof(*new_cid));
        return WF_ERR_NOT_FOUND;
    }

    size_t idx = mst_find_ge(node, key, key_len);
    int use_left = (idx == 0);
    size_t use_prev = (size_t)-1;
    if (idx > 0) {
        use_prev = (idx > node->count) ? node->count - 1 : idx - 1;
        use_left = 0;
    } else if (idx >= node->count && node->count > 0) {
        use_prev = node->count - 1;
        use_left = 0;
    }

    const wf_cid *target = use_left ? &node->left
                                    : &node->entries[use_prev].subtree;
    if (target->len == 0) {
        memset(new_cid, 0, sizeof(*new_cid));
        return WF_ERR_NOT_FOUND;
    }

    {
        wf_mst_node sub_node;
        s = mst_load_node(car, target, &sub_node);
        if (s != WF_OK) return s;
        s = mst_delete_recursive(car, &sub_node, key, key_len, key_layer, new_cid);
        wf_mst_node_free(&sub_node);
    }
    if (s != WF_OK) return s;

    size_t new_count = node->count;
    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0; i < new_count; i++) {
        new_entries[i].key_len = node->entries[i].key_len;
        new_entries[i].key = malloc(new_entries[i].key_len);
        if (!new_entries[i].key) {
            free_entries(new_entries, i);
            free(new_entries);
            return WF_ERR_ALLOC;
        }
        memcpy(new_entries[i].key, node->entries[i].key, new_entries[i].key_len);
        new_entries[i].value = node->entries[i].value;
        new_entries[i].subtree = node->entries[i].subtree;
    }

    wf_mst_node new_node;
    if (use_left) {
        s = wf_mst_node_build(node->layer, new_cid,
                              new_entries, new_count, &new_node);
    } else {
        new_entries[use_prev].subtree = *new_cid;
        s = wf_mst_node_build(node->layer, &node->left,
                              new_entries, new_count, &new_node);
    }
    free_entries(new_entries, new_count);
    free(new_entries);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

wf_status wf_mst_delete(wf_car *car, const wf_cid *root_cid,
                        const unsigned char *key, size_t key_len,
                        wf_cid *new_root) {
    if (!car || !root_cid || !key || key_len == 0 || !new_root)
        return WF_ERR_INVALID_ARG;

    memset(new_root, 0, sizeof(*new_root));

    if (root_cid->len == 0)
        return WF_ERR_NOT_FOUND;

    unsigned key_layer = wf_mst_key_layer(key, key_len);

    wf_mst_node node;
    wf_status s = mst_load_node(car, root_cid, &node);
    if (s != WF_OK) return s;

    s = mst_delete_recursive(car, &node, key, key_len, key_layer, new_root);
    wf_mst_node_free(&node);
    if (s != WF_OK) return s;

    while (new_root->len > 0) {
        wf_mst_node top;
        s = mst_load_node(car, new_root, &top);
        if (s != WF_OK) return s;
        if (top.count != 0) {
            wf_mst_node_free(&top);
            break;
        }
        *new_root = top.left;
        wf_mst_node_free(&top);
    }
    return s;
}
