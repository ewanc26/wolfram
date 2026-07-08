/*
 * test_label_query.c — offline tests for the label query/get typed parser
 * and the agent convenience wrapper's input validation.
 *
 * No network: the parser operates on a hardcoded queryLabels body, and the
 * agent wrapper is only exercised for its NULL/empty validation branches
 * (which return before any network I/O).
 */

#include "wolfram/label.h"
#include "wolfram/label_typed.h"
#include "wolfram/moderation.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Representative com.atproto.label.queryLabels response body. */
static const char *k_label_json =
    "{"
    "  \"cursor\": \"cur-987654\","
    "  \"labels\": ["
    "    {"
    "      \"src\": \"did:plc:labeler\","
    "      \"uri\": \"at://did:plc:alice/app.bsky.feed.post/aaa111\","
    "      \"val\": \"!hide\","
    "      \"cts\": \"2024-01-02T03:04:05.000Z\""
    "    },"
    "    {"
    "      \"src\": \"did:plc:labeler\","
    "      \"uri\": \"at://did:plc:bob/app.bsky.actor.profile/self\","
    "      \"val\": \"org.labeler.porn\","
    "      \"cts\": \"2024-01-03T04:05:06.000Z\""
    "    },"
    "    {"
    "      \"uri\": \"at://did:plc:carol/app.bsky.feed.post/ccc333\","
    "      \"val\": \"spam\","
    "      \"cts\": \"2024-01-04T05:06:07.000Z\""
    "    }"
    "  ]"
    "}";

int main(void) {
    /* Input validation. */
    wf_mod_label *labels = NULL;
    size_t count = 0;
    WF_CHECK(wf_label_parse_query(NULL, 0, &labels, &count) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_label_parse_query(k_label_json, strlen(k_label_json), NULL, &count) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_label_parse_query(k_label_json, strlen(k_label_json), &labels, NULL) ==
             WF_ERR_INVALID_ARG);

    /* Malformed JSON. */
    WF_CHECK(wf_label_parse_query("{not json", 8, &labels, &count) == WF_ERR_INVALID_ARG);

    /* Missing labels array. */
    WF_CHECK(wf_label_parse_query("{\"cursor\":\"x\"}", 14, &labels, &count) ==
             WF_ERR_PARSE);

    /* Parse a well-formed body. */
    wf_status st = wf_label_parse_query(k_label_json, strlen(k_label_json),
                                        &labels, &count);
    WF_CHECK(st == WF_OK);
    WF_CHECK(count == 3);
    if (st == WF_OK && count == 3) {
        WF_CHECK(strcmp(labels[0].src, "did:plc:labeler") == 0);
        WF_CHECK(strcmp(labels[0].uri,
                        "at://did:plc:alice/app.bsky.feed.post/aaa111") == 0);
        WF_CHECK(strcmp(labels[0].val, "!hide") == 0);
        WF_CHECK(strcmp(labels[0].cts, "2024-01-02T03:04:05.000Z") == 0);

        WF_CHECK(strcmp(labels[1].val, "org.labeler.porn") == 0);
        WF_CHECK(strcmp(labels[1].uri,
                        "at://did:plc:bob/app.bsky.actor.profile/self") == 0);

        /* Third entry had no src; parser still yields the label. */
        WF_CHECK(labels[2].src == NULL);
        WF_CHECK(strcmp(labels[2].val, "spam") == 0);
        WF_CHECK(strcmp(labels[2].cts, "2024-01-04T05:06:07.000Z") == 0);

        wf_mod_labels_free(labels, count);
        WF_CHECK(1); /* free is a no-op on NULL; reaching here is success */
    }
    labels = NULL;
    count = 0;

    /* Agent wrapper input validation (no network: rejected before I/O). */
    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);

    const char *dummy_uri = "at://did:plc:alice/app.bsky.feed.post/aaa111";
    wf_mod_label *out = NULL;
    size_t out_count = 0;
    WF_CHECK(wf_agent_query_labels_typed(NULL, &dummy_uri, 1, NULL, 0,
                                         &out, &out_count) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_query_labels_typed(agent, NULL, 0, NULL, 0,
                                         &out, &out_count) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_query_labels_typed(agent, &dummy_uri, 1, NULL, 0,
                                         NULL, &out_count) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_query_labels_typed(agent, &dummy_uri, 1, NULL, 0,
                                         &out, NULL) == WF_ERR_INVALID_ARG);

    wf_agent_free(agent);

    WF_TEST_SUMMARY();
}
