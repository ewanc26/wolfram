#ifndef WOLFRAM_REPO_CBOR_BUILD_H
#define WOLFRAM_REPO_CBOR_BUILD_H

#include "wolfram/repo/cbor.h"
#include "wolfram/repo/cid.h"

wf_cbor_item *cbor_str(const char *s);
wf_cbor_item *cbor_bytes(const unsigned char *data, size_t len);
wf_cbor_item *cbor_uint(uint64_t v);
wf_cbor_item *cbor_null(void);
wf_cbor_item *cbor_cid(const wf_cid *cid);

#endif /* WOLFRAM_REPO_CBOR_BUILD_H */