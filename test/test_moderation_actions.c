/*
 * test_moderation_actions.c — parser and wrapper validation for the
 * moderation/social-graph action procedures (com.atproto.moderation
 * .createReport, app.bsky.graph.muteActorList, app.bsky.graph.blockActorList).
 *
 * Parsers are exercised against fixtures from the shared test/fixtures dir and
 * against malformed/missing-field inputs. Wrappers are exercised for NULL-arg
 * validation only (no network).
 */

#include "wolfram/moderation_actions.h"
#include "test.h"

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

    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[size] = '\0';
    fclose(fp);
    if (len_out) *len_out = (size_t)size;
    return buf;
}

static char *load_fixture(const char *filename, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, filename);
    return read_entire_file(path, len_out);
}

static void test_parse_report(void) {
    size_t len = 0;
    char *json = load_fixture("report.json", &len);
    WF_CHECK(json != NULL);

    wf_moderation_report r;
    wf_status st = wf_agent_parse_report(json, len, &r);
    free(json);
    WF_CHECK(st == WF_OK);
    WF_CHECK(r.id != NULL);
    WF_CHECK(r.id && strcmp(r.id, "12345") == 0);
    WF_CHECK(r.reason != NULL);
    WF_CHECK(r.reason && strcmp(r.reason, "spam and scam content") == 0);
    WF_CHECK(r.subject_uri != NULL);
    WF_CHECK(r.subject_uri &&
             strcmp(r.subject_uri,
                    "at://did:plc:subjectabc/app.bsky.feed.post/1abcdef") == 0);
    wf_moderation_report_free(&r);
}

static void test_parse_report_repo_subject(void) {
    const char *json =
        "{\"id\":7,\"reasonType\":\"com.atproto.moderation.defs#reasonSpam\","
        "\"subject\":{\"repo\":\"did:plc:repotarget\"},"
        "\"reportedBy\":\"did:plc:reporter\",\"createdAt\":"
        "\"2024-02-02T00:00:00.000Z\"}";
    wf_moderation_report r;
    wf_status st = wf_agent_parse_report(json, strlen(json), &r);
    WF_CHECK(st == WF_OK);
    WF_CHECK(r.id && strcmp(r.id, "7") == 0);
    WF_CHECK(r.reason == NULL);
    WF_CHECK(r.subject_uri &&
             strcmp(r.subject_uri, "did:plc:repotarget") == 0);
    wf_moderation_report_free(&r);
}

static void test_parse_report_invalid(void) {
    wf_moderation_report r;

    WF_CHECK(wf_agent_parse_report(NULL, 0, &r) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_report("not json", 8, &r) == WF_ERR_PARSE);

    /* Missing `list`? not relevant here; check malformed subject still OK but
     * leaves subject_uri NULL. */
    const char *no_subj = "{\"id\":1,\"reasonType\":\"x\"}";
    wf_status st = wf_agent_parse_report(no_subj, strlen(no_subj), &r);
    WF_CHECK(st == WF_OK);
    WF_CHECK(r.id && strcmp(r.id, "1") == 0);
    WF_CHECK(r.subject_uri == NULL);
    WF_CHECK(r.reason == NULL);
    wf_moderation_report_free(&r);
}

static void test_parse_list_view(const char *fixture) {
    size_t len = 0;
    char *json = load_fixture(fixture, &len);
    WF_CHECK(json != NULL);

    wf_mod_list_view_result res;
    wf_status st = wf_agent_parse_list_view_result(json, len, &res);
    free(json);
    WF_CHECK(st == WF_OK);
    WF_CHECK(res.list.uri != NULL);
    WF_CHECK(res.list.cid != NULL);
    WF_CHECK(res.list.name != NULL);
    WF_CHECK(res.list.purpose != NULL);
    WF_CHECK(res.list.description != NULL);
    WF_CHECK(res.list.avatar != NULL);
    WF_CHECK(res.cursor != NULL);
    wf_mod_list_view_result_free(&res);
}

static void test_parse_list_view_missing_optional(void) {
    const char *json =
        "{\"list\":{\"uri\":\"at://x/app.bsky.graph.list/1\","
        "\"cid\":\"bafycid\",\"name\":\"Minimal\",\"purpose\":"
        "\"app.bsky.graph.defs#modlist\"}}";
    wf_mod_list_view_result res;
    wf_status st = wf_agent_parse_list_view_result(json, strlen(json), &res);
    WF_CHECK(st == WF_OK);
    WF_CHECK(res.list.uri && strcmp(res.list.uri, "at://x/app.bsky.graph.list/1") == 0);
    WF_CHECK(res.list.name && strcmp(res.list.name, "Minimal") == 0);
    WF_CHECK(res.list.description == NULL);
    WF_CHECK(res.list.avatar == NULL);
    WF_CHECK(res.cursor == NULL);
    wf_mod_list_view_result_free(&res);
}

static void test_parse_list_view_invalid(void) {
    wf_mod_list_view_result res;

    WF_CHECK(wf_agent_parse_list_view_result(NULL, 0, &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_list_view_result("garbage", 7, &res) == WF_ERR_PARSE);

    const char *no_list = "{\"cursor\":\"x\"}";
    WF_CHECK(wf_agent_parse_list_view_result(no_list, strlen(no_list), &res) ==
             WF_ERR_PARSE);
}

static void test_wrappers_null_args(void) {
    wf_moderation_report r;
    wf_mod_list_view_result lvr;

    WF_CHECK(wf_agent_create_report(NULL, "x", "uri", NULL, &r) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_create_report((wf_agent *)1, "x", "uri", NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    /* both subject values NULL -> invalid */
    WF_CHECK(wf_agent_create_report((wf_agent *)1, "x", NULL, NULL, &r) ==
             WF_ERR_INVALID_ARG);
    /* both subject values set -> invalid */
    WF_CHECK(wf_agent_create_report((wf_agent *)1, "x", "uri", "did:x", &r) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_mute_actor_list(NULL, "uri", &lvr) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_mute_actor_list((wf_agent *)1, NULL, &lvr) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_mute_actor_list((wf_agent *)1, "uri", NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_block_actor_list(NULL, "uri", &lvr) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_block_actor_list((wf_agent *)1, NULL, &lvr) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_block_actor_list((wf_agent *)1, "uri", NULL) ==
             WF_ERR_INVALID_ARG);
}

int main(void) {
    test_parse_report();
    test_parse_report_repo_subject();
    test_parse_report_invalid();
    test_parse_list_view("muteActorList.json");
    test_parse_list_view("blockActorList.json");
    test_parse_list_view_missing_optional();
    test_parse_list_view_invalid();
    test_wrappers_null_args();
    WF_TEST_SUMMARY();
}
