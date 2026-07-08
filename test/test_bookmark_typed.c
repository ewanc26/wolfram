/*
 * test_bookmark_typed.c — offline tests for app.bsky.bookmark typed parsers
 * and agent wrapper argument validation.
 */

#include "wolfram/bookmark_typed.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A real getBookmarks wire body: bookmarks are app.bsky.bookmark.defs#bookmarkView
 * objects with the record at-uri under `subject.uri` and `createdAt`. */
static const char *k_get_bookmarks_json =
    "{"
    "  \"bookmarks\": ["
    "    {"
    "      \"subject\": {"
    "        \"uri\": \"at://did:plc:abc123/app.bsky.feed.post/aaabbbccc\","
    "        \"cid\": \"bafyreighzkc234\""
    "      },"
    "      \"createdAt\": \"2026-01-02T03:04:05.000Z\""
    "    },"
    "    {"
    "      \"subject\": {"
    "        \"uri\": \"at://did:plc:abc123/app.bsky.feed.post/dddeeefff\","
    "        \"cid\": \"bafyreituvwx567\""
    "      },"
    "      \"createdAt\": \"2026-02-03T04:05:06.000Z\""
    "    }"
    "  ],"
    "  \"cursor\": \"next-page-cursor\""
    "}";

static const char *k_malformed_json = "{ this is not valid json ";

int main(void) {
    /* ── parse: NULL inputs rejected ── */
    wf_bookmark_list list = {0};
    WF_CHECK(wf_bookmark_parse_list(NULL, 0, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_bookmark_parse_list(k_get_bookmarks_json,
                                    strlen(k_get_bookmarks_json),
                                    NULL) == WF_ERR_INVALID_ARG);

    /* ── parse: malformed JSON rejected ── */
    WF_CHECK(wf_bookmark_parse_list(k_malformed_json, strlen(k_malformed_json),
                                    &list) == WF_ERR_PARSE);

    /* ── parse: happy path ── */
    wf_status st = wf_bookmark_parse_list(k_get_bookmarks_json,
                                           strlen(k_get_bookmarks_json),
                                           &list);
    WF_CHECK(st == WF_OK);
    WF_CHECK(list.count == 2);
    WF_CHECK(list.items != NULL);
    WF_CHECK(list.items[0].uri != NULL);
    WF_CHECK(strcmp(list.items[0].uri,
                    "at://did:plc:abc123/app.bsky.feed.post/aaabbbccc") == 0);
    WF_CHECK(list.items[0].created_at != NULL);
    WF_CHECK(strcmp(list.items[0].created_at,
                    "2026-01-02T03:04:05.000Z") == 0);
    WF_CHECK(strcmp(list.items[1].uri,
                    "at://did:plc:abc123/app.bsky.feed.post/dddeeefff") == 0);
    WF_CHECK(list.cursor != NULL);
    WF_CHECK(strcmp(list.cursor, "next-page-cursor") == 0);

    wf_bookmark_list_free(&list);
    WF_CHECK(list.count == 0 && list.items == NULL && list.cursor == NULL);

    /* ── agent wrappers: NULL / invalid args rejected ── */
    char *dummy_agent = (char *)0x1; /* non-NULL so uri validation runs */
    WF_CHECK(wf_agent_create_bookmark(NULL, NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_create_bookmark(NULL,
                                      "at://did:plc:x/app.bsky.feed.post/y",
                                      NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_create_bookmark((wf_agent *)dummy_agent, "not-a-uri",
                                      NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_delete_bookmark(NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_delete_bookmark((wf_agent *)dummy_agent, "not-a-uri") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_bookmarks_typed(NULL, 0, NULL, &list) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_bookmarks_typed((wf_agent *)dummy_agent, 0, NULL,
                                          NULL) == WF_ERR_INVALID_ARG);

    WF_TEST_SUMMARY();
}
