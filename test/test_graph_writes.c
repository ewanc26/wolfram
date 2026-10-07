/*
 * test_graph_writes.c — offline integration tests for the app.bsky.graph
 * write wrappers (graph_write.c). Drives a real local libmicrohttpd mock PDS
 * (mock_pds.c) so the SDK transports end-to-end without touching the network.
 *
 * Exercises every create helper (asserts the returned uri/cid and the exact
 * request payload sent) plus the delete and mute/unmute procedures.
 *
 * Built only when WOLFRAM_BUILD_TEST_HTTPD=ON.
 */

#include "wolfram/graph_write.h"
#include "wolfram/agent.h"
#include "wolfram/attach.h"
#include "wolfram/syntax.h"

#include "mock_pds.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Assert a string field `key` inside object `obj` (NULL = top level) of the
 * JSON `body` equals `expect`. */
static int json_field_eq(const char *body, const char *obj, const char *key,
                         const char *expect) {
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return 0;
    }
    cJSON *cur = root;
    if (obj) {
        cJSON *o = cJSON_GetObjectItemCaseSensitive(root, obj);
        if (!cJSON_IsObject(o)) {
            cJSON_Delete(root);
            return 0;
        }
        cur = o;
    }
    cJSON *f = cJSON_GetObjectItemCaseSensitive(cur, key);
    int ok = cJSON_IsString(f) && strcmp(f->valuestring, expect) == 0;
    cJSON_Delete(root);
    return ok;
}

/* Assert a string field `key` inside object `obj` (NULL = top level) exists
 * and is a non-NULL string (value not yet checked). */
static int json_field_present(const char *body, const char *obj,
                              const char *key) {
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return 0;
    }
    cJSON *cur = root;
    if (obj) {
        cJSON *o = cJSON_GetObjectItemCaseSensitive(root, obj);
        if (!cJSON_IsObject(o)) {
            cJSON_Delete(root);
            return 0;
        }
        cur = o;
    }
    cJSON *f = cJSON_GetObjectItemCaseSensitive(cur, key);
    int ok = cJSON_IsString(f) && f->valuestring != NULL;
    cJSON_Delete(root);
    return ok;
}

/* Assert a top-level string field of `body` equals `expect`. */
static int json_top_eq(const char *body, const char *key, const char *expect) {
    return json_field_eq(body, NULL, key, expect);
}

/* Assert a top-level boolean field of `body` is present and `true`. */
static int json_top_is_true(const char *body, const char *key) {
    cJSON *root = cJSON_Parse(body);
    if (!root) return 0;
    cJSON *f = cJSON_GetObjectItemCaseSensitive(root, key);
    int ok = cJSON_IsTrue(f);
    cJSON_Delete(root);
    return ok;
}

/* Assert a top-level field of `body` is absent. */
static int json_top_absent(const char *body, const char *key) {
    cJSON *root = cJSON_Parse(body);
    if (!root) return 0;
    cJSON *f = cJSON_GetObjectItemCaseSensitive(root, key);
    int ok = (f == NULL);
    cJSON_Delete(root);
    return ok;
}

int main(void) {
    wf_mock_pds *pds = NULL;
    int port = 0;
    WF_CHECK(wf_mock_pds_start(&pds, &port) == WF_OK);
    WF_CHECK(pds != NULL);
    WF_CHECK(port > 0);

    /* Canned PDS responses. refreshSession is hit by wf_agent_resume. */
    const char *session_json =
        "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\","
        "\"accessJwt\":\"eyJ.fake.access\",\"refreshJwt\":\"eyJ.fake.refresh\","
        "\"active\":true}";
    const char *create_json =
        "{\"uri\":\"at://did:plc:abc123/app.bsky.graph.list/abcXX1\",\""
        "cid\":\"bafycidcreated\"}";
    const char *put_json =
        "{\"uri\":\"at://did:plc:abc123/app.bsky.graph.starterpack/abcXX2\",\""
        "cid\":\"bafycidput\"}";
    const char *empty_json = "{}";

    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.server.refreshSession",
                                  session_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.repo.createRecord",
                                  create_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.repo.putRecord",
                                  put_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.repo.deleteRecord",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.muteThread",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.unmuteThread",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.muteActorList",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.unmuteActorList",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.muteActor",
                                  empty_json) == WF_OK);

    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);

    wf_agent *agent = wf_agent_new(base_url);
    WF_CHECK(agent != NULL);

    /* Install a session so the agent is "logged in" (resume triggers a refresh
     * round-trip against the mock). */
    wf_session_data data;
    memset(&data, 0, sizeof(data));
    data.did = "did:plc:abc123";
    data.handle = "alice.test";
    data.access_jwt = "eyJ.fake.access";
    data.refresh_jwt = "eyJ.fake.refresh";
    data.active = 1;
    WF_CHECK(wf_agent_resume(agent, &data) == WF_OK);
    wf_session_data sd;
    memset(&sd, 0, sizeof(sd));
    WF_CHECK(wf_agent_get_session_data(agent, &sd) == WF_OK);
    WF_CHECK(sd.did != NULL && strcmp(sd.did, "did:plc:abc123") == 0);
    wf_agent_session_data_free(&sd);

    const char *last_nsid = NULL;
    const char *last_method = NULL;
    const char *last_body = NULL;

    /* ---- createList ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_list(
            agent, "app.bsky.graph.defs#curatelist", "My List", "a list", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        WF_CHECK(strncmp(out.uri, "at://", 5) == 0);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "com.atproto.repo.createRecord") == 0);
        WF_CHECK(last_method && strcmp(last_method, "POST") == 0);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.list"));
        WF_CHECK(
            json_field_eq(last_body, "record", "$type", "app.bsky.graph.list"));
        WF_CHECK(json_field_eq(last_body, "record", "name", "My List"));
        WF_CHECK(json_field_eq(last_body, "record", "purpose",
                               "app.bsky.graph.defs#curatelist"));
        WF_CHECK(json_field_eq(last_body, "record", "description", "a list"));
        WF_CHECK(json_field_present(last_body, "record", "createdAt"));
        wf_agent_post_result_free(&out);
    }

    /* ---- createListItem ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_list_item(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1",
            "did:plc:subject1", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(
            json_top_eq(last_body, "collection", "app.bsky.graph.listitem"));
        WF_CHECK(
            json_field_eq(last_body, "record", "subject", "did:plc:subject1"));
        WF_CHECK(
            json_field_eq(last_body, "record", "list",
                          "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
        wf_agent_post_result_free(&out);
    }

    /* ---- profile pinned post ---- */
    {
        WF_CHECK(
            wf_mock_pds_register(
                pds, "app.bsky.actor.getProfile",
                "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\","
                "\"pinnedPost\":{\"uri\":\"at://did:plc:abc123/app.bsky.feed."
                "post/pin1\",\"cid\":\"bafy\"}}") == WF_OK);
        wf_agent_profile prof;
        memset(&prof, 0, sizeof(prof));
        WF_CHECK(wf_agent_get_profile(agent, "alice.test", &prof) == WF_OK);
        WF_CHECK(prof.pinned_post_uri != NULL &&
                 strcmp(prof.pinned_post_uri,
                        "at://did:plc:abc123/app.bsky.feed.post/pin1") == 0);
        wf_agent_profile_free(&prof);

        WF_CHECK(
            wf_mock_pds_register(
                pds, "app.bsky.actor.getProfile",
                "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\"}") ==
            WF_OK);
        memset(&prof, 0, sizeof(prof));
        WF_CHECK(wf_agent_get_profile(agent, "alice.test", &prof) == WF_OK);
        WF_CHECK(prof.pinned_post_uri == NULL);
        wf_agent_profile_free(&prof);
    }

    /* ---- post langs ---- */
    {
        WF_CHECK(wf_agent_set_post_langs(agent, "en,cy") == WF_OK);
        wf_agent_post_result out = {0};
        WF_CHECK(wf_agent_post(agent, "hello", &out) == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        cJSON *root = cJSON_Parse(last_body);
        cJSON *rec =
            root ? cJSON_GetObjectItemCaseSensitive(root, "record") : NULL;
        cJSON *langs =
            rec ? cJSON_GetObjectItemCaseSensitive(rec, "langs") : NULL;
        WF_CHECK(cJSON_IsArray(langs) && cJSON_GetArraySize(langs) == 2);
        WF_CHECK(strcmp(cJSON_GetArrayItem(langs, 0)->valuestring, "en") == 0);
        WF_CHECK(strcmp(cJSON_GetArrayItem(langs, 1)->valuestring, "cy") == 0);
        cJSON_Delete(root);
        wf_agent_post_result_free(&out);

        /* Invalid input is refused and leaves the setting alone. */
        WF_CHECK(wf_agent_set_post_langs(agent, "e") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_set_post_langs(agent, "en,fr,de,es") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_set_post_langs(agent, "en,,fr") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_set_post_langs(agent, "en_US") == WF_ERR_INVALID_ARG);

        /* Cleared: no langs field. */
        WF_CHECK(wf_agent_set_post_langs(agent, NULL) == WF_OK);
        memset(&out, 0, sizeof(out));
        WF_CHECK(wf_agent_post(agent, "hello", &out) == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        root = cJSON_Parse(last_body);
        rec = root ? cJSON_GetObjectItemCaseSensitive(root, "record") : NULL;
        WF_CHECK(rec && !cJSON_GetObjectItemCaseSensitive(rec, "langs"));
        cJSON_Delete(root);
        wf_agent_post_result_free(&out);
    }

    /* ---- reply with embed ---- */
    {
        const char *embed = "{\"$type\":\"app.bsky.embed.images\","
                            "\"images\":[]}";
        wf_agent_post_result out = {0};
        WF_CHECK(wf_agent_reply_refs_with_embed(agent, "re", "at://r/p/1", "rc",
                                                "at://r/p/2", "pc", embed,
                                                &out) == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        cJSON *root = cJSON_Parse(last_body);
        cJSON *rec =
            root ? cJSON_GetObjectItemCaseSensitive(root, "record") : NULL;
        WF_CHECK(rec && cJSON_GetObjectItemCaseSensitive(rec, "reply"));
        cJSON *em = rec ? cJSON_GetObjectItemCaseSensitive(rec, "embed") : NULL;
        WF_CHECK(cJSON_IsObject(em));
        cJSON_Delete(root);
        wf_agent_post_result_free(&out);

        WF_CHECK(wf_agent_reply_refs_with_embed(agent, "re", "at://r/p/1", "rc",
                                                "at://r/p/2", "pc", "[1]",
                                                &out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_reply_refs_with_embed(agent, "re", "at://r/p/1", "rc",
                                                "at://r/p/2", "pc", "{bad",
                                                &out) == WF_ERR_PARSE);
    }

    /* ---- upload an image file as an embed ---- */
    {
        WF_CHECK(wf_mock_pds_register(
                     pds, "com.atproto.repo.uploadBlob",
                     "{\"blob\":{\"$type\":\"blob\",\"ref\":{\"$link\":"
                     "\"bafyimage\"},\"mimeType\":\"image/png\","
                     "\"size\":3}}") == WF_OK);
        char path[] = "/tmp/wf_attach_up_XXXXXX";
        int fd = mkstemp(path);
        WF_CHECK(fd >= 0);
        if (fd >= 0) {
            WF_CHECK(write(fd, "abc", 3) == 3);
            close(fd);
        }
        char png[64];
        snprintf(png, sizeof png, "%s.png", path);
        WF_CHECK(rename(path, png) == 0);

        cJSON *embed = NULL;
        WF_CHECK(wf_agent_upload_image_file(agent, png, "a cat", &embed) ==
                 WF_OK);
        cJSON *images =
            embed ? cJSON_GetObjectItemCaseSensitive(embed, "images") : NULL;
        cJSON *img0 = cJSON_GetArrayItem(images, 0);
        cJSON *alt =
            img0 ? cJSON_GetObjectItemCaseSensitive(img0, "alt") : NULL;
        cJSON *blob =
            img0 ? cJSON_GetObjectItemCaseSensitive(img0, "image") : NULL;
        cJSON *ref =
            blob ? cJSON_GetObjectItemCaseSensitive(blob, "ref") : NULL;
        cJSON *link =
            ref ? cJSON_GetObjectItemCaseSensitive(ref, "$link") : NULL;
        WF_CHECK(cJSON_IsString(alt) && strcmp(alt->valuestring, "a cat") == 0);
        WF_CHECK(cJSON_IsString(link) &&
                 strcmp(link->valuestring, "bafyimage") == 0);
        cJSON_Delete(embed);

        /* Refusals: wrong type, missing file, bad arguments. *embed stays NULL.
         */
        embed = (cJSON *)1;
        WF_CHECK(wf_agent_upload_image_file(agent, "/tmp/nothing.gif", NULL,
                                            &embed) == WF_ERR_INVALID_ARG);
        WF_CHECK(embed == NULL);
        WF_CHECK(wf_agent_upload_image_file(agent, "/tmp/wf_attach_missing.png",
                                            NULL, &embed) == WF_ERR_NOT_FOUND);
        WF_CHECK(wf_agent_upload_image_file(NULL, png, NULL, &embed) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_upload_image_file(agent, png, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        remove(png);
    }

    /* ---- post a thread ---- */
    {
        const char *const texts[] = {"one", "two", "three"};
        wf_agent_post_result first = {0}, last = {0};
        size_t posted = 99;

        WF_CHECK(wf_agent_post_thread(agent, texts, 3, &posted, &first,
                                      &last) == WF_OK);
        WF_CHECK(posted == 3);
        WF_CHECK(first.uri && last.uri && first.cid && last.cid);
        /* The mock answers every createRecord with the same ref, so the last
         * request must be a reply whose root and parent are that ref. */
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        cJSON *root = cJSON_Parse(last_body);
        cJSON *rec =
            root ? cJSON_GetObjectItemCaseSensitive(root, "record") : NULL;
        cJSON *reply =
            rec ? cJSON_GetObjectItemCaseSensitive(rec, "reply") : NULL;
        cJSON *rt =
            reply ? cJSON_GetObjectItemCaseSensitive(reply, "root") : NULL;
        cJSON *pa =
            reply ? cJSON_GetObjectItemCaseSensitive(reply, "parent") : NULL;
        cJSON *ru = rt ? cJSON_GetObjectItemCaseSensitive(rt, "uri") : NULL;
        cJSON *pu = pa ? cJSON_GetObjectItemCaseSensitive(pa, "uri") : NULL;
        WF_CHECK(cJSON_IsString(ru) && strcmp(ru->valuestring, first.uri) == 0);
        WF_CHECK(cJSON_IsString(pu) && strcmp(pu->valuestring, first.uri) == 0);
        cJSON_Delete(root);
        wf_agent_post_result_free(&first);
        wf_agent_post_result_free(&last);

        /* One text is a plain post; the outputs are optional. */
        WF_CHECK(wf_agent_post_thread(agent, texts, 1, &posted, NULL, NULL) ==
                 WF_OK);
        WF_CHECK(posted == 1);

        /* Bad arguments post nothing. */
        const char *const bad[] = {"ok", ""};
        WF_CHECK(wf_agent_post_thread(agent, bad, 2, &posted, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(posted == 0);
        WF_CHECK(wf_agent_post_thread(agent, texts, 0, &posted, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_post_thread(agent, NULL, 2, &posted, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- searchPosts typed ---- */
    {
        WF_CHECK(
            wf_mock_pds_register(
                pds, "app.bsky.feed.searchPosts",
                "{\"cursor\":\"next1\",\"posts\":[{\"uri\":\"at://did:plc:x/"
                "app.bsky.feed.post/1\",\"cid\":\"bafy\",\"author\":{\"did\":"
                "\"did:plc:x\",\"handle\":\"x.test\"},\"record\":{\"text\":"
                "\"hi\",\"createdAt\":\"2026-01-01T00:00:00Z\"},"
                "\"indexedAt\":\"2026-01-01T00:00:00Z\"}]}") == WF_OK);
        wf_agent_post_list pl;
        memset(&pl, 0, sizeof(pl));
        char *nc = NULL;
        WF_CHECK(wf_agent_search_posts_typed(agent, "hello", 25, NULL, &pl,
                                             &nc) == WF_OK);
        WF_CHECK(pl.post_count == 1);
        WF_CHECK(nc && strcmp(nc, "next1") == 0);
        free(nc);
        wf_agent_post_list_free(&pl);
        WF_CHECK(wf_agent_search_posts_typed(agent, "", 25, NULL, &pl, NULL) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- createStarterPack ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_starter_pack(
            agent, "My Pack", "at://did:plc:abc123/app.bsky.graph.list/abcXX1",
            "a pack",
            "[{\"uri\":\"at://did:plc:abc123/app.bsky.feed.generator/feed1\"}]",
            &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(
            json_top_eq(last_body, "collection", "app.bsky.graph.starterpack"));
        WF_CHECK(json_field_eq(last_body, "record", "name", "My Pack"));
        WF_CHECK(json_field_eq(last_body, "record", "description", "a pack"));
        WF_CHECK(
            json_field_eq(last_body, "record", "list",
                          "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
        cJSON *root = cJSON_Parse(last_body);
        cJSON *rec =
            root ? cJSON_GetObjectItemCaseSensitive(root, "record") : NULL;
        cJSON *feeds =
            rec ? cJSON_GetObjectItemCaseSensitive(rec, "feeds") : NULL;
        WF_CHECK(cJSON_IsArray(feeds) && cJSON_GetArraySize(feeds) == 1);
        cJSON_Delete(root);
        wf_agent_post_result_free(&out);
    }

    /* ---- createListBlock ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_list_block(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(
            json_top_eq(last_body, "collection", "app.bsky.graph.listblock"));
        WF_CHECK(
            json_field_eq(last_body, "record", "subject",
                          "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
        wf_agent_post_result_free(&out);
    }

    /* ---- block ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_block(agent, "did:plc:blockme", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.block"));
        WF_CHECK(
            json_field_eq(last_body, "record", "subject", "did:plc:blockme"));
        wf_agent_post_result_free(&out);
    }

    /* ---- updateStarterPack (putRecord) ---- */
    {
        wf_agent_post_result out = {0};
        const char *new_rec =
            "{\"$type\":\"app.bsky.graph.starterpack\",\"name\":\"Renamed\","
            "\"list\":\"at://did:plc:abc123/app.bsky.graph.list/abcXX1\","
            "\"createdAt\":\"2026-07-09T00:00:00Z\"}";
        wf_status st =
            wf_agent_graph_update_starter_pack(agent, "abcXX2", new_rec, &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && strstr(out.uri, "starterpack") != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "com.atproto.repo.putRecord") == 0);
        WF_CHECK(
            json_top_eq(last_body, "collection", "app.bsky.graph.starterpack"));
        WF_CHECK(json_top_eq(last_body, "rkey", "abcXX2"));
        wf_agent_post_result_free(&out);
    }

    /* ---- deletes ---- */
    {
        wf_status st = wf_agent_graph_delete_list(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "com.atproto.repo.deleteRecord") == 0);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.list"));
        WF_CHECK(json_top_eq(last_body, "rkey", "abcXX1"));
    }
    {
        wf_status st = wf_agent_graph_delete_list_item(
            agent, "at://did:plc:abc123/app.bsky.graph.listitem/abcXX9");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(
            json_top_eq(last_body, "collection", "app.bsky.graph.listitem"));
    }
    {
        wf_status st = wf_agent_graph_delete_starter_pack(
            agent, "at://did:plc:abc123/app.bsky.graph.starterpack/abcXX2");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(
            json_top_eq(last_body, "collection", "app.bsky.graph.starterpack"));
    }
    {
        wf_status st = wf_agent_graph_delete_list_block(
            agent, "at://did:plc:abc123/app.bsky.graph.listblock/abcXX3");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(
            json_top_eq(last_body, "collection", "app.bsky.graph.listblock"));
    }
    {
        wf_status st = wf_agent_graph_unblock(
            agent, "at://did:plc:abc123/app.bsky.graph.block/abcXX4");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.block"));
    }

    /* ---- mute / unmute procedures ---- */
    {
        wf_status st = wf_agent_graph_mute_thread(
            agent, "at://did:plc:abc123/app.bsky.feed.post/root1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.graph.muteThread") == 0);
        WF_CHECK(json_top_eq(last_body, "root",
                             "at://did:plc:abc123/app.bsky.feed.post/root1"));
    }
    {
        wf_status st = wf_agent_graph_unmute_thread(
            agent, "at://did:plc:abc123/app.bsky.feed.post/root1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.graph.unmuteThread") == 0);
    }
    {
        wf_status st = wf_agent_graph_mute_actor_list(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.graph.muteActorList") == 0);
        WF_CHECK(json_top_eq(last_body, "list",
                             "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
    }
    {
        wf_status st = wf_agent_graph_unmute_actor_list(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.graph.unmuteActorList") == 0);
    }
    {
        /* Scoped mute: the request body must carry onlyReposts/onlyQuoteposts,
         * not just the actor. */
        wf_status st =
            wf_agent_mute_actor_scoped(agent, "did:plc:abc123", true, false);
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.graph.muteActor") == 0);
        WF_CHECK(json_top_eq(last_body, "actor", "did:plc:abc123"));
        WF_CHECK(json_top_is_true(last_body, "onlyReposts"));
        WF_CHECK(json_top_absent(last_body, "onlyQuoteposts"));
    }
    {
        /* Unscoped scoped-call == full mute: neither scope key present. */
        wf_status st =
            wf_agent_mute_actor_scoped(agent, "did:plc:abc123", false, false);
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.graph.muteActor") == 0);
        WF_CHECK(json_top_eq(last_body, "actor", "did:plc:abc123"));
        WF_CHECK(json_top_absent(last_body, "onlyReposts"));
        WF_CHECK(json_top_absent(last_body, "onlyQuoteposts"));
    }

    /* ---- invalid-arg guards ---- */
    {
        wf_agent_post_result out = {0};
        WF_CHECK(wf_agent_graph_block(agent, NULL, &out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_graph_create_list(agent, NULL, "n", NULL, &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_graph_create_list_item(
                     agent, "not-an-uri", "did:plc:x", &out) == WF_ERR_PARSE);
        WF_CHECK(wf_agent_graph_delete_list(
                     agent, "at://did:plc:other/app.bsky.graph.list/x") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_graph_mute_thread(agent, "bad-uri") == WF_ERR_PARSE);
    }

    wf_agent_free(agent);
    wf_mock_pds_free(pds);

    WF_TEST_SUMMARY();
}
