/*
 * fuzz_chat_mod_frame.c — one chat.bsky.moderation.subscribeModEvents frame
 * (wf_chat_mod_frame_parse_cbor).
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/chat_typed.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_chat_mod_frame frame;
    memset(&frame, 0, sizeof(frame));
    if (wf_chat_mod_frame_parse_cbor(data, size, &frame) != WF_OK) return 0;
    wf_chat_mod_frame_free(&frame);
    return 0;
}
