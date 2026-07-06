#ifndef WOLFRAM_VALIDATE_H
#define WOLFRAM_VALIDATE_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Validation result */
typedef struct wf_validate_error {
    char *path;       /* JSON path to the invalid field (e.g., "record/text") */
    char *message;    /* human-readable error message */
    struct wf_validate_error *next;  /* linked list */
} wf_validate_error;

typedef struct wf_validate_result {
    int success;      /* 1 if valid, 0 if invalid */
    wf_validate_error *errors;  /* linked list of errors (NULL if success) */
} wf_validate_result;

void wf_validate_result_free(wf_validate_result *result);

/* Lexicon schema representation */
typedef struct wf_lexicon_schema wf_lexicon_schema;
typedef struct wf_lexicon_registry wf_lexicon_registry;

/* Create a registry from lexicon JSON files */
wf_lexicon_registry *wf_lexicon_registry_new(void);
void wf_lexicon_registry_free(wf_lexicon_registry *registry);

/* Load a lexicon definition from JSON string */
wf_status wf_lexicon_registry_load(wf_lexicon_registry *registry,
                                   const char *json, size_t json_len);

/* Validate a JSON value against a lexicon record definition */
wf_validate_result wf_validate_record(const wf_lexicon_registry *registry,
                                     const char *lexicon_id,
                                     const char *record_json, size_t json_len);

/* Validate a JSON value against a specific definition in a lexicon */
wf_validate_result wf_validate_value(const wf_lexicon_registry *registry,
                                    const char *lexicon_id, const char *def_id,
                                    const char *json, size_t json_len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_VALIDATE_H */