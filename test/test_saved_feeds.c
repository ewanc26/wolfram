/*
 * test_saved_feeds.c -- wf_saved_feeds_parse on the preference shapes real
 * accounts return, and wf_agent_get_saved_feeds against the mock PDS.
 */

#include "wolfram/saved_feeds.h"

#include "test.h"

#include <stdio.h>
#include <string.h>

#define FEED(name) "at://did:plc:x/app.bsky.feed.generator/" name

int main(void) {
    wf_saved_feed f[4];
    size_t n = 99;

    /* V2: only the items of type "feed", in order; timelines and lists are not
     * custom feeds, an empty value is skipped, other preference types ignored.
     */
    const char *v2 =
        "[{\"$type\":\"app.bsky.actor.defs#adultContentPref\",\"enabled\":"
        "false},"
        "{\"$type\":\"app.bsky.actor.defs#savedFeedsPrefV2\",\"items\":["
        "{\"type\":\"timeline\",\"value\":\"following\",\"pinned\":true},"
        "{\"type\":\"feed\",\"value\":\"" FEED(
            "whats-hot") "\",\"pinned\":false},"
                         "{\"type\":\"list\",\"value\":\"at://did:plc:x/"
                         "app.bsky.graph.list/l\"},"
                         "{\"type\":\"feed\",\"value\":\"\"},"
                         "{\"type\":\"feed\",\"value\":\"" FEED(
                             "cats") "\"}]}]";
    WF_CHECK(wf_saved_feeds_parse(v2, f, 4, &n) == WF_OK);
    WF_CHECK(n == 2);
    WF_CHECK(strcmp(f[0].uri, FEED("whats-hot")) == 0);
    WF_CHECK(strcmp(f[0].name, "whats-hot") ==
             0); /* the record key, until named */
    WF_CHECK(strcmp(f[1].uri, FEED("cats")) == 0);

    /* The cap is honoured. */
    WF_CHECK(wf_saved_feeds_parse(v2, f, 1, &n) == WF_OK && n == 1);

    /* An older account has only the V1 list. */
    const char *v1 =
        "[{\"$type\":\"app.bsky.actor.defs#savedFeedsPref\",\"pinned\":[],"
        "\"saved\":[\"" FEED("old") "\",\"\"]}]";
    WF_CHECK(wf_saved_feeds_parse(v1, f, 4, &n) == WF_OK);
    WF_CHECK(n == 1 && strcmp(f[0].uri, FEED("old")) == 0);

    /* V2 with nothing in it falls back to V1 when both are present. */
    const char *both =
        "[{\"$type\":\"app.bsky.actor.defs#savedFeedsPrefV2\",\"items\":[]},"
        "{\"$type\":\"app.bsky.actor.defs#savedFeedsPref\",\"saved\":[\"" FEED(
            "v1") "\"]}]";
    WF_CHECK(wf_saved_feeds_parse(both, f, 4, &n) == WF_OK && n == 1);

    /* No saved feeds at all is not an error. */
    WF_CHECK(wf_saved_feeds_parse("[]", f, 4, &n) == WF_OK && n == 0);

    /* Not an array, not JSON, and bad arguments. */
    WF_CHECK(wf_saved_feeds_parse("{}", f, 4, &n) == WF_ERR_PARSE && n == 0);
    WF_CHECK(wf_saved_feeds_parse("nope", f, 4, &n) == WF_ERR_PARSE);
    WF_CHECK(wf_saved_feeds_parse(NULL, f, 4, &n) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_saved_feeds_parse("[]", NULL, 4, &n) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_saved_feeds_parse("[]", f, 4, NULL) == WF_ERR_INVALID_ARG);

    /* A very long URI is cut rather than overrunning. */
    char big[600];
    char json[800];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    snprintf(json, sizeof json,
             "[{\"$type\":\"x#savedFeedsPrefV2\",\"items\":[{\"type\":\"feed\","
             "\"value\":\"%s\"}]}]",
             big);
    WF_CHECK(wf_saved_feeds_parse(json, f, 4, &n) == WF_OK && n == 1);
    WF_CHECK(strlen(f[0].uri) == WF_SAVED_FEED_URI_MAX - 1);

    WF_CHECK(wf_agent_get_saved_feeds(NULL, f, 4, &n) == WF_ERR_INVALID_ARG);

    WF_TEST_SUMMARY();
}
