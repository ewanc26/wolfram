#include "wolfram/spaces/lthash.h"

#include <stdlib.h>
#include <string.h>

#include <blake3.h>

#include "wolfram/crypto.h"
#include "wolfram/syntax.h"

void wf_space_lthash_init(wf_space_lthash *hash) {
    if (hash) {
        memset(hash->state, 0, sizeof(hash->state));
    }
}

int wf_space_lthash_is_empty(const wf_space_lthash *hash) {
    size_t i;

    if (!hash) {
        return 0;
    }
    for (i = 0; i < sizeof(hash->state); i++) {
        if (hash->state[i]) {
            return 0;
        }
    }
    return 1;
}

/* Add (sign 1) or subtract (sign -1) the element's lanes. Lanes are assembled
 * byte by byte, so the host's endianness does not matter. */
static void combine(wf_space_lthash *hash, const void *element, size_t len,
                    int sign) {
    unsigned char lanes[WF_SPACE_LTHASH_BYTES];
    blake3_hasher hasher;
    size_t i;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, element, len);
    blake3_hasher_finalize(&hasher, lanes, sizeof(lanes));

    for (i = 0; i < sizeof(lanes); i += 2) {
        unsigned a = hash->state[i] | ((unsigned)hash->state[i + 1] << 8);
        unsigned b = lanes[i] | ((unsigned)lanes[i + 1] << 8);
        unsigned sum = sign > 0 ? a + b : a - b;
        hash->state[i] = (unsigned char)(sum & 0xffu);
        hash->state[i + 1] = (unsigned char)((sum >> 8) & 0xffu);
    }
}

wf_status wf_space_lthash_add(wf_space_lthash *hash, const void *element,
                              size_t len) {
    if (!hash || !element || len == 0) {
        return WF_ERR_INVALID_ARG;
    }
    combine(hash, element, len, 1);
    return WF_OK;
}

wf_status wf_space_lthash_remove(wf_space_lthash *hash, const void *element,
                                 size_t len) {
    if (!hash || !element || len == 0) {
        return WF_ERR_INVALID_ARG;
    }
    combine(hash, element, len, -1);
    return WF_OK;
}

wf_status wf_space_lthash_digest(const wf_space_lthash *hash,
                                 unsigned char out[WF_SPACE_DIGEST_BYTES]) {
    if (!hash || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_crypto_sha256(hash->state, sizeof(hash->state), out);
}

/* "{collection}/{rkey}/{cid}", or NULL on allocation failure. */
static char *record_element(const char *collection, const char *rkey,
                            const char *cid) {
    size_t a = strlen(collection);
    size_t b = strlen(rkey);
    size_t c = strlen(cid);
    char *s = malloc(a + b + c + 3);

    if (s) {
        memcpy(s, collection, a);
        s[a] = '/';
        memcpy(s + a + 1, rkey, b);
        s[a + b + 1] = '/';
        memcpy(s + a + b + 2, cid, c + 1);
    }
    return s;
}

static int cid_text_ok(const char *cid) {
    return cid[0] != '\0' && strchr(cid, '/') == NULL;
}

wf_status wf_space_lthash_apply(wf_space_lthash *hash, const char *collection,
                                const char *rkey, const char *prev,
                                const char *cid) {
    char *removed = NULL;
    char *added = NULL;
    wf_status status = WF_OK;

    if (!hash || !collection || !rkey || (!prev && !cid) ||
        !wf_syntax_nsid_is_valid(collection) ||
        !wf_syntax_record_key_is_valid(rkey) || (prev && !cid_text_ok(prev)) ||
        (cid && !cid_text_ok(cid))) {
        return WF_ERR_INVALID_ARG;
    }
    /* Build both elements first, so a failure leaves the state alone. */
    if (prev) {
        removed = record_element(collection, rkey, prev);
    }
    if (cid) {
        added = record_element(collection, rkey, cid);
    }
    if ((prev && !removed) || (cid && !added)) {
        status = WF_ERR_ALLOC;
    } else {
        if (removed) {
            combine(hash, removed, strlen(removed), -1);
        }
        if (added) {
            combine(hash, added, strlen(added), 1);
        }
    }
    free(removed);
    free(added);
    return status;
}
