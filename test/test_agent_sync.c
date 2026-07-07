/*
 * test_agent_sync.c — unit tests for agent sync and listRecords wrappers.
 *
 * Tests input validation only (no network calls).
 */

#include "wolfram/agent.h"
#include "wolfram/session.h"
#include "test.h"

#include <string.h>
#include <stdlib.h>

int main(void) {
    /* ── listRecords ───────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_list_records(NULL, "app.bsky.feed.post", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_list_records(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_records(agent, "", 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);

        /* not logged in */
        WF_CHECK(wf_agent_list_records(agent, "app.bsky.feed.post", 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);

        /* invalid NSID */
        wf_session_data fake_data = {0};
        wf_agent_resume(agent, &fake_data);
        WF_CHECK(wf_agent_list_records(agent, "not-a-valid-nsid!!!", 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── sync.getBlob ──────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_sync_get_blob(NULL, "did:plc:test", "bafyxx", NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_sync_get_blob(agent, NULL, "bafyxx", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blob(agent, "not-a-did", "bafyxx", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blob(agent, "did:plc:test", NULL, &res)
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── sync.getBlocks ────────────────────────────────────────────── */
    {
        const char *cids[] = {"bafyxx1", "bafyxx2"};

        WF_CHECK(wf_agent_sync_get_blocks(NULL, "did:plc:test", cids, 2, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_sync_get_blocks(agent, NULL, cids, 2, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blocks(agent, "not-a-did", cids, 2, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blocks(agent, "did:plc:test", NULL, 0, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blocks(agent, "did:plc:test", cids, 0, &res)
                 == WF_ERR_INVALID_ARG);

        /* empty CID in the array */
        const char *bad_cids[] = {"bafyxx1", ""};
        WF_CHECK(wf_agent_sync_get_blocks(agent, "did:plc:test", bad_cids, 2, &res)
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── sync.getRecord ────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_sync_get_record(NULL, "did:plc:test",
                                           "app.bsky.feed.post", "3jkl0pp", NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_sync_get_record(agent, NULL,
                                           "app.bsky.feed.post", "3jkl0pp", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record(agent, "not-a-did",
                                           "app.bsky.feed.post", "3jkl0pp", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record(agent, "did:plc:test",
                                           NULL, "3jkl0pp", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record(agent, "did:plc:test",
                                           "not-a-nsid", "3jkl0pp", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record(agent, "did:plc:test",
                                           "app.bsky.feed.post", NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record(agent, "did:plc:test",
                                           "app.bsky.feed.post", "", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record(agent, "did:plc:test",
                                           "app.bsky.feed.post", "invalid rkey!", &res)
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── sync.listBlobs ────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_sync_list_blobs(NULL, "did:plc:test", 0, NULL, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_sync_list_blobs(agent, NULL, 0, NULL, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_list_blobs(agent, "not-a-did", 0, NULL, NULL, &res)
                 == WF_ERR_INVALID_ARG);

        /* invalid TID for since */
        WF_CHECK(wf_agent_sync_list_blobs(agent, "did:plc:test", 0, NULL,
                                           "not-a-tid", &res)
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
