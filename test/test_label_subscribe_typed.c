/*
 * test_label_subscribe_typed.c — offline tests for the
 * com.atproto.label.subscribeLabels typed-wrapper family:
 *   - wf_label_to_mod_label      (single #label -> owned wf_mod_label)
 *   - wf_label_parse_subscribe  (#labels frame -> owned wf_mod_label[])
 *   - wf_agent_subscribe_labels_typed (input validation only; the live
 *     subscription path is exercised lazily and not hit here — the wrapper
 *     is rejected by its NULL/empty validation branches before any network I/O)
 *
 * No network: frames are hardcoded JSON and the agent wrapper is only driven
 * through its validation branches. An unreachable service URL is used to prove
 * the real delegation path returns promptly without hanging (the initial
 * WebSocket connect fails fast).
 */

#include "wolfram/label.h"
#include "wolfram/label_typed.h"
#include "wolfram/moderation.h"
#include "wolfram/agent.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Free the owned fields of a single stack-allocated wf_mod_label. wf_mod_labels_free
 * would also free the (stack) outer array, so we free only the strings here. */
static void free_one(wf_mod_label *m) {
    if (!m) return;
    free(m->src);
    free(m->uri);
    free(m->val);
    free(m->cts);
    free(m->cid);
    free(m->exp);
    memset(m, 0, sizeof(*m));
}

/* A representative subscribeLabels `#labels` frame (JSON streamed over WS). */
static const char *k_frame_json =
    "{"
    "  \"$type\": \"#labels\","
    "  \"seq\": 17,"
    "  \"labels\": ["
    "    {"
    "      \"src\": \"did:plc:z72i7hdynmk6r22z27h6tvur\","
    "      \"uri\": \"at://did:plc:alice/app.bsky.feed.post/aaa111\","
    "      \"val\": \"nsfw\","
    "      \"cts\": \"2024-10-16T00:00:00Z\","
    "      \"neg\": false"
    "    },"
    "    {"
    "      \"src\": \"did:plc:z72i7hdynmk6r22z27h6tvur\","
    "      \"uri\": \"at://did:plc:bob/app.bsky.actor.profile/self\","
    "      \"val\": \"!hide\","
    "      \"cts\": \"2024-10-16T01:00:00Z\","
    "      \"cid\": \"bafyreig\","
    "      \"ver\": 1,"
    "      \"exp\": \"2099-01-01T00:00:00Z\""
    "    },"
    "    {"
    "      \"src\": \"did:plc:z72i7hdynmk6r22z27h6tvur\","
    "      \"uri\": \"at://did:plc:carol/app.bsky.feed.post/ccc333\","
    "      \"val\": \"spam\","
    "      \"cts\": \"2024-10-16T02:00:00Z\","
    "      \"neg\": true"
    "    }"
    "  ]"
    "}";

static void test_to_mod_label(void) {
    /* Build a wf_label by round-tripping the frame through the decoder. */
    wf_label_message msg = {0};
    WF_CHECK(wf_label_message_parse(k_frame_json, strlen(k_frame_json),
                                  &msg) == WF_OK);
    WF_CHECK(msg.type == WF_LABEL_MESSAGE_LABELS);
    WF_CHECK(msg.data.labels.count == 3);

    /* Positive label with no optional fields. */
    wf_mod_label m0;
    memset(&m0, 0, sizeof(m0));
    WF_CHECK(wf_label_to_mod_label(&msg.data.labels.items[0], &m0) == WF_OK);
    WF_CHECK(strcmp(m0.src, "did:plc:z72i7hdynmk6r22z27h6tvur") == 0);
    WF_CHECK(strcmp(m0.uri,
                    "at://did:plc:alice/app.bsky.feed.post/aaa111") == 0);
    WF_CHECK(strcmp(m0.val, "nsfw") == 0);
    WF_CHECK(strcmp(m0.cts, "2024-10-16T00:00:00Z") == 0);
    WF_CHECK(m0.neg == 0);
    WF_CHECK(m0.has_cid == 0);
    WF_CHECK(m0.cid == NULL);
    WF_CHECK(m0.ver == 0);
    WF_CHECK(m0.exp == NULL);
    free_one(&m0);

    /* Label with cid/ver/exp present. */
    wf_mod_label m1;
    memset(&m1, 0, sizeof(m1));
    WF_CHECK(wf_label_to_mod_label(&msg.data.labels.items[1], &m1) == WF_OK);
    WF_CHECK(strcmp(m1.val, "!hide") == 0);
    WF_CHECK(m1.has_cid == 1);
    WF_CHECK(strcmp(m1.cid, "bafyreig") == 0);
    WF_CHECK(m1.ver == 1);
    WF_CHECK(m1.has_cid == 1);
    WF_CHECK(strcmp(m1.exp, "2099-01-01T00:00:00Z") == 0);
    free_one(&m1);

    /* Negation label. */
    wf_mod_label m2;
    memset(&m2, 0, sizeof(m2));
    WF_CHECK(wf_label_to_mod_label(&msg.data.labels.items[2], &m2) == WF_OK);
    WF_CHECK(strcmp(m2.val, "spam") == 0);
    WF_CHECK(m2.neg == 1);
    free_one(&m2);

    wf_label_message_free(&msg);

    /* No-value label is not representable. */
    wf_label bare = {0};
    bare.uri = "at://x/y/z";
    wf_mod_label mb;
    memset(&mb, 0, sizeof(mb));
    WF_CHECK(wf_label_to_mod_label(&bare, &mb) == WF_ERR_INVALID_ARG);

    /* NULL inputs. */
    WF_CHECK(wf_label_to_mod_label(NULL, &mb) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_label_to_mod_label(&bare, NULL) == WF_ERR_INVALID_ARG);
}

static void test_parse_subscribe(void) {
    wf_mod_label *labels = NULL;
    size_t count = 0;

    /* Input validation. */
    WF_CHECK(wf_label_parse_subscribe(NULL, 0, &labels, &count) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_label_parse_subscribe(k_frame_json, strlen(k_frame_json),
                                      NULL, &count) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_label_parse_subscribe(k_frame_json, strlen(k_frame_json),
                                      &labels, NULL) == WF_ERR_INVALID_ARG);

    /* Malformed JSON (delegated to the frame parser's WF_ERR_PARSE). */
    WF_CHECK(wf_label_parse_subscribe("{not json", 9, &labels, &count) ==
             WF_ERR_PARSE);

    /* Wrong frame type (an #info frame is not a #labels frame). */
    const char *info_frame = "{\"$type\":\"#info\",\"name\":\"OutdatedCursor\"}";
    WF_CHECK(wf_label_parse_subscribe(info_frame, strlen(info_frame),
                                      &labels, &count) == WF_ERR_PARSE);

    /* A valid object with no recognised `$type` (frame parser contract). */
    WF_CHECK(wf_label_parse_subscribe("{\"seq\":1}", 9, &labels, &count) ==
             WF_ERR_INVALID_ARG);

    /* Well-formed frame -> three owned labels. */
    wf_status st = wf_label_parse_subscribe(k_frame_json, strlen(k_frame_json),
                                            &labels, &count);
    WF_CHECK(st == WF_OK);
    WF_CHECK(count == 3);
    if (st == WF_OK && count == 3) {
        WF_CHECK(strcmp(labels[0].val, "nsfw") == 0);
        WF_CHECK(labels[0].neg == 0);
        WF_CHECK(labels[0].has_cid == 0);

        WF_CHECK(strcmp(labels[1].val, "!hide") == 0);
        WF_CHECK(labels[1].has_cid == 1);
        WF_CHECK(strcmp(labels[1].cid, "bafyreig") == 0);
        WF_CHECK(labels[1].ver == 1);
        WF_CHECK(strcmp(labels[1].exp, "2099-01-01T00:00:00Z") == 0);

        WF_CHECK(strcmp(labels[2].val, "spam") == 0);
        WF_CHECK(labels[2].neg == 1);

        wf_mod_labels_free(labels, count);
        WF_CHECK(1); /* reaching here means the free path is safe */
    }
    labels = NULL;
    count = 0;
}

/* Collect labels dispatched through the agent wrapper's callback. */
static int g_seen = 0;
static char g_last_val[64];
static void on_label(const wf_mod_label *label, void *userdata) {
    (void)userdata;
    g_seen++;
    if (label && label->val) {
        snprintf(g_last_val, sizeof(g_last_val), "%s", label->val);
    }
}

static void test_agent_validation(void) {
    /* Validation branches return before any network I/O. */
    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);

    WF_CHECK(wf_agent_subscribe_labels_typed(NULL, "https://mod.bsky.app",
        0, 0, on_label, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_subscribe_labels_typed(agent, NULL,
        0, 0, on_label, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_subscribe_labels_typed(agent, "",
        0, 0, on_label, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_subscribe_labels_typed(agent, "https://mod.bsky.app",
        0, 0, NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_subscribe_labels_typed(agent, "https://mod.bsky.app",
        -1, 1, on_label, NULL) == WF_ERR_INVALID_ARG);

    /* Unreachable service: the initial connect fails fast and the wrapper
     * returns a non-OK status without hanging. */
    g_seen = 0;
    wf_status st = wf_agent_subscribe_labels_typed(agent,
        "ws://127.0.0.1:1", 0, 0, on_label, NULL);
    WF_CHECK(st != WF_OK);
    WF_CHECK(g_seen == 0); /* we never connected, so no labels arrived */

    wf_agent_free(agent);
}

int main(void) {
    test_to_mod_label();
    test_parse_subscribe();
    test_agent_validation();
    WF_TEST_SUMMARY();
}
