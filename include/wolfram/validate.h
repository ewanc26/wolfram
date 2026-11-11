#ifndef WOLFRAM_VALIDATE_H
#define WOLFRAM_VALIDATE_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Validation result */
typedef struct wf_validate_error {
    char *path;    /* JSON path to the invalid field (e.g., "record/text") */
    char *message; /* human-readable error message */
    struct wf_validate_error *next; /* linked list */
} wf_validate_error;

typedef struct wf_validate_result {
    int success;               /* 1 if valid, 0 if invalid */
    wf_validate_error *errors; /* linked list of errors (NULL if success) */
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

/* Recursively load every *.json lexicon document under a directory. Returns
 * WF_OK if at least one lexicon loaded; malformed or non-lexicon JSON files
 * are skipped (not fatal). The registry owns every loaded document; free it
 * all with wf_lexicon_registry_free. */
wf_status wf_lexicon_registry_load_dir(wf_lexicon_registry *registry,
                                       const char *dir);

/* Is `lexicon_id` loaded in the registry? Lets a caller distinguish "no schema
 * to check against" from "checked and invalid", which a PDS needs in order to
 * report validationStatus `unknown` rather than rejecting the write. */
int wf_lexicon_registry_contains(const wf_lexicon_registry *registry,
                                 const char *lexicon_id);

/* The raw vendored lexicon document for `lexicon_id`, as loaded: WF_OK and the
 * registry-owned `json`/`json_len` out-params (valid until the registry is
 * freed or the document is reloaded), WF_ERR_NOT_FOUND when the NSID is not in
 * the registry, WF_ERR_INVALID_ARG on NULL inputs. Pure lookup — safe on any
 * target the registry itself is built on. */
wf_status wf_lexicon_registry_document(const wf_lexicon_registry *registry,
                                       const char *lexicon_id,
                                       const char **json, size_t *json_len);

/* Resolve `nsid` against the vendored lexicon corpus (the `lexicons/` tree).
 * `corpus_dir` is the corpus root; NULL means $WF_ATPROTO_LEXICONS, falling
 * back to the compile-time WF_ATPROTO_LEXICONS define. On success `out_json`
 * is an owned copy of the document (free with free()) and `out_len` its
 * length. Unknown NSIDs return WF_ERR_NOT_FOUND; an unusable corpus directory
 * returns WF_ERR_INVALID_ARG, so the two are distinguishable; NULL arguments
 * are WF_ERR_INVALID_ARG. Host-only: directory loading needs POSIX, so console
 * targets load documents from embedded strings via wf_lexicon_registry_load
 * and read them back with wf_lexicon_registry_document. */
wf_status wf_lexicon_resolve(const char *nsid, const char *corpus_dir,
                             char **out_json, size_t *out_len);

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
