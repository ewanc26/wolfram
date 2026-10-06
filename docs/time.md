# Timestamps

[`wolfram/time.h`](../include/wolfram/time.h) parses and formats AT Protocol timestamps and turns an age into the short text a feed shows, without any of the C library's time functions. Cobalt and Indigo each had their own copy, because `timegm`, `mktime`, `localtime` and the locale are missing or unreliable on the consoles. It is pure arithmetic and builds on every target.

## Parsing

`wf_time_parse_rfc3339()` accepts exactly what [`wf_syntax_datetime_is_valid()`](../include/wolfram/syntax.h) accepts, which is the lexicon `datetime` format as the atproto interop fixtures define it (`test/fixtures/syntax/datetime_*.txt`): an uppercase `T`, a required timezone, any number of fractional digits, and no `-00:00`. On top of that it refuses dates that do not exist, such as 30 February or 29 February in 1900.

A numeric offset is converted, not ignored: `1985-04-12T23:20:50.123-07:00` is seven hours after the same wall time with `Z`. The two client copies refused every offset, which would have dropped valid records from any PDS that writes one. Fractional seconds are dropped, so the result is whole seconds rounded down, and a leap second (`:60`) reads as the first second of the next minute.

## Formatting and relative age

`wf_time_format_rfc3339()` writes `YYYY-MM-DDTHH:MM:SSZ` for years 0000 to 9999 and refuses anything outside that. `wf_time_relative()` gives `45s`, `5m`, `3h`, `2d` (under a week), `3w` (under a year) and `2y`, truncating rather than rounding. A timestamp in the future, which means the console's clock is behind, reads as `0s`. The text is always English and ASCII; localisation is each client's job.

## Vectors

[`test/vectors/time.json`](../test/vectors/time.json) holds parse, format and relative-age cases, with epochs computed independently with Python's `calendar.timegm`. `test/test_time.c` also runs every line of the interop datetime fixtures through the parser, so a datetime the lexicon allows cannot be refused here.
