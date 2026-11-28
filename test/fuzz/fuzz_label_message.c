/*
 * fuzz_label_message.c — one JSON subscribeLabels message
 * (wf_label_message_parse).
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/label.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_label_message msg;
    memset(&msg, 0, sizeof(msg));
    if (wf_label_message_parse((const char *)data, size, &msg) != WF_OK)
        return 0;
    wf_label_message_free(&msg);
    return 0;
}
