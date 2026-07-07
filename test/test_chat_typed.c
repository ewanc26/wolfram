/*
 * test_chat_typed.c — tests for chat.bsky.convo typed parsers + wrappers.
 */

#include "wolfram/chat_typed.h"
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

static void test_parse_convos(void) {
    size_t len = 0;
    char *json = load_fixture("chat_convos.json", &len);
    WF_CHECK(json != NULL);

    wf_chat_convo_list list = {0};
    wf_status s = wf_agent_parse_convos(json, len, &list);
    WF_CHECK(s == WF_OK);
    WF_CHECK(list.convo_count == 2);
    WF_CHECK(list.cursor && strcmp(list.cursor, "next-page-cursor") == 0);

    WF_CHECK(list.convos[0].id && strcmp(list.convos[0].id, "convo-1") == 0);
    WF_CHECK(list.convos[0].rev && strcmp(list.convos[0].rev, "rev-abc") == 0);
    WF_CHECK(list.convos[0].member_count == 2);
    WF_CHECK(list.convos[0].members[0].did &&
             strcmp(list.convos[0].members[0].did, "did:plc:alice") == 0);
    WF_CHECK(list.convos[0].members[0].display_name &&
             strcmp(list.convos[0].members[0].display_name, "Alice") == 0);
    WF_CHECK(list.convos[0].last_message_text &&
             strcmp(list.convos[0].last_message_text, "hello there") == 0);
    WF_CHECK(list.convos[0].has_unread_count &&
             list.convos[0].unread_count == 3);
    WF_CHECK(list.convos[0].type && strcmp(list.convos[0].type, "direct") == 0);

    WF_CHECK(list.convos[1].id && strcmp(list.convos[1].id, "convo-2") == 0);
    WF_CHECK(list.convos[1].unread_count == 0);
    WF_CHECK(list.convos[1].type && strcmp(list.convos[1].type, "group") == 0);

    wf_chat_convo_list_free(&list);
    WF_CHECK(list.convo_count == 0 && list.convos == NULL);
    free(json);
}

static void test_parse_convo(void) {
    size_t len = 0;
    char *json = load_fixture("chat_convo.json", &len);
    WF_CHECK(json != NULL);

    wf_chat_convo c = {0};
    wf_status s = wf_agent_parse_convo(json, len, &c);
    WF_CHECK(s == WF_OK);
    WF_CHECK(c.id && strcmp(c.id, "convo-1") == 0);
    WF_CHECK(c.member_count == 2);
    WF_CHECK(c.has_unread_count && c.unread_count == 2);
    WF_CHECK(c.last_message_text &&
             strcmp(c.last_message_text, "hello there") == 0);
    WF_CHECK(c.type && strcmp(c.type, "direct") == 0);

    wf_chat_convo_reset(&c);
    free(json);
}

static void test_parse_messages(void) {
    size_t len = 0;
    char *json = load_fixture("chat_messages.json", &len);
    WF_CHECK(json != NULL);

    wf_chat_message_list list = {0};
    wf_status s = wf_agent_parse_messages(json, len, &list);
    WF_CHECK(s == WF_OK);
    WF_CHECK(list.message_count == 3);
    WF_CHECK(list.cursor && strcmp(list.cursor, "msg-cursor-2") == 0);

    WF_CHECK(list.messages[0].id && strcmp(list.messages[0].id, "m1") == 0);
    WF_CHECK(list.messages[0].text &&
             strcmp(list.messages[0].text, "first message") == 0);
    WF_CHECK(list.messages[0].sender &&
             strcmp(list.messages[0].sender, "did:plc:alice") == 0);
    WF_CHECK(list.messages[0].sent_at &&
             strcmp(list.messages[0].sent_at, "2024-01-01T00:00:00Z") == 0);
    WF_CHECK(list.messages[2].text &&
             strcmp(list.messages[2].text, "third message") == 0);

    wf_chat_message_list_free(&list);
    WF_CHECK(list.message_count == 0 && list.messages == NULL);
    free(json);
}

static void test_invalid_args(void) {
    wf_chat_convo_list l = {0};
    WF_CHECK(wf_agent_parse_convos(NULL, 0, &l) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_convos("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    wf_chat_convo c = {0};
    WF_CHECK(wf_agent_parse_convo(NULL, 0, &c) == WF_ERR_INVALID_ARG);

    wf_chat_message_list m = {0};
    WF_CHECK(wf_agent_parse_messages(NULL, 0, &m) == WF_ERR_INVALID_ARG);
}

static void test_malformed(void) {
    wf_chat_convo_list l = {0};
    WF_CHECK(wf_agent_parse_convos("not json", 8, &l) == WF_ERR_PARSE);
    WF_CHECK(wf_agent_parse_convos("{\"foo\":1}", 9, &l) == WF_ERR_PARSE);
    WF_CHECK(l.convo_count == 0 && l.convos == NULL);
    wf_chat_convo_list_free(&l);

    wf_chat_message_list m = {0};
    WF_CHECK(wf_agent_parse_messages("{\"messages\":5}", 15, &m) == WF_ERR_PARSE);
    wf_chat_message_list_free(&m);
}

static void test_wrappers_null_arg(void) {
    wf_chat_convo_list l = {0};
    WF_CHECK(wf_agent_chat_list_convos(NULL, 0, NULL, &l) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_list_convos((wf_agent *)1, 0, NULL, NULL) == WF_ERR_INVALID_ARG);

    wf_chat_convo c = {0};
    WF_CHECK(wf_agent_chat_get_convo(NULL, "x", &c) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo((wf_agent *)1, NULL, &c) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo((wf_agent *)1, "x", NULL) == WF_ERR_INVALID_ARG);

    wf_chat_message_list m = {0};
    WF_CHECK(wf_agent_chat_get_messages(NULL, "x", 0, NULL, &m) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_messages((wf_agent *)1, NULL, 0, NULL, &m) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_messages((wf_agent *)1, "x", 0, NULL, NULL) == WF_ERR_INVALID_ARG);
}

int main(void) {
    test_parse_convos();
    test_parse_convo();
    test_parse_messages();
    test_invalid_args();
    test_malformed();
    test_wrappers_null_arg();
    WF_TEST_SUMMARY();
}
