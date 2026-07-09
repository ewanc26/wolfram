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
    WF_CHECK(wf_agent_chat_list_convos((wf_agent *)1, 0, NULL, NULL) ==
              WF_ERR_INVALID_ARG);

    wf_chat_convo c = {0};
    WF_CHECK(wf_agent_chat_get_convo(NULL, "x", &c) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo((wf_agent *)1, NULL, &c) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_convo((wf_agent *)1, "x", NULL) ==
              WF_ERR_INVALID_ARG);

    wf_chat_message_list m = {0};
    WF_CHECK(wf_agent_chat_get_messages(NULL, "x", 0, NULL, &m) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_messages((wf_agent *)1, NULL, 0, NULL, &m) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_messages((wf_agent *)1, "x", 0, NULL, NULL) ==
              WF_ERR_INVALID_ARG);
}

/* ════════════════════════════════════════════════════════════════════════
 * Tests for the extended owned-struct typed wrappers / parsers.
 * ════════════════════════════════════════════════════════════════════════ */

static void test_parse_convo_availability(void) {
    const char *json =
        "{\"canChat\":true,\"hasConvo\":true,"
        "\"convo\":{\"id\":\"c1\",\"rev\":\"r1\","
        "\"members\":[{\"did\":\"did:plc:a\"}],\"unreadCount\":0}}";
    wf_chat_convo_availability a = {0};
    wf_status s = wf_agent_parse_convo_availability(json, strlen(json), &a);
    WF_CHECK(s == WF_OK);
    WF_CHECK(a.has_can_chat && a.can_chat == 1);
    WF_CHECK(a.has_has_convo && a.has_convo == 1);
    WF_CHECK(a.convo.id && strcmp(a.convo.id, "c1") == 0);
    WF_CHECK(a.convo.member_count == 1);
    wf_chat_convo_availability_free(&a);
    WF_CHECK(a.convo.id == NULL);
}

static void test_parse_convo_members(void) {
    const char *json =
        "{\"members\":[{\"did\":\"did:plc:a\",\"handle\":\"a\","
        "\"displayName\":\"A\"},{\"did\":\"did:plc:b\"}],\"cursor\":\"mc\"}";
    wf_chat_convo_members m = {0};
    wf_status s = wf_agent_parse_convo_members(json, strlen(json), &m);
    WF_CHECK(s == WF_OK);
    WF_CHECK(m.member_count == 2);
    WF_CHECK(m.members[0].did && strcmp(m.members[0].did, "did:plc:a") == 0);
    WF_CHECK(m.members[0].display_name &&
             strcmp(m.members[0].display_name, "A") == 0);
    WF_CHECK(m.members[1].did && strcmp(m.members[1].did, "did:plc:b") == 0);
    WF_CHECK(m.cursor && strcmp(m.cursor, "mc") == 0);
    wf_chat_convo_members_free(&m);
    WF_CHECK(m.members == NULL && m.cursor == NULL);
}

static void test_parse_unread_counts(void) {
    const char *json = "{\"unreadAcceptedConvos\":3,\"unreadRequestConvos\":2}";
    wf_chat_unread_counts u = {0};
    wf_status s = wf_agent_parse_unread_counts(json, strlen(json), &u);
    WF_CHECK(s == WF_OK);
    WF_CHECK(u.has_unread_accepted && u.unread_accepted_convos == 3);
    WF_CHECK(u.has_unread_request && u.unread_request_convos == 2);
    wf_chat_unread_counts_free(&u);
}

static void test_parse_convo_requests(void) {
    const char *json =
        "{\"requests\":[{\"id\":\"c1\",\"rev\":\"r1\","
        "\"members\":[{\"did\":\"did:plc:a\"}],\"unreadCount\":1}],"
        "\"cursor\":\"rc\"}";
    wf_chat_convo_list l = {0};
    wf_status s = wf_agent_parse_convo_requests(json, strlen(json), &l);
    WF_CHECK(s == WF_OK);
    WF_CHECK(l.convo_count == 1);
    WF_CHECK(l.convos[0].id && strcmp(l.convos[0].id, "c1") == 0);
    WF_CHECK(l.cursor && strcmp(l.cursor, "rc") == 0);
    wf_chat_convo_list_free(&l);
    WF_CHECK(l.convos == NULL);
}

static void test_parse_convo_array_key(void) {
    /* listMutualGroups / moderation.getConvos use the "convos" key. */
    const char *json =
        "{\"convos\":[{\"id\":\"c1\",\"rev\":\"r1\","
        "\"members\":[{\"did\":\"did:plc:a\"}],\"unreadCount\":0}],"
        "\"cursor\":\"gc\"}";
    wf_chat_convo_list l = {0};
    wf_status s = wf_agent_parse_convo_array(json, strlen(json), "convos", &l);
    WF_CHECK(s == WF_OK);
    WF_CHECK(l.convo_count == 1);
    WF_CHECK(l.convos[0].id && strcmp(l.convos[0].id, "c1") == 0);
    wf_chat_convo_list_free(&l);
}

static void test_parse_log(void) {
    const char *json =
        "{\"logs\":[{\"$type\":\"#logBeginConvo\",\"id\":\"l1\",\"rev\":\"r1\","
        "\"convoId\":\"c1\"},"
        "{\"$type\":\"#logAddMember\",\"id\":\"l2\",\"convoId\":\"c1\"}],"
        "\"cursor\":\"lc\"}";
    wf_chat_log lg = {0};
    wf_status s = wf_agent_parse_log(json, strlen(json), &lg);
    WF_CHECK(s == WF_OK);
    WF_CHECK(lg.event_count == 2);
    WF_CHECK(lg.events[0].id && strcmp(lg.events[0].id, "l1") == 0);
    WF_CHECK(lg.events[0].type &&
             strcmp(lg.events[0].type, "#logBeginConvo") == 0);
    WF_CHECK(lg.events[0].convo_id &&
             strcmp(lg.events[0].convo_id, "c1") == 0);
    WF_CHECK(lg.events[1].type &&
             strcmp(lg.events[1].type, "#logAddMember") == 0);
    WF_CHECK(lg.cursor && strcmp(lg.cursor, "lc") == 0);
    wf_chat_log_free(&lg);
    WF_CHECK(lg.events == NULL);
}

static void test_parse_message_batch(void) {
    const char *json =
        "{\"items\":[{\"convoId\":\"c1\","
        "\"message\":{\"id\":\"m1\",\"rev\":\"r1\",\"text\":\"hi\","
        "\"sender\":{\"did\":\"did:plc:a\"},"
        "\"sentAt\":\"2024-01-01T00:00:00Z\"}}]}";
    wf_chat_message_batch b = {0};
    wf_status s = wf_agent_parse_message_batch(json, strlen(json), &b);
    WF_CHECK(s == WF_OK);
    WF_CHECK(b.item_count == 1);
    WF_CHECK(b.items[0].convo_id &&
             strcmp(b.items[0].convo_id, "c1") == 0);
    WF_CHECK(b.items[0].message.id &&
             strcmp(b.items[0].message.id, "m1") == 0);
    WF_CHECK(b.items[0].message.text &&
             strcmp(b.items[0].message.text, "hi") == 0);
    wf_chat_message_batch_free(&b);
    WF_CHECK(b.items == NULL);
}

static void test_parse_convo_ref(void) {
    const char *json = "{\"convoId\":\"c1\",\"rev\":\"r1\"}";
    wf_chat_convo_ref r = {0};
    wf_status s = wf_agent_parse_convo_ref(json, strlen(json), &r);
    WF_CHECK(s == WF_OK);
    WF_CHECK(r.convo_id && strcmp(r.convo_id, "c1") == 0);
    WF_CHECK(r.rev && strcmp(r.rev, "r1") == 0);
    wf_chat_convo_ref_free(&r);
    WF_CHECK(r.convo_id == NULL);
}

static void test_parse_updated_count(void) {
    const char *json = "{\"updatedCount\":7}";
    wf_chat_updated_count u = {0};
    wf_status s = wf_agent_parse_updated_count(json, strlen(json), &u);
    WF_CHECK(s == WF_OK);
    WF_CHECK(u.has_updated_count && u.updated_count == 7);
    wf_chat_updated_count_free(&u);
}

static void test_parse_message_ref(void) {
    const char *json =
        "{\"message\":{\"id\":\"m1\",\"rev\":\"r1\",\"text\":\"hi\","
        "\"sender\":{\"did\":\"did:plc:a\"},"
        "\"sentAt\":\"2024-01-01T00:00:00Z\"}}";
    wf_chat_message m = {0};
    wf_status s = wf_agent_parse_message_ref(json, strlen(json), &m);
    WF_CHECK(s == WF_OK);
    WF_CHECK(m.id && strcmp(m.id, "m1") == 0);
    WF_CHECK(m.text && strcmp(m.text, "hi") == 0);
    wf_chat_message_reset(&m);
    WF_CHECK(m.id == NULL);
}

static void test_parse_join_link(void) {
    const char *json =
        "{\"joinLink\":{\"code\":\"abc\",\"enabledStatus\":\"enabled\","
        "\"requireApproval\":true,\"joinRule\":\"all-members\","
        "\"createdAt\":\"2024-01-01T00:00:00Z\"}}";
    wf_chat_join_link j = {0};
    wf_status s = wf_agent_parse_join_link(json, strlen(json), &j);
    WF_CHECK(s == WF_OK);
    WF_CHECK(j.code && strcmp(j.code, "abc") == 0);
    WF_CHECK(j.enabled_status && strcmp(j.enabled_status, "enabled") == 0);
    WF_CHECK(j.has_require_approval && j.require_approval == 1);
    WF_CHECK(j.join_rule && strcmp(j.join_rule, "all-members") == 0);
    WF_CHECK(j.created_at && strcmp(j.created_at, "2024-01-01T00:00:00Z") == 0);
    wf_chat_join_link_free(&j);
    WF_CHECK(j.code == NULL);
}

static void test_parse_join_link_previews(void) {
    const char *json =
        "{\"joinLinkPreviews\":["
        "{\"$type\":\"#joinLinkPreviewView\",\"code\":\"abc\","
        "\"name\":\"Group\",\"joinRule\":\"all-members\","
        "\"requireApproval\":true}]}";
    wf_chat_join_link_previews p = {0};
    wf_status s = wf_agent_parse_join_link_previews(json, strlen(json), &p);
    WF_CHECK(s == WF_OK);
    WF_CHECK(p.item_count == 1);
    WF_CHECK(p.items[0].code && strcmp(p.items[0].code, "abc") == 0);
    WF_CHECK(p.items[0].name && strcmp(p.items[0].name, "Group") == 0);
    WF_CHECK(p.items[0].has_require_approval &&
             p.items[0].require_approval == 1);
    wf_chat_join_link_previews_free(&p);
    WF_CHECK(p.items == NULL);
}

static void test_parse_join_requests(void) {
    const char *json =
        "{\"requests\":[{\"convoId\":\"c1\","
        "\"requestedBy\":{\"did\":\"did:plc:x\",\"displayName\":\"X\"},"
        "\"requestedAt\":\"2024-01-01T00:00:00Z\"}],\"cursor\":\"jrc\"}";
    wf_chat_join_requests r = {0};
    wf_status s = wf_agent_parse_join_requests(json, strlen(json), &r);
    WF_CHECK(s == WF_OK);
    WF_CHECK(r.item_count == 1);
    WF_CHECK(r.items[0].convo_id && strcmp(r.items[0].convo_id, "c1") == 0);
    WF_CHECK(r.items[0].requested_by.did &&
             strcmp(r.items[0].requested_by.did, "did:plc:x") == 0);
    WF_CHECK(r.items[0].requested_at &&
             strcmp(r.items[0].requested_at, "2024-01-01T00:00:00Z") == 0);
    WF_CHECK(r.cursor && strcmp(r.cursor, "jrc") == 0);
    wf_chat_join_requests_free(&r);
    WF_CHECK(r.items == NULL);
}

static void test_parse_request_join(void) {
    const char *json =
        "{\"status\":\"joined\","
        "\"convo\":{\"id\":\"c1\",\"rev\":\"r1\","
        "\"members\":[{\"did\":\"did:plc:a\"}]}}";
    wf_chat_request_join r = {0};
    wf_status s = wf_agent_parse_request_join(json, strlen(json), &r);
    WF_CHECK(s == WF_OK);
    WF_CHECK(r.status && strcmp(r.status, "joined") == 0);
    WF_CHECK(r.has_convo && r.convo.id && strcmp(r.convo.id, "c1") == 0);
    wf_chat_request_join_free(&r);
    WF_CHECK(r.status == NULL);
}

static void test_parse_actor_metadata(void) {
    const char *json =
        "{\"day\":{\"messagesSent\":1,\"messagesReceived\":2,"
        "\"convos\":3,\"convosStarted\":4},"
        "\"month\":{\"messagesSent\":10,\"messagesReceived\":20,"
        "\"convos\":30,\"convosStarted\":40},"
        "\"all\":{\"messagesSent\":100,\"messagesReceived\":200,"
        "\"convos\":300,\"convosStarted\":400}}";
    wf_chat_actor_metadata m = {0};
    wf_status s = wf_agent_parse_actor_metadata(json, strlen(json), &m);
    WF_CHECK(s == WF_OK);
    WF_CHECK(m.day.has_messages_sent && m.day.messages_sent == 1);
    WF_CHECK(m.day.has_convos_started && m.day.convos_started == 4);
    WF_CHECK(m.month.has_messages_received && m.month.messages_received == 20);
    WF_CHECK(m.all.has_convos && m.all.convos == 300);
    wf_chat_actor_metadata_free(&m);
}

static void test_parse_mod_convo(void) {
    const char *json =
        "{\"id\":\"c1\",\"rev\":\"r1\","
        "\"kind\":{\"$type\":\"#directConvo\",\"foo\":1}}";
    wf_chat_mod_convo c = {0};
    wf_status s = wf_agent_parse_mod_convo(json, strlen(json), &c);
    WF_CHECK(s == WF_OK);
    WF_CHECK(c.id && strcmp(c.id, "c1") == 0);
    WF_CHECK(c.rev && strcmp(c.rev, "r1") == 0);
    WF_CHECK(c.type && strcmp(c.type, "#directConvo") == 0);
    wf_chat_mod_convo_free(&c);
    WF_CHECK(c.id == NULL);
}

static void test_parse_mod_convos(void) {
    const char *json =
        "{\"convos\":[{\"id\":\"c1\",\"rev\":\"r1\","
        "\"kind\":{\"$type\":\"#groupConvo\"}}]}";
    wf_chat_mod_convo_list l = {0};
    wf_status s = wf_agent_parse_mod_convos(json, strlen(json), &l);
    WF_CHECK(s == WF_OK);
    WF_CHECK(l.convo_count == 1);
    WF_CHECK(l.convos[0].id && strcmp(l.convos[0].id, "c1") == 0);
    WF_CHECK(l.convos[0].type && strcmp(l.convos[0].type, "#groupConvo") == 0);
    wf_chat_mod_convo_list_free(&l);
    WF_CHECK(l.convos == NULL);
}

static void test_parse_actor_status(void) {
    const char *json =
        "{\"chatDisabled\":false,\"canCreateGroups\":true,"
        "\"groupMemberLimit\":50}";
    wf_chat_actor_status s = {0};
    wf_status st = wf_agent_parse_actor_status(json, strlen(json), &s);
    WF_CHECK(st == WF_OK);
    WF_CHECK(s.has_chat_disabled && s.chat_disabled == 0);
    WF_CHECK(s.has_can_create_groups && s.can_create_groups == 1);
    WF_CHECK(s.has_group_member_limit && s.group_member_limit == 50);
    wf_chat_actor_status_free(&s);
}

static void test_extended_typed_wrappers_null_arg(void) {
    /* Argument validation (no network needed): NULL agent / NULL out. */
    wf_chat_convo_availability av = {0};
    WF_CHECK(wf_agent_chat_get_convo_availability_typed(NULL, NULL, 0, &av) ==
              WF_ERR_INVALID_ARG);
    wf_chat_convo_members mem = {0};
    WF_CHECK(wf_agent_chat_get_convo_members_typed(NULL, "x", &mem) ==
              WF_ERR_INVALID_ARG);
    wf_chat_unread_counts uc = {0};
    WF_CHECK(wf_agent_chat_get_unread_counts_typed(NULL, &uc) ==
              WF_ERR_INVALID_ARG);
    wf_chat_convo_list l = {0};
    WF_CHECK(wf_agent_chat_list_convo_requests_typed(NULL, 0, NULL, &l) ==
              WF_ERR_INVALID_ARG);
    wf_chat_log lg = {0};
    WF_CHECK(wf_agent_chat_get_log_typed(NULL, 0, NULL, &lg) ==
              WF_ERR_INVALID_ARG);
    wf_chat_convo c = {0};
    WF_CHECK(wf_agent_chat_accept_convo_typed(NULL, "x", &c) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mute_convo_typed(NULL, "x", &c) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_unlock_convo_typed(NULL, "x", &c) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_update_read_typed(NULL, "x", "y", &c) ==
              WF_ERR_INVALID_ARG);
    wf_chat_convo_ref r = {0};
    WF_CHECK(wf_agent_chat_leave_convo_typed(NULL, "x", &r) ==
              WF_ERR_INVALID_ARG);
    wf_chat_updated_count u = {0};
    WF_CHECK(wf_agent_chat_update_all_read_typed(NULL, "x", &u) ==
              WF_ERR_INVALID_ARG);
    wf_chat_message m = {0};
    WF_CHECK(wf_agent_chat_add_reaction_typed(NULL, "x", "y", "z", &m) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_remove_reaction_typed(NULL, "x", "y", "z", &m) ==
              WF_ERR_INVALID_ARG);
    wf_chat_join_link j = {0};
    WF_CHECK(wf_agent_chat_create_join_link_typed(NULL, "x", true, "y", &j) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_enable_join_link_typed(NULL, "x", &j) ==
              WF_ERR_INVALID_ARG);
    wf_chat_join_link_previews p = {0};
    WF_CHECK(wf_agent_chat_get_join_link_previews_typed(NULL, NULL, 0, &p) ==
              WF_ERR_INVALID_ARG);
    wf_chat_join_requests jr = {0};
    WF_CHECK(wf_agent_chat_list_join_requests_typed(NULL, "x", 0, NULL, &jr) ==
              WF_ERR_INVALID_ARG);
    wf_chat_request_join rq = {0};
    WF_CHECK(wf_agent_chat_request_join_typed(NULL, "x", &rq) ==
              WF_ERR_INVALID_ARG);
    wf_chat_actor_metadata am = {0};
    WF_CHECK(wf_agent_chat_mod_get_actor_metadata_typed(NULL, "x", &am) ==
              WF_ERR_INVALID_ARG);
    wf_chat_mod_convo mc = {0};
    WF_CHECK(wf_agent_chat_mod_get_convo_typed(NULL, "x", &mc) ==
              WF_ERR_INVALID_ARG);
    wf_chat_mod_convo_list ml = {0};
    WF_CHECK(wf_agent_chat_mod_get_convos_typed(NULL, NULL, 0, &ml) ==
              WF_ERR_INVALID_ARG);
    wf_chat_actor_status as = {0};
    WF_CHECK(wf_agent_chat_get_status_typed(NULL, &as) == WF_ERR_INVALID_ARG);
    wf_chat_message_list ml2 = {0};
    WF_CHECK(wf_agent_chat_mod_get_message_context_typed(NULL, "x", "y", 0, 0,
                                                        0, &ml2) ==
              WF_ERR_INVALID_ARG);

    /* out == NULL must also be rejected without touching the service. */
    WF_CHECK(wf_agent_chat_get_convo_members_typed((wf_agent *)1, "x", NULL) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_get_status_typed((wf_agent *)1, NULL) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_accept_convo_typed((wf_agent *)1, "x", NULL) ==
              WF_ERR_INVALID_ARG);
}

static void test_extended_typed_parsers_invalid(void) {
    wf_chat_convo_availability a = {0};
    WF_CHECK(wf_agent_parse_convo_availability(NULL, 0, &a) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_convo_availability("not json", 8, &a) ==
              WF_ERR_PARSE);
    wf_chat_unread_counts u = {0};
    WF_CHECK(wf_agent_parse_unread_counts(NULL, 0, &u) == WF_ERR_INVALID_ARG);
    wf_chat_log lg = {0};
    WF_CHECK(wf_agent_parse_log("{\"foo\":1}", 9, &lg) == WF_ERR_PARSE);
    wf_chat_message m = {0};
    WF_CHECK(wf_agent_parse_message_ref("{\"foo\":1}", 9, &m) == WF_ERR_PARSE);
    wf_chat_actor_status s = {0};
    WF_CHECK(wf_agent_parse_actor_status(NULL, 0, &s) == WF_ERR_INVALID_ARG);
}

static void test_parse_chat_ok(void) {
    /* Empty-body procedures parse as ok=true. */
    wf_chat_ok o = {0};
    wf_status s = wf_agent_parse_chat_ok("{}", 2, &o);
    WF_CHECK(s == WF_OK && o.ok == 1);
    wf_chat_ok_free(&o);
    WF_CHECK(o.ok == 0);

    wf_chat_ok o2 = {0};
    const char *ok_body = "{\"rev\":\"r1\"}";
    WF_CHECK(wf_agent_parse_chat_ok(ok_body, strlen(ok_body), &o2) == WF_OK);
    wf_chat_ok_free(&o2);

    /* A non-object body is rejected. */
    wf_chat_ok o3 = {0};
    WF_CHECK(wf_agent_parse_chat_ok("not json", 8, &o3) == WF_ERR_PARSE);
    WF_CHECK(wf_agent_parse_chat_ok("[1,2]", 5, &o3) == WF_ERR_PARSE);

    /* NULL inputs. */
    WF_CHECK(wf_agent_parse_chat_ok(NULL, 0, &o3) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_chat_ok("{}", 2, NULL) == WF_ERR_INVALID_ARG);
}

static void test_parse_export_account_data(void) {
    const char *jsonl =
        "{\"uri\":\"at://did:plc:a/.../r1\"}\n"
        "{\"uri\":\"at://did:plc:a/.../r2\"}\r\n"
        "{\"uri\":\"at://did:plc:a/.../r3\"}";
    wf_chat_export_account_data e = {0};
    wf_status s = wf_agent_parse_export_account_data(jsonl, strlen(jsonl), &e);
    WF_CHECK(s == WF_OK);
    WF_CHECK(e.record_count == 3);
    WF_CHECK(e.records[0].json &&
              strstr(e.records[0].json, "r1") != NULL);
    WF_CHECK(e.records[2].json &&
              strstr(e.records[2].json, "r3") != NULL);
    wf_chat_export_account_data_free(&e);
    WF_CHECK(e.records == NULL && e.record_count == 0);

    /* Blank lines are skipped. */
    wf_chat_export_account_data e2 = {0};
    WF_CHECK(wf_agent_parse_export_account_data("\n\n{\"a\":1}\n",
             strlen("\n\n{\"a\":1}\n"), &e2) == WF_OK);
    WF_CHECK(e2.record_count == 1);
    wf_chat_export_account_data_free(&e2);

    wf_chat_export_account_data e3 = {0};
    WF_CHECK(wf_agent_parse_export_account_data(NULL, 0, &e3) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_export_account_data("x", 1, NULL) ==
              WF_ERR_INVALID_ARG);
}

static void test_parse_mod_event(void) {
    const char *json =
        "{\"$type\":\"#eventGroupChatCreated\",\"convoId\":\"c1\","
        "\"rev\":\"r1\",\"createdAt\":\"2024-01-01T00:00:00Z\","
        "\"actorDid\":\"did:plc:owner\",\"subjectDid\":\"did:plc:sub\"}";
    wf_chat_mod_event ev = {0};
    wf_status s = wf_agent_parse_mod_event(json, strlen(json), &ev);
    WF_CHECK(s == WF_OK);
    WF_CHECK(ev.type && strcmp(ev.type, "#eventGroupChatCreated") == 0);
    WF_CHECK(ev.convo_id && strcmp(ev.convo_id, "c1") == 0);
    WF_CHECK(ev.rev && strcmp(ev.rev, "r1") == 0);
    WF_CHECK(ev.created_at &&
              strcmp(ev.created_at, "2024-01-01T00:00:00Z") == 0);
    WF_CHECK(ev.actor_did && strcmp(ev.actor_did, "did:plc:owner") == 0);
    WF_CHECK(ev.subject_did && strcmp(ev.subject_did, "did:plc:sub") == 0);
    wf_chat_mod_event_free(&ev);
    WF_CHECK(ev.type == NULL);

    wf_chat_mod_event ev2 = {0};
    WF_CHECK(wf_agent_parse_mod_event("not json", 8, &ev2) == WF_ERR_PARSE);
    WF_CHECK(wf_agent_parse_mod_event(NULL, 0, &ev2) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_mod_event("{}", 2, NULL) ==
              WF_ERR_INVALID_ARG);
}

static void test_new_typed_wrappers_null_arg(void) {
    /* Argument validation (no network): NULL agent / NULL out. */
    wf_chat_ok ok = {0};
    WF_CHECK(wf_agent_chat_reject_join_request_typed(NULL, "x", "y", &ok) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_reject_join_request_typed((wf_agent *)1, NULL, "y",
                                                     &ok) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_reject_join_request_typed((wf_agent *)1, "x", NULL,
                                                     &ok) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_reject_join_request_typed((wf_agent *)1, "x", "y",
                                                     NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_chat_update_join_requests_read_typed(NULL, "x", &ok) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_update_join_requests_read_typed((wf_agent *)1, NULL,
                                                          &ok) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_update_join_requests_read_typed((wf_agent *)1, "x",
                                                          NULL) ==
              WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_chat_withdraw_join_request_typed(NULL, "x", &ok) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_withdraw_join_request_typed((wf_agent *)1, "x",
                                                      NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_chat_delete_account_typed(NULL, &ok) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_delete_account_typed((wf_agent *)1, NULL) ==
              WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_chat_mod_update_actor_access_typed(NULL, "x", true, "r",
                                                        &ok) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mod_update_actor_access_typed((wf_agent *)1, NULL,
                                                         true, "r", &ok) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_mod_update_actor_access_typed((wf_agent *)1, "x",
                                                         true, "r", NULL) ==
              WF_ERR_INVALID_ARG);

    wf_chat_export_account_data ex = {0};
    WF_CHECK(wf_agent_chat_export_account_data_typed(NULL, &ex) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_export_account_data_typed((wf_agent *)1, NULL) ==
              WF_ERR_INVALID_ARG);

    /* subscribeModEvents is an honest stub (streaming not wired). */
    WF_CHECK(wf_agent_chat_subscribe_mod_events_typed(NULL, NULL) ==
              WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_chat_subscribe_mod_events_typed((wf_agent *)1, "cur") ==
              WF_ERR_INVALID_ARG);
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
    test_parse_convo_availability();
    test_parse_convo_members();
    test_parse_unread_counts();
    test_parse_convo_requests();
    test_parse_convo_array_key();
    test_parse_log();
    test_parse_message_batch();
    test_parse_convo_ref();
    test_parse_updated_count();
    test_parse_message_ref();
    test_parse_join_link();
    test_parse_join_link_previews();
    test_parse_join_requests();
    test_parse_request_join();
    test_parse_actor_metadata();
    test_parse_mod_convo();
    test_parse_mod_convos();
    test_parse_actor_status();
    test_extended_typed_wrappers_null_arg();
    test_extended_typed_parsers_invalid();
    test_parse_chat_ok();
    test_parse_export_account_data();
    test_parse_mod_event();
    test_new_typed_wrappers_null_arg();
    WF_TEST_SUMMARY();
}
