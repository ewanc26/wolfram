/*
 * fuzz_firehose_frame.c — one com.atproto.sync.subscribeRepos frame
 * (wf_subscribe_decode_frame): a DAG-CBOR header map followed by a body.
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/sync_subscribe.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_subscribe_event ev;
    memset(&ev, 0, sizeof(ev));
    if (wf_subscribe_decode_frame(data, size, &ev) != WF_OK) return 0;
    wf_subscribe_event_free(&ev);
    return 0;
}
