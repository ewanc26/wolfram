#include "wolfram/jetstream_replay.h"

#include <cJSON.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBZSTD
#include <zstd.h>
#endif

#define WF_JSON_MAX_EXACT_U64 UINT64_C(9007199254740991)
#define WF_REPLAY_MAX_NAME_BYTES 256u
#define WF_REPLAY_MAX_BLOCK_EVENTS (1u << 18)
#define WF_REPLAY_SEGMENT_HEADER_BYTES 256u

static char *wf_replay_strdup(const char *text) {
    if (!text) return NULL;
    const size_t len = strlen(text) + 1u;
    char *copy = malloc(len);
    if (copy) memcpy(copy, text, len);
    return copy;
}

static uint16_t wf_replay_u16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

static uint32_t wf_replay_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static uint64_t wf_replay_u64(const unsigned char *p) {
    uint64_t value = 0u;
    for (unsigned int i = 0u; i < 8u; ++i) value |= (uint64_t)p[i] << (i * 8u);
    return value;
}

wf_status wf_jetstream_replay_segment_header_parse(
    const void *bytes, size_t bytes_len,
    wf_jetstream_replay_segment_header *out) {
    if (!bytes || !out || bytes_len < WF_REPLAY_SEGMENT_HEADER_BYTES) {
        return WF_ERR_INVALID_ARG;
    }
    const unsigned char *data = bytes;
    if (memcmp(data, "jss0", 4u) != 0) return WF_ERR_PARSE;
    memset(out, 0, sizeof(*out));
    out->checksum = wf_replay_u64(data + 4u);
    out->version = wf_replay_u16(data + 12u);
    out->block_count = wf_replay_u32(data + 14u);
    out->event_count = wf_replay_u32(data + 18u);
    out->unique_did_count = wf_replay_u32(data + 22u);
    out->min_seq = wf_replay_u64(data + 26u);
    out->max_seq = wf_replay_u64(data + 34u);
    out->min_witnessed_at = (int64_t)wf_replay_u64(data + 42u);
    out->max_witnessed_at = (int64_t)wf_replay_u64(data + 50u);
    out->footer_offset = wf_replay_u64(data + 58u);
    out->did_bloom_offset = wf_replay_u64(data + 66u);
    out->block_did_bloom_offset = wf_replay_u64(data + 74u);
    out->collection_index_offset = wf_replay_u64(data + 82u);
    out->block_index_offset = wf_replay_u64(data + 90u);
    if (out->version == 0u ||
        out->block_count > WF_JETSTREAM_REPLAY_MAX_SEGMENTS ||
        out->min_seq > out->max_seq ||
        out->min_witnessed_at > out->max_witnessed_at ||
        out->footer_offset < WF_REPLAY_SEGMENT_HEADER_BYTES ||
        out->did_bloom_offset < out->footer_offset ||
        out->block_did_bloom_offset < out->did_bloom_offset ||
        out->collection_index_offset < out->block_did_bloom_offset ||
        out->block_index_offset < out->collection_index_offset) {
        return WF_ERR_PARSE;
    }
    return WF_OK;
}

wf_status wf_jetstream_replay_block_frame(const void *bytes, size_t bytes_len,
                                          size_t offset, const void **out_block,
                                          size_t *out_block_len,
                                          size_t *out_next_offset) {
    if (!bytes || !out_block || !out_block_len || !out_next_offset ||
        offset > bytes_len || bytes_len - offset < 8u)
        return WF_ERR_INVALID_ARG;
    const unsigned char *data = bytes;
    const uint64_t length64 = wf_replay_u64(data + offset);
    if (length64 == 0u ||
        length64 > WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES - 8u ||
        length64 > bytes_len - offset - 8u)
        return WF_ERR_PARSE;
    const size_t length = (size_t)length64;
    *out_block = data + offset + 8u;
    *out_block_len = length;
    *out_next_offset = offset + 8u + length;
    return WF_OK;
}

void wf_jetstream_replay_events_free(wf_jetstream_replay_event *events,
                                     size_t count) {
    if (!events) return;
    for (size_t i = 0u; i < count; ++i) {
        free(events[i].collection);
        free(events[i].did);
        free(events[i].rkey);
        free(events[i].rev);
        free(events[i].payload);
    }
    free(events);
}

static char *wf_replay_copy_column(const unsigned char *blob, size_t *offset,
                                   size_t length, size_t total) {
    if (length > total - *offset) return NULL;
    char *copy = malloc(length + 1u);
    if (!copy) return NULL;
    memcpy(copy, blob + *offset, length);
    copy[length] = '\0';
    *offset += length;
    return copy;
}

wf_status
wf_jetstream_replay_block_decode(const void *bytes, size_t bytes_len,
                                 wf_jetstream_replay_event **out_events,
                                 size_t *out_count) {
    if (!bytes || !out_events || !out_count || bytes_len < 4u ||
        bytes_len > WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES)
        return WF_ERR_INVALID_ARG;
    *out_events = NULL;
    *out_count = 0u;
    const unsigned char *data = bytes;
    const uint32_t count32 = wf_replay_u32(data);
    const size_t count = count32;
    const size_t fixed =
        4u + count * (8u + 8u + 8u + 1u + 1u + 2u + 1u + 1u + 4u);
    if (count > WF_REPLAY_MAX_BLOCK_EVENTS || count > (SIZE_MAX - 4u) / 34u ||
        fixed > bytes_len)
        return WF_ERR_INVALID_ARG;
    if (count == 0u) return bytes_len == 4u ? WF_OK : WF_ERR_INVALID_ARG;

    wf_jetstream_replay_event *events = calloc(count, sizeof(*events));
    if (!events) return WF_ERR_ALLOC;
    size_t offset = 4u;
    const unsigned char *seq = data + offset;
    offset += count * 8u;
    const unsigned char *witnessed = data + offset;
    offset += count * 8u;
    const unsigned char *indexed = data + offset;
    offset += count * 8u;
    const unsigned char *kind = data + offset;
    offset += count;
    const unsigned char *collection_len = data + offset;
    offset += count;
    const unsigned char *did_len = data + offset;
    offset += count * 2u;
    const unsigned char *rkey_len = data + offset;
    offset += count;
    const unsigned char *rev_len = data + offset;
    offset += count;
    const unsigned char *payload_len = data + offset;
    offset += count * 4u;

    size_t collection_total = 0u, did_total = 0u, rkey_total = 0u,
           rev_total = 0u, payload_total = 0u;
    for (size_t i = 0u; i < count; ++i) {
        if (kind[i] < 1u || kind[i] > 6u ||
            collection_total > SIZE_MAX - collection_len[i] ||
            did_total > SIZE_MAX - wf_replay_u16(did_len + i * 2u) ||
            rkey_total > SIZE_MAX - rkey_len[i] ||
            rev_total > SIZE_MAX - rev_len[i] ||
            payload_total > SIZE_MAX - wf_replay_u32(payload_len + i * 4u)) {
            wf_jetstream_replay_events_free(events, count);
            return WF_ERR_INVALID_ARG;
        }
        collection_total += collection_len[i];
        did_total += wf_replay_u16(did_len + i * 2u);
        rkey_total += rkey_len[i];
        rev_total += rev_len[i];
        payload_total += wf_replay_u32(payload_len + i * 4u);
    }
    if (collection_total > bytes_len - offset ||
        did_total > bytes_len - offset - collection_total ||
        rkey_total > bytes_len - offset - collection_total - did_total ||
        rev_total >
            bytes_len - offset - collection_total - did_total - rkey_total ||
        payload_total != bytes_len - offset - collection_total - did_total -
                             rkey_total - rev_total) {
        wf_jetstream_replay_events_free(events, count);
        return WF_ERR_INVALID_ARG;
    }
    const unsigned char *collection_blob = data + offset;
    offset += collection_total;
    const unsigned char *did_blob = data + offset;
    offset += did_total;
    const unsigned char *rkey_blob = data + offset;
    offset += rkey_total;
    const unsigned char *rev_blob = data + offset;
    offset += rev_total;
    const unsigned char *payload_blob = data + offset;
    size_t collection_at = 0u, did_at = 0u, rkey_at = 0u, rev_at = 0u,
           payload_at = 0u;
    for (size_t i = 0u; i < count; ++i) {
        events[i].seq = wf_replay_u64(seq + i * 8u);
        events[i].witnessed_at = (int64_t)wf_replay_u64(witnessed + i * 8u);
        events[i].indexed_at = (int64_t)wf_replay_u64(indexed + i * 8u);
        events[i].kind = kind[i];
        events[i].collection =
            wf_replay_copy_column(collection_blob, &collection_at,
                                  collection_len[i], collection_total);
        events[i].did = wf_replay_copy_column(
            did_blob, &did_at, wf_replay_u16(did_len + i * 2u), did_total);
        events[i].rkey =
            wf_replay_copy_column(rkey_blob, &rkey_at, rkey_len[i], rkey_total);
        events[i].rev =
            wf_replay_copy_column(rev_blob, &rev_at, rev_len[i], rev_total);
        const size_t plen = wf_replay_u32(payload_len + i * 4u);
        if (plen > payload_total - payload_at)
            events[i].payload = NULL;
        else if (plen) {
            events[i].payload = malloc(plen);
            if (events[i].payload)
                memcpy(events[i].payload, payload_blob + payload_at, plen);
        }
        events[i].payload_len = plen;
        payload_at += plen;
        if (!events[i].collection || !events[i].did || !events[i].rkey ||
            !events[i].rev || (plen && !events[i].payload)) {
            wf_jetstream_replay_events_free(events, count);
            return WF_ERR_ALLOC;
        }
    }
    *out_events = events;
    *out_count = count;
    return WF_OK;
}

wf_status
wf_jetstream_replay_block_decode_zstd(const void *bytes, size_t bytes_len,
                                      wf_jetstream_replay_event **out_events,
                                      size_t *out_count) {
#ifndef HAVE_LIBZSTD
    (void)bytes;
    (void)bytes_len;
    (void)out_events;
    (void)out_count;
    return WF_ERR_INVALID_ARG;
#else
    if (!bytes || bytes_len == 0u ||
        bytes_len > WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES)
        return WF_ERR_INVALID_ARG;
    const unsigned long long size = ZSTD_getFrameContentSize(bytes, bytes_len);
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN ||
        size > WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES)
        return WF_ERR_INVALID_ARG;
    unsigned char *decoded = malloc((size_t)size);
    if (!decoded) return WF_ERR_ALLOC;
    const size_t result =
        ZSTD_decompress(decoded, (size_t)size, bytes, bytes_len);
    if (ZSTD_isError(result) || result != (size_t)size) {
        free(decoded);
        return WF_ERR_INVALID_ARG;
    }
    const wf_status status = wf_jetstream_replay_block_decode(
        decoded, result, out_events, out_count);
    free(decoded);
    return status;
#endif
}

static int wf_replay_kind_valid(const char *kind) {
    return kind &&
           (strcmp(kind, "commit") == 0 || strcmp(kind, "identity") == 0 ||
            strcmp(kind, "account") == 0 || strcmp(kind, "sync") == 0);
}

static int wf_replay_string_list_valid(const char *const *values,
                                       size_t count) {
    if (count && !values) return 0;
    for (size_t i = 0u; i < count; ++i) {
        if (!values[i] || !values[i][0]) return 0;
    }
    return 1;
}

wf_status
wf_jetstream_replay_filter_validate(const wf_jetstream_replay_filter *filter) {
    if (!filter || filter->kinds_count > WF_JETSTREAM_REPLAY_MAX_KINDS ||
        filter->dids_count > WF_JETSTREAM_REPLAY_MAX_DIDS ||
        filter->collections_count > WF_JETSTREAM_REPLAY_MAX_COLLECTIONS ||
        filter->after_seq > WF_JSON_MAX_EXACT_U64 ||
        (filter->has_before_seq &&
         (filter->before_seq > WF_JSON_MAX_EXACT_U64 ||
          filter->before_seq < filter->after_seq)) ||
        !wf_replay_string_list_valid(filter->kinds, filter->kinds_count) ||
        !wf_replay_string_list_valid(filter->dids, filter->dids_count) ||
        !wf_replay_string_list_valid(filter->collections,
                                     filter->collections_count)) {
        return WF_ERR_INVALID_ARG;
    }

    int includes_commit = filter->kinds_count == 0u;
    for (size_t i = 0u; i < filter->kinds_count; ++i) {
        if (!wf_replay_kind_valid(filter->kinds[i])) return WF_ERR_INVALID_ARG;
        if (strcmp(filter->kinds[i], "commit") == 0) includes_commit = 1;
    }
    if (filter->collections_count && !includes_commit)
        return WF_ERR_INVALID_ARG;

    for (size_t i = 0u; i < filter->dids_count; ++i) {
        if (strncmp(filter->dids[i], "did:", 4u) != 0)
            return WF_ERR_INVALID_ARG;
    }
    return WF_OK;
}

static int wf_replay_add_strings(cJSON *root, const char *name,
                                 const char *const *values, size_t count) {
    cJSON *array = cJSON_CreateArray();
    if (!array) return 0;
    for (size_t i = 0u; i < count; ++i) {
        cJSON *item = cJSON_CreateString(values[i]);
        if (!item || !cJSON_AddItemToArray(array, item)) {
            cJSON_Delete(item);
            cJSON_Delete(array);
            return 0;
        }
    }
    if (!cJSON_AddItemToObject(root, name, array)) {
        cJSON_Delete(array);
        return 0;
    }
    return 1;
}

wf_status
wf_jetstream_replay_plan_json(const wf_jetstream_replay_filter *filter,
                              char **out_json, size_t *out_json_len) {
    if (!out_json || !out_json_len) return WF_ERR_INVALID_ARG;
    *out_json = NULL;
    *out_json_len = 0u;
    const wf_status valid = wf_jetstream_replay_filter_validate(filter);
    if (valid != WF_OK) return valid;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;

    if ((filter->kinds_count &&
         !wf_replay_add_strings(root, "kinds", filter->kinds,
                                filter->kinds_count)) ||
        (filter->dids_count &&
         !wf_replay_add_strings(root, "dids", filter->dids,
                                filter->dids_count)) ||
        (filter->collections_count &&
         !wf_replay_add_strings(root, "collections", filter->collections,
                                filter->collections_count)) ||
        !cJSON_AddNumberToObject(root, "afterSeq", (double)filter->after_seq) ||
        (filter->has_before_seq &&
         !cJSON_AddNumberToObject(root, "beforeSeq",
                                  (double)filter->before_seq))) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;
    *out_json = json;
    *out_json_len = strlen(json);
    return WF_OK;
}

static int wf_replay_json_u64(const cJSON *object, const char *name,
                              uint64_t *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < 0.0 ||
        item->valuedouble > (double)WF_JSON_MAX_EXACT_U64 ||
        floor(item->valuedouble) != item->valuedouble) {
        return 0;
    }
    *out = (uint64_t)item->valuedouble;
    return 1;
}

static int wf_replay_hex16(const char *value) {
    if (!value || strlen(value) != 16u) return 0;
    for (size_t i = 0u; i < 16u; ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

static int wf_replay_json_i64(const cJSON *object, const char *name,
                              int64_t *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < (double)INT64_MIN ||
        item->valuedouble > (double)INT64_MAX)
        return 0;
    *out = (int64_t)item->valuedouble;
    return 1;
}

static void wf_replay_segment_free(wf_jetstream_replay_segment *segment) {
    if (!segment) return;
    free(segment->name);
    free(segment->checksum);
    free(segment->blocks);
    memset(segment, 0, sizeof(*segment));
}

void wf_jetstream_replay_plan_page_free(wf_jetstream_replay_plan_page *page) {
    if (!page) return;
    for (size_t i = 0u; i < page->segments_count; ++i)
        wf_replay_segment_free(&page->segments[i]);
    free(page->segments);
    memset(page, 0, sizeof(*page));
}

void wf_jetstream_replay_manifest_free(wf_jetstream_replay_manifest *manifest) {
    if (!manifest) return;
    for (size_t i = 0u; i < manifest->segments_count; ++i) {
        free(manifest->segments[i].name);
        free(manifest->segments[i].checksum);
    }
    free(manifest->segments);
    memset(manifest, 0, sizeof(*manifest));
}

wf_status
wf_jetstream_replay_manifest_parse(const char *json, size_t json_len,
                                   wf_jetstream_replay_manifest *out) {
    if (!json || !json_len || !out) return WF_ERR_INVALID_ARG;
    if (json_len > WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES) return WF_ERR_PARSE;
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;
    wf_status status = WF_ERR_PARSE;
    const cJSON *segments = cJSON_GetObjectItemCaseSensitive(root, "segments");
    const int count =
        cJSON_IsArray(segments) ? cJSON_GetArraySize(segments) : -1;
    if (count < 0 || (size_t)count > WF_JETSTREAM_REPLAY_MAX_SEGMENTS)
        goto done;
    if (count) {
        out->segments = calloc((size_t)count, sizeof(*out->segments));
        if (!out->segments) {
            status = WF_ERR_ALLOC;
            goto done;
        }
        out->segments_count = (size_t)count;
    }
    for (int i = 0; i < count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(segments, i);
        wf_jetstream_replay_manifest_segment *segment =
            &out->segments[(size_t)i];
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *checksum =
            cJSON_GetObjectItemCaseSensitive(item, "checksum");
        if (!cJSON_IsObject(item) || !cJSON_IsString(name) ||
            !name->valuestring || !name->valuestring[0] ||
            strlen(name->valuestring) > WF_REPLAY_MAX_NAME_BYTES ||
            !cJSON_IsString(checksum) ||
            !wf_replay_hex16(checksum->valuestring) ||
            !wf_replay_json_u64(item, "index", &segment->index) ||
            !wf_replay_json_u64(item, "sizeBytes", &segment->size_bytes) ||
            !wf_replay_json_u64(item, "eventCount", &segment->event_count) ||
            !wf_replay_json_u64(item, "minSeq", &segment->min_seq) ||
            !wf_replay_json_u64(item, "maxSeq", &segment->max_seq) ||
            !wf_replay_json_i64(item, "minWitnessedAt",
                                &segment->min_witnessed_at) ||
            !wf_replay_json_i64(item, "maxWitnessedAt",
                                &segment->max_witnessed_at) ||
            segment->max_seq < segment->min_seq ||
            segment->max_witnessed_at < segment->min_witnessed_at)
            goto done;
        if (i > 0) {
            const wf_jetstream_replay_manifest_segment *previous =
                &out->segments[(size_t)i - 1u];
            if (segment->index <= previous->index ||
                segment->min_seq <= previous->max_seq)
                goto done;
        }
        segment->name = wf_replay_strdup(name->valuestring);
        segment->checksum = wf_replay_strdup(checksum->valuestring);
        if (!segment->name || !segment->checksum) {
            status = WF_ERR_ALLOC;
            goto done;
        }
    }
    status = WF_OK;
done:
    cJSON_Delete(root);
    if (status != WF_OK) wf_jetstream_replay_manifest_free(out);
    return status;
}

static wf_status wf_replay_parse_blocks(const cJSON *value,
                                        wf_jetstream_replay_segment *segment) {
    if (!cJSON_IsArray(value)) return WF_ERR_PARSE;
    const int count = cJSON_GetArraySize(value);
    if (count < 0 || (size_t)count > WF_JETSTREAM_REPLAY_MAX_BLOCK_RANGES)
        return WF_ERR_PARSE;
    if (count == 0) return WF_OK;
    segment->blocks = calloc((size_t)count, sizeof(*segment->blocks));
    if (!segment->blocks) return WF_ERR_ALLOC;
    segment->blocks_count = (size_t)count;

    for (int i = 0; i < count; ++i) {
        const cJSON *entry = cJSON_GetArrayItem(value, i);
        uint64_t first = 0u, last = 0u;
        if (!cJSON_IsObject(entry) ||
            !wf_replay_json_u64(entry, "first", &first) ||
            !wf_replay_json_u64(entry, "last", &last) || last < first) {
            return WF_ERR_PARSE;
        }
        segment->blocks[(size_t)i].first = first;
        segment->blocks[(size_t)i].last = last;
    }
    return WF_OK;
}

static wf_status wf_replay_parse_segment(const cJSON *value,
                                         wf_jetstream_replay_segment *out) {
    if (!cJSON_IsObject(value)) return WF_ERR_PARSE;

    const cJSON *name = cJSON_GetObjectItemCaseSensitive(value, "name");
    const cJSON *checksum = cJSON_GetObjectItemCaseSensitive(value, "checksum");
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(value, "mode");
    if (!cJSON_IsString(name) || !name->valuestring || !name->valuestring[0] ||
        !cJSON_IsString(checksum) || !wf_replay_hex16(checksum->valuestring) ||
        !cJSON_IsString(mode) || !mode->valuestring ||
        !wf_replay_json_u64(value, "index", &out->index) ||
        !wf_replay_json_u64(value, "minSeq", &out->min_seq) ||
        !wf_replay_json_u64(value, "maxSeq", &out->max_seq) ||
        out->max_seq < out->min_seq) {
        return WF_ERR_PARSE;
    }

    out->name = wf_replay_strdup(name->valuestring);
    out->checksum = wf_replay_strdup(checksum->valuestring);
    if (!out->name || !out->checksum) return WF_ERR_ALLOC;

    if (strcmp(mode->valuestring, "segment") == 0) {
        out->mode = WF_JETSTREAM_REPLAY_SEGMENT_WHOLE;
        return WF_OK;
    }
    if (strcmp(mode->valuestring, "blocks") != 0) return WF_ERR_PARSE;

    out->mode = WF_JETSTREAM_REPLAY_SEGMENT_BLOCKS;
    const cJSON *blocks = cJSON_GetObjectItemCaseSensitive(value, "blocks");
    if (!blocks) return WF_ERR_PARSE;
    return wf_replay_parse_blocks(blocks, out);
}

wf_status wf_jetstream_replay_plan_parse(const char *json, size_t json_len,
                                         wf_jetstream_replay_plan_page *out) {
    if (!json || !json_len || !out) return WF_ERR_INVALID_ARG;
    if (json_len > WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES) return WF_ERR_PARSE;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;
    wf_status status = WF_ERR_PARSE;

    if (!cJSON_IsObject(root) ||
        !wf_replay_json_u64(root, "plannedThroughSeq",
                            &out->planned_through_seq) ||
        !wf_replay_json_u64(root, "sealedTipSeq", &out->sealed_tip_seq) ||
        out->planned_through_seq > out->sealed_tip_seq) {
        goto done;
    }

    const cJSON *segments = cJSON_GetObjectItemCaseSensitive(root, "segments");
    const cJSON *stats = cJSON_GetObjectItemCaseSensitive(root, "stats");
    if (!cJSON_IsArray(segments) || !cJSON_IsObject(stats) ||
        !wf_replay_json_u64(stats, "segmentsExamined",
                            &out->stats.segments_examined) ||
        !wf_replay_json_u64(stats, "segmentsMatched",
                            &out->stats.segments_matched) ||
        !wf_replay_json_u64(stats, "blocksMatched",
                            &out->stats.blocks_matched) ||
        !wf_replay_json_u64(stats, "entries", &out->stats.entries)) {
        goto done;
    }

    const int count = cJSON_GetArraySize(segments);
    if (count < 0 || (size_t)count > WF_JETSTREAM_REPLAY_MAX_SEGMENTS)
        goto done;
    if (count) {
        out->segments = calloc((size_t)count, sizeof(*out->segments));
        if (!out->segments) {
            status = WF_ERR_ALLOC;
            goto done;
        }
        out->segments_count = (size_t)count;
        for (int i = 0; i < count; ++i) {
            status = wf_replay_parse_segment(cJSON_GetArrayItem(segments, i),
                                             &out->segments[(size_t)i]);
            if (status != WF_OK) goto done;
        }
    }
    status = WF_OK;

done:
    cJSON_Delete(root);
    if (status != WF_OK) wf_jetstream_replay_plan_page_free(out);
    return status;
}

wf_status wf_jetstream_replay_plan(wf_xrpc_client *client,
                                   const wf_jetstream_replay_filter *filter,
                                   wf_jetstream_replay_plan_page *out) {
    if (!client || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    char *body = NULL;
    size_t body_len = 0u;
    wf_status status = wf_jetstream_replay_plan_json(filter, &body, &body_len);
    (void)body_len;
    if (status != WF_OK) return status;

    wf_response response = {0};
    status = wf_xrpc_procedure(client, WF_JETSTREAM_PLAN_SNAPSHOT_NSID, body,
                               &response);
    free(body);
    if (status != WF_OK) {
        wf_response_free(&response);
        return status;
    }

    status =
        wf_jetstream_replay_plan_parse(response.body, response.body_len, out);
    wf_response_free(&response);
    if (status != WF_OK) return status;

    if ((filter->has_before_seq && out->sealed_tip_seq > filter->before_seq) ||
        (out->sealed_tip_seq > filter->after_seq &&
         out->planned_through_seq <= filter->after_seq)) {
        wf_jetstream_replay_plan_page_free(out);
        return WF_ERR_PARSE;
    }
    return WF_OK;
}

static wf_status wf_replay_get_binary(wf_xrpc_client *client, const char *nsid,
                                      const wf_xrpc_param *params,
                                      size_t param_count, wf_response *out) {
    if (!client || !nsid || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_xrpc_query_params(client, nsid, params, param_count, out);
}

wf_status wf_jetstream_replay_get_segment(wf_xrpc_client *client,
                                          const char *name, wf_response *out) {
    if (!name || !name[0] || strlen(name) > WF_REPLAY_MAX_NAME_BYTES)
        return WF_ERR_INVALID_ARG;
    const wf_xrpc_param params[] = {{"name", name}};
    return wf_replay_get_binary(client, WF_JETSTREAM_GET_SEGMENT_NSID, params,
                                1u, out);
}

wf_status wf_jetstream_replay_list_segments(wf_xrpc_client *client,
                                            wf_response *out) {
    return wf_replay_get_binary(client, WF_JETSTREAM_LIST_SEGMENTS_NSID, NULL,
                                0u, out);
}

wf_status wf_jetstream_replay_get_block(wf_xrpc_client *client,
                                        const char *segment,
                                        uint64_t block_index,
                                        wf_response *out) {
    if (!segment || !segment[0] || strlen(segment) > WF_REPLAY_MAX_NAME_BYTES)
        return WF_ERR_INVALID_ARG;
    char index[32];
    const int written = snprintf(index, sizeof(index), "%" PRIu64, block_index);
    if (written < 0 || (size_t)written >= sizeof(index))
        return WF_ERR_INVALID_ARG;
    const wf_xrpc_param params[] = {{"segment", segment},
                                    {"blockIndex", index}};
    return wf_replay_get_binary(client, WF_JETSTREAM_GET_BLOCK_NSID, params, 2u,
                                out);
}
