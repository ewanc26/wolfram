#include "wolfram/jetstream.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_LIBZSTD
#include <zstd.h>
#endif

int main(void) {
    const char *collections[] = {"app.bsky.feed.post", "app.bsky.graph.*"};
    const char *dids[] = {"did:plc:a/b"};
    wf_jetstream_options options = {
        .endpoint = "wss://jetstream.example/subscribe",
        .wanted_collections = collections,
        .wanted_collections_count = 2,
        .wanted_dids = dids,
        .wanted_dids_count = 1,
        .cursor = 1725519626134432LL,
        .max_message_size_bytes = 1000000,
        .require_hello = 1,
    };
    char *url = NULL;
    WF_CHECK(wf_jetstream_build_url(&options, &url) == WF_OK);
    WF_CHECK(url && strcmp(url,
        "wss://jetstream.example/subscribe?wantedCollections=app.bsky.feed.post"
        "&wantedCollections=app.bsky.graph.%2A&wantedDids=did%3Aplc%3Aa%2Fb"
        "&cursor=1725519626134432&maxMessageSizeBytes=1000000"
        "&requireHello=true") == 0);
    free(url);

    static const unsigned char dictionary[] =
        "wolfram Jetstream test dictionary: did time_us kind commit record";
    options.compress = 1;
    options.zstd_dictionary = dictionary;
    options.zstd_dictionary_len = sizeof(dictionary) - 1;
    WF_CHECK(wf_jetstream_build_url(&options, &url) ==
             (wf_jetstream_zstd_supported() ? WF_OK : WF_ERR_INVALID_ARG));
    if (url) {
        WF_CHECK(strstr(url, "&compress=true") != NULL);
        free(url); url = NULL;
    }
    options.compress = 0;

    wf_jetstream_options_update update = {
        .wanted_collections = collections, .wanted_collections_count = 2,
        .wanted_dids = dids, .wanted_dids_count = 1,
        .max_message_size_bytes = 1000000,
    };
    char *update_json = NULL; size_t update_json_len = 0;
    WF_CHECK(wf_jetstream_options_update_json(&update, &update_json,
                                               &update_json_len) == WF_OK);
    WF_CHECK(update_json && strcmp(update_json,
        "{\"type\":\"options_update\",\"payload\":{"
        "\"wantedCollections\":[\"app.bsky.feed.post\",\"app.bsky.graph.*\"],"
        "\"wantedDids\":[\"did:plc:a/b\"],\"maxMessageSizeBytes\":1000000}}") == 0);
    WF_CHECK(update_json_len == strlen(update_json));
    free(update_json);
    update.wanted_collections_count = 101;
    WF_CHECK(wf_jetstream_options_update_json(&update, &update_json,
                                               &update_json_len) == WF_ERR_INVALID_ARG);
    update.wanted_collections_count = 0; update.wanted_collections = NULL;
    update.wanted_dids_count = 0; update.wanted_dids = NULL;
    WF_CHECK(wf_jetstream_options_update_json(&update, &update_json,
                                               &update_json_len) == WF_OK);
    WF_CHECK(strcmp(update_json,
        "{\"type\":\"options_update\",\"payload\":{\"wantedCollections\":[],"
        "\"wantedDids\":[],\"maxMessageSizeBytes\":1000000}}") == 0);
    free(update_json);

    options.wanted_collections_count = 101;
    WF_CHECK(wf_jetstream_build_url(&options, &url) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_jetstream_build_url(NULL, &url) == WF_ERR_INVALID_ARG);

    const char json[] =
        "{\"did\":\"did:plc:test\",\"time_us\":1725516666833633,"
        "\"kind\":\"commit\",\"commit\":{\"operation\":\"delete\"}}";
    wf_jetstream_event event = {0};
    WF_CHECK(wf_jetstream_event_parse(json, sizeof(json) - 1, &event) == WF_OK);
    WF_CHECK(event.kind == WF_JETSTREAM_EVENT_COMMIT);
    WF_CHECK(event.time_us == 1725516666833633LL);
    WF_CHECK(event.did && strcmp(event.did, "did:plc:test") == 0);
    WF_CHECK(event.json_len == sizeof(json) - 1);
    wf_jetstream_event_free(&event);
    WF_CHECK(event.did == NULL && event.json == NULL);

    WF_CHECK(wf_jetstream_event_parse("{}", 2, &event) == WF_ERR_PARSE);
    const char fractional[] =
        "{\"did\":\"did:plc:x\",\"kind\":\"commit\",\"time_us\":1.5}";
    WF_CHECK(wf_jetstream_event_parse(fractional, strlen(fractional), &event)
             == WF_ERR_PARSE);
#ifdef HAVE_LIBZSTD
    {
        size_t bound = ZSTD_compressBound(sizeof(json) - 1);
        void *compressed = malloc(bound);
        ZSTD_CCtx *context = ZSTD_createCCtx();
        WF_CHECK(compressed && context);
        size_t compressed_len = ZSTD_compress_usingDict(
            context, compressed, bound, json, sizeof(json) - 1,
            dictionary, sizeof(dictionary) - 1, 3);
        WF_CHECK(!ZSTD_isError(compressed_len));
        WF_CHECK(wf_jetstream_event_parse_zstd(
                     compressed, compressed_len, dictionary,
                     sizeof(dictionary) - 1, &event) == WF_OK);
        WF_CHECK(event.kind == WF_JETSTREAM_EVENT_COMMIT);
        WF_CHECK(event.time_us == 1725516666833633LL);
        wf_jetstream_event_free(&event);
        WF_CHECK(wf_jetstream_event_parse_zstd(
                     compressed, compressed_len, "wrong", 5, &event)
                 == WF_ERR_PARSE);
        ZSTD_freeCCtx(context);
        free(compressed);
    }
#else
    WF_CHECK(wf_jetstream_event_parse_zstd("x", 1, "dict", 4, &event)
             == WF_ERR_INVALID_ARG);
#endif
    WF_CHECK(wf_websocket_connect("https://not-websocket.example", NULL)
             == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_websocket_send_text(NULL, "{}", 2) == WF_ERR_INVALID_ARG);

    WF_TEST_SUMMARY();
}
