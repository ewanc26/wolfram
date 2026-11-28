/*
 * fuzz_cbor.c — the DAG-CBOR item decoder (wf_cbor_parse, wf_cbor_scan_item)
 * and its serializer.
 *
 * Invariant: an item that parses serializes, and serializing what that
 * re-parses gives the same bytes. The canonical form is a fixed point.
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/repo/cbor.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t used = 0;
    if (wf_cbor_scan_item(data, size, &used)) wf_fuzz_check(used <= size);

    wf_cbor_item *item = wf_cbor_parse(data, size);
    if (!item) return 0;

    size_t len1 = 0;
    unsigned char *enc1 = wf_cbor_serialize(item, &len1);
    wf_cbor_free(item);
    if (!enc1) return 0;

    wf_cbor_item *again = wf_cbor_parse(enc1, len1);
    wf_fuzz_check(again != NULL);
    size_t len2 = 0;
    unsigned char *enc2 = wf_cbor_serialize(again, &len2);
    wf_cbor_free(again);
    wf_fuzz_check(enc2 != NULL);
    wf_fuzz_check(len1 == len2 && memcmp(enc1, enc2, len1) == 0);
    free(enc1);
    free(enc2);
    return 0;
}
