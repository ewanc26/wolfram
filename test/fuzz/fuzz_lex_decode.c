/*
 * fuzz_lex_decode.c — the generated lexicon output decoders
 * (wf_lex_*_decode_json), which every generated XRPC wrapper runs on a
 * server's response body.
 *
 * Input: "<decoder>\n<json>", where <decoder> is the C name between `wf_lex_`
 * and `_decode_json`, e.g. app_bsky_feed_get_timeline_main_output. A name
 * that matches no decoder (what most mutations of it produce) selects one by
 * hash, so every byte string still reaches some decoder. The table is
 * generated from atproto_lex.h at configure time.
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/atproto_lex.h"

typedef void (*wf_fuzz_lex_fn)(const char *json, size_t len);

typedef struct wf_fuzz_lex_entry {
    const char *name;
    wf_fuzz_lex_fn run;
} wf_fuzz_lex_entry;

#define WF_FUZZ_LEX(n)                                                         \
    static void wf_fuzz_lex_##n(const char *json, size_t len) {                \
        wf_lex_##n *value = NULL;                                              \
        if (wf_lex_##n##_decode_json(json, len, &value) == WF_OK)              \
            wf_lex_##n##_free(value);                                          \
    }
#include "fuzz_lex_table.inc"
#undef WF_FUZZ_LEX

#define WF_FUZZ_LEX(n) {#n, wf_fuzz_lex_##n},
static const wf_fuzz_lex_entry wf_fuzz_lex_table[] = {
#include "fuzz_lex_table.inc"
};
#undef WF_FUZZ_LEX

#define WF_FUZZ_LEX_COUNT                                                      \
    (sizeof(wf_fuzz_lex_table) / sizeof(wf_fuzz_lex_table[0]))

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *text = (const char *)data;
    const char *nl = memchr(text, '\n', size < 160 ? size : 160);
    size_t name_len = nl ? (size_t)(nl - text) : 0;
    const char *json = nl ? nl + 1 : text;
    size_t json_len = nl ? size - name_len - 1 : size;

    const wf_fuzz_lex_entry *entry = NULL;
    for (size_t i = 0; nl && i < WF_FUZZ_LEX_COUNT; i++) {
        if (strlen(wf_fuzz_lex_table[i].name) == name_len &&
            memcmp(wf_fuzz_lex_table[i].name, text, name_len) == 0) {
            entry = &wf_fuzz_lex_table[i];
            break;
        }
    }
    if (!entry) {
        uint32_t h = 2166136261u; /* FNV-1a over the selector bytes */
        for (size_t i = 0; i < name_len; i++)
            h = (h ^ (uint8_t)text[i]) * 16777619u;
        entry = &wf_fuzz_lex_table[h % WF_FUZZ_LEX_COUNT];
    }
    entry->run(json, json_len);
    return 0;
}
