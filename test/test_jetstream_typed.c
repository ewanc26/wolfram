#include "wolfram/jetstream.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    {
        const char json[] =
            "{\"did\":\"did:plc:repo\",\"time_us\":1725516666833633,"
            "\"kind\":\"commit\",\"commit\":{"
            "\"seq\":123,\"tooBig\":false,"
            "\"blocks\":\"mF4CAgI\","
            "\"repo\":\"did:plc:repo\",\"rev\":\"3-abc\",\"since\":null,"
            "\"time\":\"2024-09-05T10:11:06.833Z\","
            "\"ops\":["
            "{\"action\":\"create\",\"path\":\"app.bsky.feed.post/aa1\","
            "\"cid\":\"bafycid1\",\"prev\":null,"
            "\"value\":{\"$type\":\"app.bsky.feed.post\",\"text\":\"hi\"}},"
            "{\"action\":\"update\",\"path\":\"app.bsky.graph.follow/bb2\","
            "\"cid\":\"bafycid2\",\"prev\":\"bafyprev2\","
            "\"value\":{\"$type\":\"app.bsky.graph.follow\"}}"
            "]}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_COMMIT);
        WF_CHECK(ev.did && strcmp(ev.did, "did:plc:repo") == 0);
        WF_CHECK(ev.commit.seq && strcmp(ev.commit.seq, "123") == 0);
        WF_CHECK(ev.commit.repo && strcmp(ev.commit.repo, "did:plc:repo") == 0);
        WF_CHECK(ev.commit.did && strcmp(ev.commit.did, "did:plc:repo") == 0);
        WF_CHECK(ev.commit.repo_rev &&
                 strcmp(ev.commit.repo_rev, "3-abc") == 0);
        WF_CHECK(ev.commit.too_big == 0);
        WF_CHECK(ev.commit.blocks && strcmp(ev.commit.blocks, "mF4CAgI") == 0);
        WF_CHECK(ev.commit.time &&
                 strcmp(ev.commit.time, "2024-09-05T10:11:06.833Z") == 0);
        WF_CHECK(ev.commit.op_count == 2);
        WF_CHECK(ev.commit.ops[0].action &&
                 strcmp(ev.commit.ops[0].action, "create") == 0);
        WF_CHECK(ev.commit.ops[0].path &&
                 strcmp(ev.commit.ops[0].path, "app.bsky.feed.post/aa1") == 0);
        WF_CHECK(ev.commit.ops[0].cid &&
                 strcmp(ev.commit.ops[0].cid, "bafycid1") == 0);
        WF_CHECK(ev.commit.ops[0].prev == NULL);
        WF_CHECK(ev.commit.ops[0].has_value &&
                 cJSON_IsObject(ev.commit.ops[0].value));
        WF_CHECK(ev.commit.ops[1].action &&
                 strcmp(ev.commit.ops[1].action, "update") == 0);
        WF_CHECK(ev.commit.ops[1].prev &&
                 strcmp(ev.commit.ops[1].prev, "bafyprev2") == 0);
        WF_CHECK(ev.commit.ops[1].has_value);
        wf_jetstream_event_typed_free(&ev);
        WF_CHECK(ev.did == NULL && ev.commit.ops == NULL);
    }

    {
        const char json[] =
            "{\"did\":\"did:plc:ident\",\"time_us\":1725516666833633,"
            "\"kind\":\"identity\",\"identity\":{"
            "\"seq\":7,\"did\":\"did:plc:ident\","
            "\"time\":\"2024-09-05T10:11:06.833Z\","
            "\"handle\":\"alice.bsky.social\"}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_IDENTITY);
        WF_CHECK(ev.identity.seq && strcmp(ev.identity.seq, "7") == 0);
        WF_CHECK(ev.identity.did &&
                 strcmp(ev.identity.did, "did:plc:ident") == 0);
        WF_CHECK(ev.identity.handle &&
                 strcmp(ev.identity.handle, "alice.bsky.social") == 0);
        WF_CHECK(ev.identity.time &&
                 strcmp(ev.identity.time, "2024-09-05T10:11:06.833Z") == 0);
        wf_jetstream_event_typed_free(&ev);
    }

    {
        const char json[] =
            "{\"did\":\"did:plc:acct\",\"time_us\":1725516666833633,"
            "\"kind\":\"account\",\"account\":{"
            "\"seq\":9,\"did\":\"did:plc:acct\","
            "\"time\":\"2024-09-05T10:11:06.833Z\","
            "\"active\":true,\"status\":null}}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_OK);
        WF_CHECK(ev.kind == WF_JETSTREAM_EVENT_ACCOUNT);
        WF_CHECK(ev.account.seq && strcmp(ev.account.seq, "9") == 0);
        WF_CHECK(ev.account.active == 1);
        WF_CHECK(ev.account.status == NULL);
        wf_jetstream_event_typed_free(&ev);
    }

    {
        const char json[] =
            "{\"did\":\"did:plc:fork\",\"time_us\":1725516666833633,"
            "\"kind\":\"fork\",\"fork\":{"
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
        const char json[] = "{\"did\":123,\"time_us\":1,\"kind\":\"commit\"}";
        wf_jetstream_event_typed ev = {0};
        WF_CHECK(wf_jetstream_event_parse_typed(json, sizeof(json) - 1, &ev)
                 == WF_ERR_PARSE);
        WF_CHECK(ev.did == NULL && ev.kind == WF_JETSTREAM_EVENT_UNKNOWN);
    }

    WF_TEST_SUMMARY();
}
