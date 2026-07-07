/**
 * lexcall.h — generic typed-output decode registry for generated lexicon
 * responses.
 *
 * The `atproto_lex.h` header exposes, for each generated NSID, an owning
 * output decoder of the form:
 *
 *   wf_status wf_lex_<ns>_main_output_decode_json(
 *       const char *json, size_t length,
 *       wf_lex_<ns>_main_output **out_value);
 *   void wf_lex_<ns>_main_output_free(wf_lex_<ns>_main_output *value);
 *
 * This module wraps those behind a single string-keyed registry so a caller
 * can decode ANY registered NSID's JSON response into its typed struct with
 * one call, without knowing the generated type name.
 */

#ifndef WOLFRAM_LEXCALL_H
#define WOLFRAM_LEXCALL_H

#include <stddef.h>

#include "wolfram/xrpc.h"

/**
 * Decode a JSON response body into an owning, NSID-specific typed struct.
 *
 * `json`/`length` are the raw response bytes; `out` receives a heap pointer
 * to the decoded struct. The generated decoder allocates the struct and
 * assigns `*out` on success; the caller releases it with wf_lex_output_free.
 */
typedef wf_status (*wf_lex_decode_fn)(const char *json, size_t length,
                                      void **out);

/** Free a previously decoded, NSID-specific typed struct. */
typedef void (*wf_lex_free_fn)(void *);

/** A single registry entry binding an NSID to its generated decoder/free. */
typedef struct wf_lex_entry {
    const char *nsid;
    wf_lex_decode_fn decode;
    wf_lex_free_fn free;
    size_t size;
} wf_lex_entry;

/**
 * Decode `json` (of `json_len` bytes) for the given `nsid` into a typed
 * struct and store the pointer at `*out`.
 *
 * Returns:
 *   WF_ERR_INVALID_ARG  if any argument is NULL.
 *   WF_ERR_NOT_FOUND    if `nsid` is not in the registry.
 *   otherwise the status returned by the generated decoder (e.g.
 *   WF_ERR_PARSE for malformed JSON, WF_OK on success).
 *
 * On any failure `*out` is left NULL. The caller must release a successful
 * result with wf_lex_output_free(nsid, *out).
 */
wf_status wf_lex_decode_output(const char *nsid, const char *json,
                               size_t json_len, void **out);

/** Convenience alias for wf_lex_decode_output. */
wf_status wf_lex_call_output(const char *nsid, const char *json,
                             size_t json_len, void **out);

/**
 * Free a typed output previously produced by wf_lex_decode_output for
 * `nsid`. No-op if `nsid` is unknown or `out` is NULL.
 */
void wf_lex_output_free(const char *nsid, void *out);

#endif /* WOLFRAM_LEXCALL_H */
