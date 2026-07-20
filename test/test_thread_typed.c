/*
 * test_thread_typed.c — unit tests for the typed getPostThread parser.
 *
 * Exercises the offline JSON parser (`wf_agent_parse_thread`) with a fixture;
 * no network calls are made.
 */

#include "wolfram/thread_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    char *buf = NULL;
    long size;
    size_t len;

    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    len = (size_t)size;
    buf = malloc(len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, len, fp) != len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    if (len_out) {
        *len_out = len;
    }
    return buf;
}

static char *load_fixture(const char *filename, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, filename);
    return read_entire_file(path, len_out);
}

int main(void) {
    /* Invalid-argument handling. */
    wf_agent_thread t = {0};
    WF_CHECK(wf_agent_parse_thread(NULL, 0, &t) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_thread("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    /* Malformed JSON. */
    WF_CHECK(wf_agent_parse_thread("not json", 8, &t) == WF_ERR_PARSE);

    /* Missing `thread`. */
    WF_CHECK(wf_agent_parse_thread("{\"cursor\":\"x\"}", 12, &t) == WF_ERR_PARSE);

    /* Valid fixture. */
    size_t len = 0;
    char *json = load_fixture("thread.json", &len);
    WF_CHECK(json != NULL);

    if (json) {
        wf_status status = wf_agent_parse_thread(json, len, &t);
        WF_CHECK(status == WF_OK);

        WF_CHECK(t.root.kind == WF_AGENT_THREAD_KIND_POST);
        WF_CHECK(t.root.post.uri &&
                 strcmp(t.root.post.uri,
                        "at://did:plc:root/example.com/post/abc") == 0);
        WF_CHECK(t.root.post.cid && strcmp(t.root.post.cid, "bafyrootcid") == 0);
        WF_CHECK(t.root.post.author.handle &&
                 strcmp(t.root.post.author.handle, "root.example.com") == 0);
        WF_CHECK(t.root.post.author.display_name &&
                 strcmp(t.root.post.author.display_name, "Root Author") == 0);
        WF_CHECK(t.root.post.reply_count == 2);
        WF_CHECK(t.root.post.repost_count == 1);
        WF_CHECK(t.root.post.like_count == 5);
        WF_CHECK(t.root.post.quote_count == 0);
        WF_CHECK(t.root.post.viewer_like &&
                 strstr(t.root.post.viewer_like, "app.bsky.feed.like/like1"));
        WF_CHECK(t.root.post.viewer_repost &&
                 strstr(t.root.post.viewer_repost, "app.bsky.feed.repost/repost1"));
        WF_CHECK(t.root.post.indexed_at &&
                 strcmp(t.root.post.indexed_at, "2026-07-01T10:01:00Z") == 0);
        WF_CHECK(t.root.post.record != NULL);
        WF_CHECK(cJSON_IsObject(t.root.post.record));

        /* Two replies: one post, one notFound union member. */
        WF_CHECK(t.root.replies_count == 2);
        if (t.root.replies_count == 2) {
            WF_CHECK(t.root.replies[0].kind == WF_AGENT_THREAD_KIND_POST);
            WF_CHECK(t.root.replies[0].post.author.handle &&
                     strcmp(t.root.replies[0].post.author.handle,
                            "reply1.example.com") == 0);
            WF_CHECK(t.root.replies[1].kind == WF_AGENT_THREAD_KIND_NOT_FOUND);
            WF_CHECK(t.root.replies[1].uri &&
                     strstr(t.root.replies[1].uri, "gone") != NULL);
        }

        /* Parent node. */
        WF_CHECK(t.root.parent != NULL);
        if (t.root.parent) {
            WF_CHECK(t.root.parent->kind == WF_AGENT_THREAD_KIND_POST);
            WF_CHECK(t.root.parent->post.author.handle &&
                     strcmp(t.root.parent->post.author.handle,
                            "parent.example.com") == 0);
        }

        /* Cursor. */
        WF_CHECK(t.cursor && strcmp(t.cursor, "thread-cursor-xyz") == 0);

        wf_agent_thread_free(&t);
        /* After free the struct is zeroed and a double-free is safe. */
        WF_CHECK(t.root.post.uri == NULL);
        WF_CHECK(t.root.replies == NULL);
        WF_CHECK(t.cursor == NULL);
        wf_agent_thread_free(&t);

        free(json);
    }

    /* Wrapper arg validation (no network; NULL agent/args rejected). */
    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);
    wf_agent_thread t2 = {0};
    WF_CHECK(wf_agent_get_post_thread_typed(NULL, "at://x", 1, &t2) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_post_thread_typed(agent, NULL, 1, &t2) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_post_thread_typed(agent, "at://x", 1, NULL) == WF_ERR_INVALID_ARG);
    wf_agent_free(agent);

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
