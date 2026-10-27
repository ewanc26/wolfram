#ifndef WOLFRAM_UTIL_H
#define WOLFRAM_UTIL_H

#include "wolfram/agent.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Duplicate a string span (does not require null‑termination) */
char *wf_dup_span(const char *s, size_t len);

/* Duplicate `s`: NULL gives NULL, and so does a failed allocation. Use
 * wf_str_set where the two must be told apart. */
char *wf_str_dup(const char *s);

/* Replace *dst with a copy of `src`, freeing the old value. A NULL `src` clears
 * it. WF_ERR_ALLOC, leaving *dst as it was, when the copy fails. */
wf_status wf_str_set(char **dst, const char *src);

/* Case‑insensitive ASCII string compare */
int wf_ascii_iequals(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_UTIL_H */
