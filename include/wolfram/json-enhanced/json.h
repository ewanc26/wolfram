#ifndef WOLFRAM_JSON_ENHANCED_H
#define WOLFRAM_JSON_ENHANCED_H

#include "wolfram/json.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Enhanced JSON-Schema validation with extended keyword support */

/* Extended JSON-Schema validation with additional keywords */
wf_status wf_json_validate_enhanced(const char *schema_json, size_t schema_len,
                                     const char *doc_json, size_t doc_len,
                                     char **out_error);

/* Format validation with support for common format patterns */
wf_status wf_json_validate_formats(const char *schema_json, size_t schema_len,
                                   const char *doc_json, size_t doc_len,
                                   char **out_error);

/* Validation with enum support */
wf_status wf_json_validate_enums(const char *schema_json, size_t schema_len,
                                const char *doc_json, size_t doc_len,
                                char **out_error);

/* Validation with number range constraints */
wf_status wf_json_validate_number_ranges(const char *schema_json, size_t schema_len,
                                        const char *doc_json, size_t doc_len,
                                        char **out_error);

/* Validation with string pattern matching */
wf_status wf_json_validate_patterns(const char *schema_json, size_t schema_len,
                                   const char *doc_json, size_t doc_len,
                                   char **out_error);

/* Validation with additionalProperties control */
wf_status wf_json_validate_additional_props(const char *schema_json, size_t schema_len,
                                           const char *doc_json, size_t doc_len,
                                           char **out_error);

/* Validation with union types (anyOf/oneOf) */
wf_status wf_json_validate_unions(const char *schema_json, size_t schema_len,
                                 const char *doc_json, size_t doc_len,
                                 char **out_error);

/* CID validation for JSON values */
wf_status wf_json_validate_cid_links(const char *schema_json, size_t schema_len,
                                     const char *doc_json, size_t doc_len,
                                     char **out_error);

/* Bytes validation for binary data */
wf_status wf_json_validate_bytes(const char *schema_json, size_t schema_len,
                               const char *doc_json, size_t doc_len,
                               char **out_error);

/* Format validation types */
typedef enum {
    WF_FORMAT_NONE,
    WF_FORMAT_DATE_TIME_RFC3339,
    WF_FORMAT_EMAIL,
    WF_FORMAT_URI,
    WF_FORMAT_HANDLE,
    WF_FORMAT_CID,
    WF_FORMAT_SCHEMA,
    WF_FORMAT_NUMBER_FORMAT,
    WF_FORMAT_STRING_FORMAT,
    WF_FORMAT_ARRAY_FORMAT,
    WF_FORMAT_OBJECT_FORMAT
} wf_json_format;

/* Number format types */
typedef enum {
    WF_NUMBER_FORM_AUTO,
    WF_NUMBER_FORM_INTEGER,
    WF_NUMBER_FORM_NUMBER,
    WF_NUMBER_FORM_FLAT,
    WF_NUMBER_FORM_SCIENTIFIC
} wf_json_number_format;

/* String format types */
typedef enum {
    WF_STRING_FORM_UTF8,
    WF_STRING_FORM_UTF16,
    WF_STRING_FORM_ASCII,
    WF_STRING_FORM_URL,
    WF_STRING_FORM_EMAIL,
    WF_STRING_FORM_HANDLE,
    WF_STRING_FORM_CID,
    WF_STRING_FORM_BASE64,
    WF_STRING_FORM_BASE32
} wf_json_string_format;

/* Validation options */
typedef struct {
    int validate_formats;
    int validate_enums;
    int validate_numbers;
    int validate_patterns;
    int validate_additional_props;
    int validate_unions;
    int validate_cid_links;
    int validate_bytes;
    int strict_mode;
    const char *format_whitelist[16];
    const char *default_formats[16];
} wf_json_validation_options;

/* Initialize validation options with defaults */
void wf_json_validation_options_init(wf_json_validation_options *options);

/* Enhanced validation with custom options */
wf_status wf_json_validate_with_options(const char *schema_json, size_t schema_len,
                                         const char *doc_json, size_t doc_len,
                                         const wf_json_validation_options *options,
                                         char **out_error);

/* Sorted canonical JSON (option to sort object keys) */
wf_status wf_json_canonicalize_sorted(const char *in, size_t len, char **out,
                                    int sort_objects);

/* Lex integration functions */
wf_status wf_json_to_lex(const char *json, size_t len, char **out_lex);
wf_status wf_lex_to_json(const char *lex, size_t len, char **out_json);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_JSON_ENHANCED_H */