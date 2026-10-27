#ifndef WOLFRAM_TID_H
#define WOLFRAM_TID_H

#include <stdint.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TID (record key) generation.
 *
 * A TID is a 13-character "sortable base32" (s32) string carrying a 64-bit
 * value laid out as:
 *
 *     (timestamp_micros << 10) | (clockid & 0x3FF)
 *
 * where timestamp_micros is microseconds since the Unix epoch (53 bits) and
 * clockid is a 10-bit value (0..1023). The encoding is big-endian s32 and is
 * always exactly WF_TID_LEN characters, matching the atproto TID spec/regex.
 *
 * The TIDs produced here are bit-compatible with atproto implementations and
 * pass wf_syntax_tid_is_valid().
 *
 * Ownership: every output here is written into a caller-owned fixed-size
 * buffer of 14 bytes (13 chars + NUL). Nothing is heap-allocated, so there is
 * no matching _free.
 *
 * Thread-safety: wf_tid_now() is safe to call concurrently from multiple
 * threads; the process-wide clock state is guarded by an internal mutex. The
 * remaining functions are pure/stateless and need no synchronization.
 */

/* Number of significant characters in a TID. */
#define WF_TID_LEN 13

/*
 * Mint the next monotonic TID for this process into `out` (NUL-terminated).
 *
 * `out` must point to a buffer of at least 14 bytes. The returned TID is
 * strictly greater than any TID previously returned by this clock within the
 * process, making emitted TIDs sortable and unique. Thread-safe.
 *
 * Returns WF_OK on success, WF_ERR_INVALID_ARG if `out` is NULL.
 */
wf_status wf_tid_now(char out[15]);

/*
 * Deterministically build a TID from an explicit timestamp and clockid.
 *
 * No clock state is consulted or mutated (pure function). `out` must point to
 * a buffer of at least 14 bytes. `clockid` is masked to 10 bits (0..1023).
 *
 * Returns WF_OK on success, WF_ERR_INVALID_ARG if `out` is NULL.
 */
wf_status wf_tid_from_time(uint64_t timestamp_micros, uint32_t clockid,
                           char out[15]);

/*
 * Encode a raw 64-bit TID value into its 13-character s32 string form.
 *
 * `out` must point to a buffer of at least 14 bytes. Pure function.
 *
 * Returns WF_OK on success, WF_ERR_INVALID_ARG if `out` is NULL.
 */
wf_status wf_tid_encode(uint64_t tid_value, char out[15]);

/*
 * Decode a TID string back into its raw 64-bit value.
 *
 * Returns WF_OK on success (and writes `*out_value`), or WF_ERR_INVALID_ARG
 * if `tid` is NULL/empty, not exactly WF_TID_LEN characters, or contains
 * characters outside the s32 alphabet.
 */
wf_status wf_tid_decode(const char *tid, uint64_t *out_value);

/*
 * Recover the timestamp (microseconds since the Unix epoch) embedded in a TID.
 *
 * Returns 0 if `tid` is NULL or not a valid TID; otherwise the embedded
 * timestamp.
 */
uint64_t wf_tid_timestamp_micros(const char *tid);

/*
 * Recover the clockid (0..1023) embedded in a TID.
 *
 * Returns 0 if `tid` is NULL or not a valid TID; otherwise the embedded
 * clockid.
 */
uint32_t wf_tid_clockid(const char *tid);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_TID_H */
