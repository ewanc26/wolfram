#ifndef WOLFRAM_JSON_H
#define WOLFRAM_JSON_H

#include "wolfram/xrpc.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic, non-Lexicon JSON utilities built on cJSON.
 *
 * These helpers are for arbitrary application JSON (config blobs, ad-hoc
 * payloads) — they are NOT a Lexicon validator. For Lexicon schema validation
 * use the validate module.
 */

/*
 * Parse `in` (length `len`, may contain embedded NULs) and re-serialize it
 * with cJSON_PrintUnformatted into a freshly heap-allocated, NUL-terminated
 * string returned via `*out`. The caller owns `*out` and must free() it.
 *
 * This is a stable round-trip: it normalizes insignificant whitespace and
 * number formatting but does NOT reorder or sort object keys. Key order is
 * preserved (insertion order) — this is a round-trip, not a sorted canonical
 * form. Two semantically-equal documents can therefore produce different
 * output if their key order differs.
 *
 * Returns WF_OK on success and sets *out to the owned string.
 * Returns WF_ERR_PARSE if `in` is not valid JSON and leaves *out untouched.
 */
wf_status wf_json_canonicalize(const char *in, size_t len, char **out);

/*
 * Validate `doc` (length `doc_len`) against a JSON-Schema SUBSET described by
 * `schema` (length `schema_len`).
 *
 * Supported keywords (unknown keywords are ignored):
 *   type        : one of "object","array","string","number","boolean","null".
 *   required    : array of property keys that must be present when
 *                 type=="object".
 *   properties  : object mapping key -> sub-schema; each present key is
 *                 validated recursively against its sub-schema.
 *   items       : sub-schema applied to every element when type=="array".
 *   enum        : value must equal one listed value (type-aware).
 *   const       : value must equal the given const.
 *   format      : "date-time","email","uri","hostname" basic checks; unknown
 *                 formats are ignored.
 *   minimum/maximum/exclusiveMinimum/exclusiveMaximum/multipleOf : numeric.
 *   minLength/maxLength : string byte-length bounds.
 *   pattern     : POSIX ERE match (invalid pattern is a schema error).
 *   minItems/maxItems/uniqueItems : array constraints.
 *   additionalProperties : false rejects extra keys; a schema validates them.
 *   anyOf/oneOf/not : subschema combinators.
 *
 * On the first failure, sets *out_error to a freshly heap-allocated,
 * human-readable, NUL-terminated message (caller free()s it) and returns a
 * non-OK wf_status (WF_ERR_INVALID_ARG). On success returns WF_OK and sets
 * *out_error to NULL.
 */
wf_status wf_json_validate(const char *schema_json, size_t schema_len,
                           const char *doc_json, size_t doc_len,
                           char **out_error);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_JSON_H */
