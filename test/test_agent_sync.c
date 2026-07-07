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

    /* ── mute / unmute ─────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_mute(NULL, "did:plc:test") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute(NULL, "did:plc:test") == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        /* not logged in */
        WF_CHECK(wf_agent_mute(agent, "did:plc:test") == WF_ERR_INVALID_ARG);

        /* invalid actor */
        wf_session_data fake_data = {0};
        wf_agent_resume(agent, &fake_data);
        WF_CHECK(wf_agent_mute(agent, "not-an-identifier") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute(agent, "not-an-identifier") == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── searchPosts ───────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_search_posts(NULL, "test", 0, NULL, NULL, NULL, NULL, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_search_posts(agent, NULL, 0, NULL, NULL, NULL, NULL, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_search_posts(agent, "", 0, NULL, NULL, NULL, NULL, NULL, &res)
                 == WF_ERR_INVALID_ARG);

        /* invalid author format */
        WF_CHECK(wf_agent_search_posts(agent, "hello", 0, NULL, NULL, NULL, "not-valid", NULL, &res) == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── getActorLikes ─────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_get_actor_likes(NULL, "did:plc:test", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_get_actor_likes(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_actor_likes(agent, "not-valid", 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── getLikes ──────────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_get_likes(NULL, "at://did:plc:test/app.bsky.feed.post/3jkl0pp", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_get_likes(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_likes(agent, "not-a-uri", 0, NULL, &res)
                 == WF_ERR_PARSE);

        wf_agent_free(agent);
    }

    /* ── getLikes (lexicon) ──────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_get_likes_lex(NULL, "at://did:plc:test/app.bsky.feed.post/3jkl0pp", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_get_likes_lex(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_likes_lex(agent, "not-a-uri", 0, NULL, &res)
                     == WF_ERR_PARSE);

            wf_agent_free(agent);

        /* ── getQuotes (lexicon) ─────────────────────────────────────── */
        {
            WF_CHECK(wf_agent_get_quotes_lex(NULL, "at://did:plc:test/app.bsky.feed.post/3jkl0pp", 0, NULL, NULL)
                     == WF_ERR_INVALID_ARG);
            wf_agent *agent = wf_agent_new("https://example.com");
            WF_CHECK(agent != NULL);
            wf_response res = {0};
            WF_CHECK(wf_agent_get_quotes_lex(agent, NULL, 0, NULL, &res)
                     == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_quotes_lex(agent, "not-a-uri", 0, NULL, &res)
                     == WF_ERR_PARSE);
            wf_agent_free(agent);
        }

        /* ── getListFeed (lexicon) ─────────────────────────────────────── */
        {
            WF_CHECK(wf_agent_get_list_feed_lex(NULL, "at://did:plc:test/app.bsky.graph.list/3jkl0pp", 0, NULL, NULL)
                     == WF_ERR_INVALID_ARG);
            wf_agent *agent = wf_agent_new("https://example.com");
            WF_CHECK(agent != NULL);
            wf_response res = {0};
            WF_CHECK(wf_agent_get_list_feed_lex(agent, NULL, 0, NULL, &res)
                     == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_list_feed_lex(agent, "not-a-uri", 0, NULL, &res)
                     == WF_ERR_PARSE);
            wf_agent_free(agent);
        }

        /* ── getFeed (lexicon) ─────────────────────────────────────── */
        {
            WF_CHECK(wf_agent_get_feed_lex(NULL, "at://did:plc:test/app.bsky.feed.generator/slug", 0, NULL, NULL)
                     == WF_ERR_INVALID_ARG);
            wf_agent *agent = wf_agent_new("https://example.com");
            WF_CHECK(agent != NULL);
            wf_response res = {0};
            WF_CHECK(wf_agent_get_feed_lex(agent, NULL, 0, NULL, &res)
                     == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_feed_lex(agent, "not-a-uri", 0, NULL, &res)
                     == WF_ERR_PARSE);
            wf_agent_free(agent);
        }

        /* ── getActorFeeds (lexicon) ─────────────────────────────────────── */
        {
            WF_CHECK(wf_agent_get_actor_feeds_lex(NULL, "did:plc:test", 0, NULL, NULL)
                     == WF_ERR_INVALID_ARG);
            wf_agent *agent = wf_agent_new("https://example.com");
            WF_CHECK(agent != NULL);
            wf_response res = {0};
            WF_CHECK(wf_agent_get_actor_feeds_lex(agent, NULL, 0, NULL, &res)
                     == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_actor_feeds_lex(agent, "not-valid", 0, NULL, &res)
                     == WF_ERR_INVALID_ARG);
            wf_agent_free(agent);
        }

        }

        /* ── getTimeline (lexicon) ─────────────────────────────────────── */
        {
            WF_CHECK(wf_agent_get_timeline_lex(NULL, 10, NULL, NULL) == WF_ERR_INVALID_ARG);
        }

        /* ── getAuthorFeed (lexicon) ─────────────────────────────────────── */
        {
            WF_CHECK(wf_agent_get_author_feed_lex(NULL, "did:plc:test", 0, NULL, NULL, NULL) == WF_ERR_INVALID_ARG);
            wf_agent *agent = wf_agent_new("https://example.com");
            WF_CHECK(agent != NULL);
            wf_response res = {0};
            WF_CHECK(wf_agent_get_author_feed_lex(agent, "did:plc:test", 0, NULL, NULL, &res) == WF_OK);
            wf_agent_free(agent);
        }

        /* ── searchPosts (lexicon) ─────────────────────────────────────── */
        {
            WF_CHECK(wf_agent_search_posts_lex(NULL, "test", 0, NULL, NULL, NULL, NULL, NULL, NULL) == WF_ERR_INVALID_ARG);
            wf_agent *agent = wf_agent_new("https://example.com");
            WF_CHECK(agent != NULL);
            wf_response res = {0};
            WF_CHECK(wf_agent_search_posts_lex(agent, "test", 0, NULL, NULL, NULL, NULL, NULL, &res) == WF_OK);
            wf_agent_free(agent);
        }

        /* ── getRepostedBy ─────────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_get_reposted_by(NULL, "at://did:plc:test/app.bsky.feed.post/3jkl0pp", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_get_reposted_by(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_reposted_by(agent, "not-a-uri", 0, NULL, &res)
                 == WF_ERR_PARSE);

        wf_agent_free(agent);
    }

    /* ── graph queries ─────────────────────────────────────────────── */
    {
        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_get_blocks(NULL, 0, NULL, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_mutes(NULL, 0, NULL, NULL) == WF_ERR_INVALID_ARG);

        /* getKnownFollowers */
        WF_CHECK(wf_agent_get_known_followers(NULL, "did:plc:test", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_known_followers(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_known_followers(agent, "not-valid", 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);

        /* getRelationships */
        const char *others[] = {"did:plc:other"};
        WF_CHECK(wf_agent_get_relationships(NULL, "did:plc:test", others, 1, NULL)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_relationships(agent, NULL, others, 1, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_relationships(agent, "not-a-did", others, 1, &res)
                 == WF_ERR_INVALID_ARG);

        /* getList */
        WF_CHECK(wf_agent_get_list(NULL, "at://did:plc:test/app.bsky.graph.list/3jkl0pp", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_list(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_list(agent, "not-a-uri", 0, NULL, &res)
                 == WF_ERR_PARSE);

        /* getLists */
        WF_CHECK(wf_agent_get_lists(NULL, "did:plc:test", 0, NULL, NULL)
                  == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_lists(agent, NULL, 0, NULL, &res)
                  == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_lists(agent, "not-valid", 0, NULL, &res)
                  == WF_ERR_INVALID_ARG);

        /* getSuggestedFollowsByActor */
        WF_CHECK(wf_agent_get_suggested_follows_by_actor(NULL, "did:plc:test", NULL)
                  == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_suggested_follows_by_actor(agent, NULL, NULL)
                  == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_suggested_follows_by_actor(agent, "not-valid", &res)
                  == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── searchActorsTypeahead ─────────────────────────────────────── */
    {
        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);
        wf_response res = {0};
        WF_CHECK(wf_agent_search_actors_typeahead(NULL, "bob", 10, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_search_actors_typeahead(agent, NULL, 10, NULL) == WF_ERR_INVALID_ARG);
        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
