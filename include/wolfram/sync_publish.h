#ifndef WOLFRAM_SYNC_PUBLISH_H
#define WOLFRAM_SYNC_PUBLISH_H

#include "wolfram/sync_subscribe.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build a framed atproto subscription event message.
 *
 * The wire format is exactly the inverse of `src/sync/sync_subscribe.c`'s
 * decoder: a CBOR header map `{ "op": 1, "t": "#<kind>" }` immediately
 * followed by the CBOR body map for the event kind in `ev->type`.
 *
 * For `WF_SUBSCRIBE_EVENT_ERROR` the header is `{ "op": -1 }` (no `t`) and the
 * body is `{ "error": ..., "message": ... }`.
 *
 * @param ev      Event to serialize (caller retains ownership).
 * @param out     Receives a heap-allocated buffer of framed CBOR bytes.
 *                Owned by the caller; free with `free()`.
 * @param out_len Receives the length of `*out`.
 * @return WF_OK on success, or an error status on failure.
 *
 * Round-trip invariant: bytes produced here MUST decode back (via the
 * subscribe decoder) into an event equal to `ev` at the field level. */
wf_status wf_sync_publish_event(const wf_subscribe_event *ev,
                                unsigned char **out, size_t *out_len);

/* Build an `op: -1` error frame.
 *
 * @param seq     Sequence number associated with the error (0 if none).
 * @param error   Error name (e.g. "FutureCursor"); may be NULL (emitted as null).
 * @param message Human-readable message; may be NULL (omitted when absent).
 * @param out     Receives a heap-allocated buffer of framed CBOR bytes;
 *                free with `free()`.
 * @param out_len Receives the length of `*out`. */
wf_status wf_sync_publish_error(int64_t seq, const char *error,
                                const char *message,
                                unsigned char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SYNC_PUBLISH_H */
