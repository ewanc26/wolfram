#include "wolfram/repo.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

static const wf_cbor_item *get(const wf_cbor_item *map, const char *name) {
    for (size_t i = 0; i < map->map.count; i++)
        if (map->map.pairs[i].key->string.len == strlen(name) &&
            memcmp(map->map.pairs[i].key->string.str, name, strlen(name)) == 0)
            return map->map.pairs[i].value;
    return NULL;
}

int main(void) {
    static const wf_record_schema string_schema = { WF_RECORD_STRING, NULL, 0, NULL };
    static const wf_record_schema integer_schema = { WF_RECORD_INTEGER, NULL, 0, NULL };
    static const wf_record_schema boolean_schema = { WF_RECORD_BOOLEAN, NULL, 0, NULL };
    static const wf_record_schema langs_schema = { WF_RECORD_ARRAY, NULL, 0, &string_schema };
    static const wf_record_property props[] = {
        { "text", &string_schema, 1 },
        { "createdAt", &string_schema, 1 },
        { "langs", &langs_schema, 0 },
        { "replyCount", &integer_schema, 0 },
        { "enabled", &boolean_schema, 0 },
    };
    static const wf_record_schema post = { WF_RECORD_OBJECT, props, 5, NULL };

    unsigned char *cbor = NULL; size_t len = 0;
    WF_CHECK(wf_record_encode_json("app.bsky.feed.post", &post,
        "{\"text\":\"hello\",\"createdAt\":\"2026-07-05T12:00:00Z\","
        "\"langs\":[\"en\",\"cy\"],\"replyCount\":-2,\"enabled\":true}",
        &cbor, &len) == WF_OK);
    WF_CHECK(cbor != NULL && len > 0);
    wf_cbor_item *record = wf_cbor_parse(cbor, len);
    WF_CHECK(record && record->type == WF_CBOR_MAP && record->map.count == 6);
    const wf_cbor_item *type = get(record, "$type");
    WF_CHECK(type && type->type == WF_CBOR_STRING &&
             strcmp(type->string.str, "app.bsky.feed.post") == 0);
    const wf_cbor_item *langs = get(record, "langs");
    WF_CHECK(langs && langs->type == WF_CBOR_ARRAY && langs->children.count == 2);
    const wf_cbor_item *negative = get(record, "replyCount");
    WF_CHECK(negative && negative->type == WF_CBOR_NEGATIVE && negative->neginteger == 1);
    wf_cbor_free(record); free(cbor);

    WF_CHECK(wf_record_encode_json("app.bsky.feed.post", &post,
        "{\"text\":\"missing timestamp\"}", &cbor, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_record_encode_json("app.bsky.feed.post", &post,
        "{\"text\":1,\"createdAt\":\"now\"}", &cbor, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_record_encode_json("app.bsky.feed.post", &post,
        "{\"text\":\"x\",\"createdAt\":\"now\",\"unknown\":true}",
        &cbor, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_record_encode_json("app.bsky.feed.post", &post,
        "{\"$type\":\"wrong\",\"text\":\"x\",\"createdAt\":\"now\"}",
        &cbor, &len) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_record_encode_json("app.bsky.feed.post", &post,
        "{\"text\":\"x\",\"text\":\"y\",\"createdAt\":\"now\"}",
        &cbor, &len) == WF_ERR_INVALID_ARG);

    WF_TEST_SUMMARY();
}
