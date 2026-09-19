/** jetstream_replay.h — Jetstream v2 sealed-archive replay planning. */
#ifndef WOLFRAM_JETSTREAM_REPLAY_H
#define WOLFRAM_JETSTREAM_REPLAY_H

#include "wolfram/xrpc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_JETSTREAM_PLAN_SNAPSHOT_NSID "network.bsky.jetstream.planSnapshot"
#define WF_JETSTREAM_REPLAY_MAX_KINDS 4u
#define WF_JETSTREAM_REPLAY_MAX_DIDS 10000u
#define WF_JETSTREAM_REPLAY_MAX_COLLECTIONS 100u

/**
 * planSnapshot filter/window. Empty arrays mean "all", matching the lexicon.
 *
 * after_seq is an exclusive lower bound. before_seq is an inclusive upper
 * bound only when has_before_seq is non-zero. Sequence values are required to
 * fit exactly in JSON's interoperable integer range (<= 2^53 - 1).
 */
typedef struct wf_jetstream_replay_filter {
    const char *const *kinds;
    size_t kinds_count;
    const char *const *dids;
    size_t dids_count;
    const char *const *collections;
    size_t collections_count;
    uint64_t after_seq;
    uint64_t before_seq;
    int has_before_seq;
} wf_jetstream_replay_filter;

typedef enum wf_jetstream_replay_segment_mode {
    WF_JETSTREAM_REPLAY_SEGMENT_UNKNOWN = 0,
    WF_JETSTREAM_REPLAY_SEGMENT_WHOLE,
    WF_JETSTREAM_REPLAY_SEGMENT_BLOCKS,
} wf_jetstream_replay_segment_mode;

typedef struct wf_jetstream_replay_block_range {
    uint64_t first;
    uint64_t last;
} wf_jetstream_replay_block_range;

typedef struct wf_jetstream_replay_segment {
    char *name;
    uint64_t index;
    char *checksum;
    uint64_t min_seq;
    uint64_t max_seq;
    wf_jetstream_replay_segment_mode mode;
    wf_jetstream_replay_block_range *blocks;
    size_t blocks_count;
} wf_jetstream_replay_segment;

typedef struct wf_jetstream_replay_stats {
    uint64_t segments_examined;
    uint64_t segments_matched;
    uint64_t blocks_matched;
    uint64_t entries;
} wf_jetstream_replay_stats;

/**
 * One immutable planSnapshot page.
 *
 * Pin sealed_tip_seq from the first page. A caller has finished the sealed
 * archive once planned_through_seq >= sealed_tip_seq. When more work remains,
 * the next request uses after_seq=planned_through_seq and
 * before_seq=sealed_tip_seq.
 */
typedef struct wf_jetstream_replay_plan_page {
    uint64_t planned_through_seq;
    uint64_t sealed_tip_seq;
    wf_jetstream_replay_segment *segments;
    size_t segments_count;
    wf_jetstream_replay_stats stats;
} wf_jetstream_replay_plan_page;

/** Validate a replay filter/window without doing I/O. */
wf_status
wf_jetstream_replay_filter_validate(const wf_jetstream_replay_filter *filter);

/** Build the exact planSnapshot JSON request. Caller owns *out_json. */
wf_status
wf_jetstream_replay_plan_json(const wf_jetstream_replay_filter *filter,
                              char **out_json, size_t *out_json_len);

/** Parse one planSnapshot response into owned typed data. */
wf_status wf_jetstream_replay_plan_parse(const char *json, size_t json_len,
                                         wf_jetstream_replay_plan_page *out);

/**
 * Issue network.bsky.jetstream.planSnapshot through an existing XRPC client.
 *
 * For Bluesky-hosted replay, install the raw archive API key with
 * wf_xrpc_client_set_auth(client, key) before this call. Wolfram does not copy
 * the key into replay state or return it in any result.
 */
wf_status wf_jetstream_replay_plan(
    wf_xrpc_client *client,
    const wf_jetstream_replay_filter *filter,
    wf_jetstream_replay_plan_page *out);

/** Release all storage owned by a parsed plan page and reset it to zero. */
void wf_jetstream_replay_plan_page_free(wf_jetstream_replay_plan_page *page);

#ifdef __cplusplus
}
#endif
#endif /* WOLFRAM_JETSTREAM_REPLAY_H */
