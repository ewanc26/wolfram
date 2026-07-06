#ifndef WOLFRAM_REPO_CBOR_MAP_FIND_H
#define WOLFRAM_REPO_CBOR_MAP_FIND_H

#include <stddef.h>
#include "wolfram/repo/cbor.h"

wf_cbor_item *wf_cbor_map_find(wf_cbor_item *item,
                               const char *key, size_t key_len);

#endif /* WOLFRAM_REPO_CBOR_MAP_FIND_H */