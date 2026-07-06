#ifndef WOLFRAM_SYNTAX_H
#define WOLFRAM_SYNTAX_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

int wf_syntax_did_is_valid(const char *did);
int wf_syntax_handle_is_valid(const char *handle);
int wf_syntax_at_identifier_is_valid(const char *id);
int wf_syntax_nsid_is_valid(const char *nsid);
int wf_syntax_record_key_is_valid(const char *key);
int wf_syntax_tid_is_valid(const char *tid);

typedef struct wf_syntax_aturi {
    const char *authority;
    const char *collection;
    const char *record_key;
    const char *fragment;
    char *alloc;
} wf_syntax_aturi;

int wf_syntax_aturi_parse(const char *aturi, wf_syntax_aturi *parsed);
void wf_syntax_aturi_free(wf_syntax_aturi *parsed);
int wf_syntax_datetime_is_valid(const char *datetime);
int wf_syntax_language_is_valid(const char *language);

wf_status wf_syntax_did_validate(const char *did);
wf_status wf_syntax_handle_validate(const char *handle);
wf_status wf_syntax_nsid_validate(const char *nsid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SYNTAX_H */
