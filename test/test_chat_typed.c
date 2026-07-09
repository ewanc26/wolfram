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

    wf_chat_convo_free(&c);
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

static void test_parse_send_message(void) {
    size_t len = 0;
    char *json = load_fixture("chat_send_message.json", &len);
    WF_CHECK(json != NULL);

    wf_chat_message m = {0};
    wf_status s = wf_agent_parse_message(json, len, &m);
    WF_CHECK(s == WF_OK);
    WF_CHECK(m.id && strcmp(m.id, "msg-3xk9f2") == 0);
    WF_CHECK(m.rev && strcmp(m.rev, "rev-abc123def456") == 0);
    WF_CHECK(m.text && strcmp(m.text, "hello from wolfram") == 0);
    WF_CHECK(m.sender && strcmp(m.sender, "did:plc:alice") == 0);
    WF_CHECK(m.sent_at && strcmp(m.sent_at, "2024-05-01T12:34:56Z") == 0);

    wf_chat_message_reset(&m);
    WF_CHECK(m.id == NULL && m.rev == NULL && m.text == NULL &&
             m.sender == NULL && m.sent_at == NULL);
    free(json);
}

static void test_parse_send_message_invalid(void) {
    wf_chat_message m = {0};
    WF_CHECK(wf_agent_parse_message(NULL, 0, &m) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_message("not json", 8, &m) == WF_ERR_PARSE);
    WF_CHECK(m.id == NULL);
}

static void test_chat_service_did_from_describe(void) {
    /* No `chat` field present => fallback to default endpoint. */
    const char *no_chat =
        "{\"did\":\"did:plc:srv\",\"availableUserDomains\":[\"bsky.social\"]}";
    char *did = NULL;
    wf_status s = wf_agent_chat_service_did_from_describe(no_chat,
                                                          strlen(no_chat), &did);
    WF_CHECK(s == WF_OK);
    WF_CHECK(did == NULL);

    /* `chat` field present => extract the advertised DID. */
    const char *with_chat =
        "{\"did\":\"did:plc:srv\",\"availableUserDomains\":[\"bsky.social\"],"
        "\"chat\":\"did:web:api.bsky.chat\"}";
    s = wf_agent_chat_service_did_from_describe(with_chat, strlen(with_chat),
                                                &did);
    WF_CHECK(s == WF_OK);
    WF_CHECK(did && strcmp(did, "did:web:api.bsky.chat") == 0);
    free(did);

    /* Empty `chat` field is treated as absent (fallback). */
    const char *empty_chat =
        "{\"did\":\"did:plc:srv\",\"chat\":\"\"}";
    s = wf_agent_chat_service_did_from_describe(empty_chat, strlen(empty_chat),
                                                &did);
    WF_CHECK(s == WF_OK);
    WF_CHECK(did == NULL);

    WF_CHECK(wf_agent_chat_service_did_from_describe("not json", 8, &did) ==
             WF_ERR_PARSE);
    WF_CHECK(wf_agent_chat_service_did_from_describe(NULL, 0, &did) ==
             WF_ERR_INVALID_ARG);
}

static void test_chat_default_endpoint(void) {
    /* Documented fallback when describeServer omits the chat service. */
    WF_CHECK(strcmp(WF_CHAT_DEFAULT_ENDPOINT, "https://api.bsky.chat") == 0);
}

static void test_chat_resolve_args(void) {
    /* Offline arg validation for the resolver (no network needed). */
    WF_CHECK(wf_agent_chat_service_resolve(NULL) == WF_ERR_INVALID_ARG);
}

static void test_parse_chat_notification_prefs(void) {
    size_t len = 0;
    char *json = load_fixture("chat_notification_prefs.json", &len);
    WF_CHECK(json != NULL);

    wf_chat_notification_preferences prefs = {0};
    wf_status s = wf_agent_parse_chat_notification_preferences(json, len, &prefs);
    WF_CHECK(s == WF_OK);

    WF_CHECK(prefs.chat.include && strcmp(prefs.chat.include, "all") == 0);
    WF_CHECK(prefs.chat.has_push && prefs.chat.push == 1);
    WF_CHECK(prefs.chat_request.include &&
             strcmp(prefs.chat_request.include, "follows") == 0);
    WF_CHECK(prefs.chat_request.has_push && prefs.chat_request.push == 0);

    wf_chat_notification_preferences_free(&prefs);
    WF_CHECK(prefs.chat.include == NULL && prefs.chat_request.include == NULL);
    free(json);
}

static void test_parse_chat_notification_prefs_invalid(void) {
    wf_chat_notification_preferences p = {0};
    WF_CHECK(wf_agent_parse_chat_notification_preferences(NULL, 0, &p) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_chat_notification_preferences("not json", 8, &p) ==
             WF_ERR_PARSE);
    WF_CHECK(wf_agent_parse_chat_notification_preferences("{}", 2, &p) ==
             WF_ERR_PARSE);
    WF_CHECK(wf_agent_parse_chat_notification_preferences(
                 "{\"preferences\":{\"chat\":{}}}", 26, &p) == WF_ERR_PARSE);
    WF_CHECK(p.chat.include == NULL);
}

static void test_chat_notification_wrappers_null_arg(void) {
    wf_chat_notification_preferences p = {0};
    WF_CHECK(wf_agent_chat_notification_get_preferences(NULL, &p) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_notification_get_preferences((wf_agent *)1, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_notification_put_preferences(NULL, &p, &p) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_notification_put_preferences((wf_agent *)1, NULL, &p) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_notification_put_preferences((wf_agent *)1, &p, NULL) ==
             WF_ERR_INVALID_ARG);
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
    test_parse_send_message();
    test_parse_send_message_invalid();
    test_invalid_args();
    test_malformed();
    test_wrappers_null_arg();
    test_chat_service_did_from_describe();
    test_chat_default_endpoint();
    test_chat_resolve_args();
    test_parse_chat_notification_prefs();
    test_parse_chat_notification_prefs_invalid();
    test_chat_notification_wrappers_null_arg();
    WF_TEST_SUMMARY();
}
