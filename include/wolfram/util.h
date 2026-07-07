#ifndef WOLFRAM_UTIL_H
#define WOLFRAM_UTIL_H

#include "wolfram/agent.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Duplicate a string span (does not require null‑termination) */
char *wf_dup_span(const char *s, size_t len);

/* Case‑insensitive ASCII string compare */
int wf_ascii_iequals(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_UTIL_H */
