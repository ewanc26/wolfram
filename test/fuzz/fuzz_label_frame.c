/*
 * fuzz_label_frame.c — one binary com.atproto.label.subscribeLabels frame
 * (wf_label_frame_parse_cbor).
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/label.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_label_message msg;
    memset(&msg, 0, sizeof(msg));
    if (wf_label_frame_parse_cbor(data, size, &msg) != WF_OK) return 0;
    wf_label_message_free(&msg);
    return 0;
}
