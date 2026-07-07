/*
 * test_list_typed.c — tests for the typed Bluesky list parsers.
 *
 * Covers invalid args, malformed JSON, missing arrays/objects, and parsing of
 * real-shaped fixtures. Uses the fixture loader convention from
 * test_crypto_interop.c.
 */

#include "wolfram/list_typed.h"
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

    if (!fp) return NULL;
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
    if (len_out) *len_out = len;
    return buf;
}

static char *load_fixture(const char *filename, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, filename);
    return read_entire_file(path, len_out);
}

static void test_parse_lists_invalid_args(void) {
    wf_agent_list_view_list out = {0};
    WF_CHECK(wf_agent_parse_lists(NULL, 0, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_lists("{", 1, NULL) == WF_ERR_INVALID_ARG);
}

static void test_parse_lists_malformed(void) {
    wf_agent_list_view_list out = {0};
    WF_CHECK(wf_agent_parse_lists("{not valid json", 14, &out) ==
            WF_ERR_PARSE);
    WF_CHECK(out.list_count == 0);
    WF_CHECK(out.lists == NULL);
}

static void test_parse_lists_missing_array(void) {
    wf_agent_list_view_list out = {0};
    WF_CHECK(wf_agent_parse_lists("{\"cursor\":\"x\"}", 13, &out) ==
            WF_ERR_PARSE);
    WF_CHECK(out.list_count == 0);
}

static void test_parse_lists_fixture(void) {
    size_t len = 0;
    char *json = load_fixture("lists.json", &len);
    WF_CHECK(json != NULL);
    if (!json) return;

    wf_agent_list_view_list out = {0};
    wf_status st = wf_agent_parse_lists(json, len, &out);
    free(json);
    WF_CHECK(st == WF_OK);
    WF_CHECK(out.list_count == 2);
    WF_CHECK(out.lists != NULL);
    WF_CHECK(out.cursor != NULL);
    WF_CHECK(strcmp(out.cursor, "next-page-token") == 0);

    WF_CHECK(out.lists[0].uri != NULL);
    WF_CHECK(strcmp(out.lists[0].uri,
                    "at://did:plc:abc/app.bsky.graph.list/aaaa") == 0);
    WF_CHECK(strcmp(out.lists[0].name, "My Mod List") == 0);
    WF_CHECK(strcmp(out.lists[0].purpose,
                    "app.bsky.graph.defs#modlist") == 0);
    WF_CHECK(strcmp(out.lists[0].description, "Accounts to mute or block.") == 0);

    WF_CHECK(strcmp(out.lists[1].name, "Reading List") == 0);
    WF_CHECK(strcmp(out.lists[1].purpose,
                    "app.bsky.graph.defs#curatelist") == 0);

    wf_agent_list_view_list_free(&out);
    WF_CHECK(out.list_count == 0);
    WF_CHECK(out.lists == NULL);
}

static void test_parse_list_items_invalid_args(void) {
    wf_agent_list_item_list out = {0};
    WF_CHECK(wf_agent_parse_list_items(NULL, 0, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_list_items("{", 1, NULL) == WF_ERR_INVALID_ARG);
}

static void test_parse_list_items_malformed(void) {
    wf_agent_list_item_list out = {0};
    WF_CHECK(wf_agent_parse_list_items("{{{", 3, &out) == WF_ERR_PARSE);
    WF_CHECK(out.item_count == 0);
}

static void test_parse_list_items_missing(void) {
    wf_agent_list_item_list out = {0};
    WF_CHECK(wf_agent_parse_list_items("{\"items\":[]}", 12, &out) ==
            WF_ERR_PARSE);
    WF_CHECK(wf_agent_parse_list_items("{\"list\":{}}", 10, &out) ==
            WF_ERR_PARSE);
}

static void test_parse_list_items_fixture(void) {
    size_t len = 0;
    char *json = load_fixture("listItems.json", &len);
    WF_CHECK(json != NULL);
    if (!json) return;

    wf_agent_list_item_list out = {0};
    wf_status st = wf_agent_parse_list_items(json, len, &out);
    free(json);
    WF_CHECK(st == WF_OK);
    WF_CHECK(out.item_count == 2);
    WF_CHECK(out.items != NULL);
    WF_CHECK(out.cursor != NULL);
    WF_CHECK(strcmp(out.cursor, "next-items-token") == 0);

    WF_CHECK(out.list.uri != NULL);
    WF_CHECK(strcmp(out.list.name, "My Mod List") == 0);
    WF_CHECK(strcmp(out.list.purpose, "app.bsky.graph.defs#modlist") == 0);

    WF_CHECK(strcmp(out.items[0].uri,
                    "at://did:plc:abc/app.bsky.graph.listitem/cccc") == 0);
    WF_CHECK(strcmp(out.items[0].created_at, "2024-01-16T10:00:00.000Z") == 0);
    WF_CHECK(strcmp(out.items[0].subject.did, "did:plc:user1") == 0);
    WF_CHECK(strcmp(out.items[0].subject.handle, "alice.bsky.social") == 0);
    WF_CHECK(strcmp(out.items[0].subject.display_name, "Alice") == 0);

    WF_CHECK(strcmp(out.items[1].subject.handle, "bob.bsky.social") == 0);
    WF_CHECK(strcmp(out.items[1].subject.display_name, "Bob") == 0);

    wf_agent_list_item_list_free(&out);
    WF_CHECK(out.item_count == 0);
    WF_CHECK(out.items == NULL);
}

static void test_wrappers_arg_validation(void) {
    wf_agent_list_view_list lists = {0};
    wf_agent_list_item_list items = {0};

    WF_CHECK(wf_agent_get_lists_typed(NULL, "alice", 50, NULL, &lists) ==
            WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_lists_typed((wf_agent *)1, NULL, 50, NULL, &lists) ==
            WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_lists_typed((wf_agent *)1, "alice", 50, NULL, NULL) ==
            WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_get_list_typed(NULL, "at://x", 50, NULL, &items) ==
            WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_list_typed((wf_agent *)1, NULL, 50, NULL, &items) ==
            WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_list_typed((wf_agent *)1, "at://x", 50, NULL, NULL) ==
            WF_ERR_INVALID_ARG);
}

int main(void) {
    test_parse_lists_invalid_args();
    test_parse_lists_malformed();
    test_parse_lists_missing_array();
    test_parse_lists_fixture();

    test_parse_list_items_invalid_args();
    test_parse_list_items_malformed();
    test_parse_list_items_missing();
    test_parse_list_items_fixture();

    test_wrappers_arg_validation();

    WF_TEST_SUMMARY();
}
