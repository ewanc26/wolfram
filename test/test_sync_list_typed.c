/*
 * test_sync_list_typed.c — offline tests for the com.atproto.sync list/host/
 * crawl typed parsers and their agent wrappers. Parsers are exercised against
 * sample JSON built with cJSON; agent wrappers are asserted to reject NULL
 * agent / missing required args with WF_ERR_INVALID_ARG. No network is used.
 */

#include "wolfram/sync_list_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Render a cJSON tree to an owned string (caller frees with free()). */
static char *render(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    return s;
}

int main(void) {
    /* ── listRepos parse ─────────────────────────────────────── */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *repos = cJSON_AddArrayToObject(root, "repos");

        cJSON *r0 = cJSON_CreateObject();
        cJSON_AddStringToObject(r0, "did", "did:plc:a");
        cJSON_AddStringToObject(r0, "head", "bafyhead");
        cJSON_AddStringToObject(r0, "rev", "3lkrev");
        cJSON_AddBoolToObject(r0, "active", 1);
        cJSON_AddStringToObject(r0, "status", "deactivated");
        cJSON_AddItemToArray(repos, r0);

        cJSON *r1 = cJSON_CreateObject();
        cJSON_AddStringToObject(r1, "did", "did:plc:b");
        cJSON_AddStringToObject(r1, "head", "bafyhead2");
        cJSON_AddStringToObject(r1, "rev", "3lkrev2");
        cJSON_AddBoolToObject(r1, "active", 0);
        cJSON_AddItemToArray(repos, r1);

        cJSON_AddStringToObject(root, "cursor", "c1");

        char *json = render(root);
        cJSON_Delete(root);

        wf_sync_repo_ref_list list;
        memset(&list, 0, sizeof(list));
        WF_CHECK(wf_sync_parse_repo_list(json, strlen(json), &list) == WF_OK);
        WF_CHECK(list.count == 2);
        WF_CHECK(list.cursor && strcmp(list.cursor, "c1") == 0);
        WF_CHECK(list.items[0].did &&
                 strcmp(list.items[0].did, "did:plc:a") == 0);
        WF_CHECK(list.items[0].head &&
                 strcmp(list.items[0].head, "bafyhead") == 0);
        WF_CHECK(list.items[0].rev &&
                 strcmp(list.items[0].rev, "3lkrev") == 0);
        WF_CHECK(list.items[0].has_active && list.items[0].active);
        WF_CHECK(list.items[0].has_status &&
                 strcmp(list.items[0].status, "deactivated") == 0);
        WF_CHECK(list.items[1].did &&
                 strcmp(list.items[1].did, "did:plc:b") == 0);
        WF_CHECK(list.items[1].has_active && !list.items[1].active);
        WF_CHECK(!list.items[1].has_status);

        wf_sync_repo_ref_list_free(&list);
        WF_CHECK(list.items == NULL && list.cursor == NULL && list.count == 0);
        free(json);
    }

    /* ── listReposByCollection parse ─────────────────────────── */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *repos = cJSON_AddArrayToObject(root, "repos");
        cJSON *r0 = cJSON_CreateObject();
        cJSON_AddStringToObject(r0, "did", "did:plc:x");
        cJSON_AddItemToArray(repos, r0);
        cJSON *r1 = cJSON_CreateObject();
        cJSON_AddStringToObject(r1, "did", "did:plc:y");
        cJSON_AddItemToArray(repos, r1);
        cJSON_AddStringToObject(root, "cursor", "c4");

        char *json = render(root);
        cJSON_Delete(root);

        wf_sync_repo_by_collection_list list;
        memset(&list, 0, sizeof(list));
        WF_CHECK(wf_sync_parse_repo_by_collection_list(
                     json, strlen(json), &list) == WF_OK);
        WF_CHECK(list.count == 2);
        WF_CHECK(list.cursor && strcmp(list.cursor, "c4") == 0);
        WF_CHECK(list.items[0].did &&
                 strcmp(list.items[0].did, "did:plc:x") == 0);
        WF_CHECK(list.items[1].did &&
                 strcmp(list.items[1].did, "did:plc:y") == 0);

        wf_sync_repo_by_collection_list_free(&list);
        WF_CHECK(list.items == NULL && list.cursor == NULL);
        free(json);
    }

    /* ── listBlobs parse ─────────────────────────────────────── */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *cids = cJSON_AddArrayToObject(root, "cids");
        cJSON_AddItemToArray(cids, cJSON_CreateString("bafycid1"));
        cJSON_AddItemToArray(cids, cJSON_CreateString("bafycid2"));
        cJSON_AddStringToObject(root, "cursor", "c2");

        char *json = render(root);
        cJSON_Delete(root);

        wf_sync_blob_cid_list list;
        memset(&list, 0, sizeof(list));
        WF_CHECK(wf_sync_parse_blob_cid_list(json, strlen(json), &list) == WF_OK);
        WF_CHECK(list.count == 2);
        WF_CHECK(list.cursor && strcmp(list.cursor, "c2") == 0);
        WF_CHECK(list.cids[0] && strcmp(list.cids[0], "bafycid1") == 0);
        WF_CHECK(list.cids[1] && strcmp(list.cids[1], "bafycid2") == 0);

        wf_sync_blob_cid_list_free(&list);
        WF_CHECK(list.cids == NULL && list.cursor == NULL && list.count == 0);
        free(json);
    }

    /* ── listHosts parse ─────────────────────────────────────── */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *hosts = cJSON_AddArrayToObject(root, "hosts");

        cJSON *h0 = cJSON_CreateObject();
        cJSON_AddStringToObject(h0, "hostname", "h1");
        cJSON_AddNumberToObject(h0, "seq", 1);
        cJSON_AddNumberToObject(h0, "accountCount", 10);
        cJSON_AddStringToObject(h0, "status", "active");
        cJSON_AddItemToArray(hosts, h0);

        cJSON *h1 = cJSON_CreateObject();
        cJSON_AddStringToObject(h1, "hostname", "h2");
        cJSON_AddItemToArray(hosts, h1);

        cJSON_AddStringToObject(root, "cursor", "c3");

        char *json = render(root);
        cJSON_Delete(root);

        wf_sync_host_list list;
        memset(&list, 0, sizeof(list));
        WF_CHECK(wf_sync_parse_host_list(json, strlen(json), &list) == WF_OK);
        WF_CHECK(list.count == 2);
        WF_CHECK(list.cursor && strcmp(list.cursor, "c3") == 0);
        WF_CHECK(list.items[0].hostname &&
                 strcmp(list.items[0].hostname, "h1") == 0);
        WF_CHECK(list.items[0].has_seq && list.items[0].seq == 1);
        WF_CHECK(list.items[0].has_account_count &&
                 list.items[0].account_count == 10);
        WF_CHECK(list.items[0].has_status &&
                 strcmp(list.items[0].status, "active") == 0);
        WF_CHECK(list.items[1].hostname &&
                 strcmp(list.items[1].hostname, "h2") == 0);
        WF_CHECK(!list.items[1].has_seq && !list.items[1].has_status);

        wf_sync_host_list_free(&list);
        WF_CHECK(list.items == NULL && list.cursor == NULL);
        free(json);
    }

    /* ── getHostStatus parse ─────────────────────────────────── */
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "hostname", "pds.example.com");
        cJSON_AddNumberToObject(root, "seq", 123);
        cJSON_AddNumberToObject(root, "accountCount", 42);
        cJSON_AddStringToObject(root, "status", "active");

        char *json = render(root);
        cJSON_Delete(root);

        wf_sync_host h;
        memset(&h, 0, sizeof(h));
        WF_CHECK(wf_sync_parse_host(json, strlen(json), &h) == WF_OK);
        WF_CHECK(h.hostname && strcmp(h.hostname, "pds.example.com") == 0);
        WF_CHECK(h.has_seq && h.seq == 123);
        WF_CHECK(h.has_account_count && h.account_count == 42);
        WF_CHECK(h.has_status && strcmp(h.status, "active") == 0);

        wf_sync_host_free(&h);
        WF_CHECK(h.hostname == NULL && h.status == NULL);
        free(json);
    }

    /* ── parser rejects NULL ─────────────────────────────────── */
    {
        wf_sync_repo_ref_list list;
        memset(&list, 0, sizeof(list));
        WF_CHECK(wf_sync_parse_repo_list(NULL, 0, &list) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_parse_repo_list("{}", 2, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_parse_blob_cid_list(NULL, 0, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_parse_host_list(NULL, 0, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_parse_host(NULL, 0, NULL) == WF_ERR_INVALID_ARG);
    }

    /* ── agent wrappers reject NULL/empty args ───────────────── */
    {
        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_sync_repo_ref_list lr;
        memset(&lr, 0, sizeof(lr));
        WF_CHECK(wf_agent_list_repos_typed(NULL, 0, NULL, &lr) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_repos_typed(agent, 0, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_sync_repo_by_collection_list lc;
        memset(&lc, 0, sizeof(lc));
        WF_CHECK(wf_agent_list_repos_by_collection_typed(
                     NULL, "col", 0, NULL, &lc) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_repos_by_collection_typed(
                     agent, NULL, 0, NULL, &lc) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_repos_by_collection_typed(
                     agent, "", 0, NULL, &lc) == WF_ERR_INVALID_ARG);

        wf_sync_blob_cid_list lb;
        memset(&lb, 0, sizeof(lb));
        WF_CHECK(wf_agent_list_blobs_typed(NULL, "did", NULL, 0, NULL, &lb) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_blobs_typed(agent, NULL, NULL, 0, NULL, &lb) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_blobs_typed(agent, "", NULL, 0, NULL, &lb) ==
                 WF_ERR_INVALID_ARG);

        wf_sync_host_list lh;
        memset(&lh, 0, sizeof(lh));
        WF_CHECK(wf_agent_list_hosts_typed(NULL, 0, NULL, &lh) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_list_hosts_typed(agent, 0, NULL, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_sync_host host;
        memset(&host, 0, sizeof(host));
        WF_CHECK(wf_agent_get_host_status_typed(NULL, "h", &host) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_host_status_typed(agent, NULL, &host) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_host_status_typed(agent, "", &host) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_notify_of_update_typed(NULL, "h") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_notify_of_update_typed(agent, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_notify_of_update_typed(agent, "") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_crawl_typed(NULL, "h") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_crawl_typed(agent, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_request_crawl_typed(agent, "") ==
                 WF_ERR_INVALID_ARG);

        unsigned char *bytes = NULL;
        size_t len = 0;
        WF_CHECK(wf_agent_get_repo_car(NULL, "did", NULL, &bytes, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_repo_car(agent, NULL, NULL, &bytes, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_repo_car(agent, "", NULL, &bytes, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_repo_car(agent, "did", NULL, NULL, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_checkout_car(NULL, "did", &bytes, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_checkout_car(agent, NULL, &bytes, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_checkout_car(agent, "", &bytes, &len) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_checkout_car(agent, "did", &bytes, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
