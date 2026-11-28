/*
 * fuzz_chat_mod_event.c — one JSON chat moderation event
 * (wf_agent_parse_mod_event).
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/chat_typed.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_chat_mod_event ev;
    memset(&ev, 0, sizeof(ev));
    if (wf_agent_parse_mod_event((const char *)data, size, &ev) != WF_OK)
        return 0;
    wf_chat_mod_event_free(&ev);
    return 0;
}
