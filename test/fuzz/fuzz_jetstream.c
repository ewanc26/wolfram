/*
 * fuzz_jetstream.c — one uncompressed Jetstream message, through the legacy,
 * proposal-0015 v2 and typed parsers.
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/jetstream.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *json = (const char *)data;
    wf_jetstream_event ev;
    memset(&ev, 0, sizeof(ev));
    if (wf_jetstream_event_parse(json, size, &ev) == WF_OK)
        wf_jetstream_event_free(&ev);

    memset(&ev, 0, sizeof(ev));
    if (wf_jetstream_event_parse_v2(json, size, &ev) == WF_OK)
        wf_jetstream_event_free(&ev);

    wf_jetstream_event_typed typed;
    memset(&typed, 0, sizeof(typed));
    if (wf_jetstream_event_parse_typed(json, size, &typed) == WF_OK)
        wf_jetstream_event_typed_free(&typed);
    return 0;
}
