/*
 * time.h -- AT Protocol timestamps without the C library's time functions.
 *
 * Parse a lexicon `datetime` to Unix seconds, format Unix seconds back as a
 * UTC `datetime`, and turn an age into the short text feeds show. All three
 * are pure arithmetic: no timegm, mktime, localtime or locale, none of which
 * is safe or present on the console targets. Builds on every target.
 *
 * Parsing follows the lexicon `datetime` format as checked by
 * wf_syntax_datetime_is_valid (and the interop fixtures in
 * test/fixtures/syntax): `T` separator, a required timezone (`Z` or
 * +hh:mm / -hh:mm), any number of fractional digits, `-00:00` refused. A
 * numeric offset is converted to UTC, not read as if it were Zulu. Fractional
 * seconds are dropped (the epoch is whole seconds, rounded down).
 */

#ifndef WOLFRAM_TIME_H
#define WOLFRAM_TIME_H

#include "wolfram/xrpc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse `datetime` into Unix seconds. WF_ERR_PARSE if it is not a valid
 * lexicon datetime (including impossible dates such as 1985-02-30). *epoch is
 * left untouched on error. A leap second (:60) is read as the first second of
 * the next minute. */
wf_status wf_time_parse_rfc3339(const char *datetime, int64_t *epoch);

/* Write `epoch` as YYYY-MM-DDTHH:MM:SSZ into `out` (at least 21 bytes).
 * WF_ERR_INVALID_ARG if `out` is too small or `epoch` is outside years
 * 0000 to 9999. */
wf_status wf_time_format_rfc3339(int64_t epoch, char *out, size_t out_size);

/* Short relative age of `epoch` at `now`, always English and ASCII:
 * "45s", "5m", "3h", "2d" (under a week), "3w" (under a year), "2y".
 * Truncates, never rounds. An epoch in the future (the console's clock is
 * behind) reads as "0s". Localisation belongs to the client. `out` is always
 * NUL-terminated when out_size > 0; it needs at most 12 bytes. */
void wf_time_relative(int64_t epoch, int64_t now, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_TIME_H */
