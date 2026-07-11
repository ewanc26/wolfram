#include "wolfram/jetstream.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    {
        const char json[] =
            "{\"did\":\"did:plc:abc\",\"time_us\":1700000000000000,"
            "\"seq\":123,\"kind\":\"commit\",\"commit\":{"
            "\"operation\":\"create\","
            "\"collection\":\"app.bsky.feed.post\",\"rkey\":\"abc\","
            "\"record\":{\"$type\":\"app.bsky.feed.post\",\"text\":\"hello world\"},"
            "\"cid\":\"bafyreihxojphs4rlytr3jfdj5ztqtsb3cywwmzjahc5q6wzlw4ourzphtu\","
            "\"rev\":\"3loabcmyt2sld\"}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_COMMIT);
        WF_CHECK(ev.did && strcmp(ev.did, "did:plc:abc") == 0);
        WF_CHECK(ev.seq == 123);
        WF_CHECK(ev.time_us == 1700000000000000);
        WF_CHECK(ev.commit.operation == WF_JETSTREAM_COMMIT_CREATE);
        WF_CHECK(ev.commit.collection &&
                 strcmp(ev.commit.collection, "app.bsky.feed.post") == 0);
        WF_CHECK(ev.commit.rkey && strcmp(ev.commit.rkey, "abc") == 0);
        WF_CHECK(ev.commit.has_record &&
                 cJSON_IsObject(ev.commit.record));
        WF_CHECK(ev.commit.cid &&
                 strcmp(ev.commit.cid,
                        "bafyreihxojphs4rlytr3jfdj5ztqtsb3cywwmzjahc5q6wzlw4ourzphtu") == 0);
        WF_CHECK(ev.commit.rev &&
                 strcmp(ev.commit.rev, "3loabcmyt2sld") == 0);
        wf_jetstream_event_typed_free(&ev);
        WF_CHECK(ev.did == NULL && ev.commit.record == NULL);
    }

    {
        const char json[] =
            "{\"did\":\"did:plc:abc\",\"time_us\":1700000000000001,"
            "\"seq\":124,\"kind\":\"commit\",\"commit\":{"
            "\"operation\":\"delete\","
            "\"collection\":\"app.bsky.feed.post\",\"rkey\":\"abc\","
            "\"cid\":\"bafyreihxojphs4rlytr3jfdj5ztqtsb3cywwmzjahc5q6wzlw4ourzphtu\","
            "\"rev\":\"3loabcmyt2sle\"}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_COMMIT);
        WF_CHECK(ev.commit.operation == WF_JETSTREAM_COMMIT_DELETE);
        WF_CHECK(ev.commit.collection &&
                 strcmp(ev.commit.collection, "app.bsky.feed.post") == 0);
        WF_CHECK(ev.commit.rkey && strcmp(ev.commit.rkey, "abc") == 0);
        WF_CHECK(ev.commit.has_record == 0 && ev.commit.record == NULL);
        WF_CHECK(ev.commit.cid != NULL);
        wf_jetstream_event_typed_free(&ev);
    }

    {
        const char json[] =
            "{\"did\":\"did:plc:ident\",\"time_us\":1700000000000000,"
            "\"seq\":7,\"kind\":\"identity\","
            "\"identity\":{\"handle\":\"alice.bsky.social\","
            "\"did\":\"did:plc:ident\",\"seq\":7,"
            "\"time\":\"2023-11-14T12:00:00Z\",\"prev\":\"\"}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_IDENTITY);
        WF_CHECK(ev.did && strcmp(ev.did, "did:plc:ident") == 0);
        WF_CHECK(ev.seq == 7);
        WF_CHECK(ev.identity.handle &&
                 strcmp(ev.identity.handle, "alice.bsky.social") == 0);
        wf_jetstream_event_typed_free(&ev);
    }

    {
        const char json[] =
            "{\"did\":\"did:plc:acct\",\"time_us\":1700000000000000,"
            "\"seq\":9,\"kind\":\"account\","
            "\"account\":{\"active\":true,\"did\":\"did:plc:acct\",\"seq\":9,"
            "\"time\":\"2023-11-14T12:00:00Z\",\"status\":null}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_ACCOUNT);
        WF_CHECK(ev.did && strcmp(ev.did, "did:plc:acct") == 0);
        WF_CHECK(ev.seq == 9);
        WF_CHECK(ev.account.active == 1);
        WF_CHECK(ev.account.status == NULL);
        wf_jetstream_event_typed_free(&ev);
    }

    {
        const char json[] =
            "{\"did\":\"did:plc:fork\",\"time_us\":1700000000000000,"
            "\"seq\":42,\"kind\":\"fork\",\"fork\":{"
            "\"seq\":42,\"did\":\"did:plc:fork\","
            "\"time\":\"2024-09-05T10:11:06.833Z\","
            "\"rev\":\"3-fork\",\"tooBig\":false}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_FORK);
        WF_CHECK(ev.sync.kind == WF_JETSTREAM_EVENT_FORK);
        WF_CHECK(ev.sync.seq && strcmp(ev.sync.seq, "42") == 0);
        WF_CHECK(ev.sync.did && strcmp(ev.sync.did, "did:plc:fork") == 0);
        WF_CHECK(ev.sync.rev && strcmp(ev.sync.rev, "3-fork") == 0);
        WF_CHECK(ev.sync.too_big == 0);
        wf_jetstream_event_typed_free(&ev);
    }

    {
        const char json[] = "{\"did\":123,\"time_us\":1,\"seq\":1,\"kind\":\"commit\"}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_ERR_PARSE);
        WF_CHECK(ev.did == NULL && ev.kind == WF_JETSTREAM_EVENT_UNKNOWN);
    }

    WF_TEST_SUMMARY();
}
