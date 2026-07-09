/*
 * test_sync_typed.c — offline tests for the com.atproto.sync typed parsers
 * (getRepoStatus, getLatestCommit, getBlocks, getRecord) and their agent
 * wrappers. The CAR-based parsers are exercised against a real CAR built with
 * the existing repo machinery (wf_repo_create_record + wf_car_write), mirroring
 * test/test_repo.c; no network is used.
 */

#include "wolfram/sync_typed.h"
#include "wolfram/repo.h"
#include "wolfram/crypto.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a small repo CAR containing one record and return its bytes. */
static unsigned char *build_fixture_car(size_t *out_len) {
    wf_signing_key key;
    memset(&key, 0, sizeof(key));
    if (wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key) != WF_OK) {
        return NULL;
    }

    wf_car car;
    memset(&car, 0, sizeof(car));

    unsigned char record[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x18, 0x7B};
    wf_cid out_commit, out_record;
    memset(&out_commit, 0, sizeof(out_commit));
    memset(&out_record, 0, sizeof(out_record));

    if (wf_repo_create_record(&car, NULL, "did:plc:test", "com.example.posts",
                              "3jui7kd54zh2y", record, sizeof(record), &key,
                              &out_commit, &out_record) != WF_OK) {
        return NULL;
    }

    /* A real getRecord/getBlocks CAR carries the commit as its single root.
     * wf_repo_create_record computes the commit CID but leaves car->roots
     * unset, so set it here to mirror a valid server response. */
    car.roots = (wf_cid *)malloc(sizeof(wf_cid));
    if (!car.roots) {
        wf_car_free(&car);
        return NULL;
    }
    car.roots[0] = out_commit;
    car.root_count = 1;

    unsigned char *bytes = NULL;
    size_t len = 0;
    if (wf_car_write(&car, &bytes, &len) != WF_OK) {
        wf_car_free(&car);
        return NULL;
    }
    wf_car_free(&car);

    if (out_len) *out_len = len;
    return bytes;
}

int main(void) {
    /* ── getRepoStatus parse ─────────────────────────────────────── */
    {
        const char *json =
            "{\"did\":\"did:plc:abc123\",\"active\":true,"
            "\"status\":\"deactivated\",\"rev\":\"3lkabc\"}";
        wf_sync_repo_status_typed s;
        memset(&s, 0, sizeof(s));
        WF_CHECK(wf_sync_repo_status_typed_parse(json, strlen(json), &s) == WF_OK);
        WF_CHECK(s.did && strcmp(s.did, "did:plc:abc123") == 0);
        WF_CHECK(s.active == 1);
        WF_CHECK(s.status && strcmp(s.status, "deactivated") == 0);
        WF_CHECK(s.rev && strcmp(s.rev, "3lkabc") == 0);
        wf_sync_repo_status_typed_free(&s);
        WF_CHECK(s.did == NULL && s.status == NULL && s.rev == NULL);
    }

    /* optional fields may be absent (active=false case) */
    {
        const char *json = "{\"did\":\"did:plc:x\",\"active\":false}";
        wf_sync_repo_status_typed s;
        memset(&s, 0, sizeof(s));
        WF_CHECK(wf_sync_repo_status_typed_parse(json, strlen(json), &s) == WF_OK);
        WF_CHECK(s.active == 0);
        WF_CHECK(s.status == NULL);
        WF_CHECK(s.rev == NULL);
        wf_sync_repo_status_typed_free(&s);
    }

    /* ── getLatestCommit parse ──────────────────────────────────── */
    {
        const char *json =
            "{\"cid\":\"bafyreighcommitxxxx\",\"rev\":\"3lkrev\"}";
        wf_sync_latest_commit c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_sync_latest_commit_parse(json, strlen(json), &c) == WF_OK);
        WF_CHECK(c.cid && strcmp(c.cid, "bafyreighcommitxxxx") == 0);
        WF_CHECK(c.rev && strcmp(c.rev, "3lkrev") == 0);
        wf_sync_latest_commit_free(&c);
        WF_CHECK(c.cid == NULL && c.rev == NULL);
    }

    /* ── getHead parse (deprecated) ─────────────────────────────── */
    {
        const char *json = "{\"root\":\"bafyreigheadxxxx\"}";
        wf_sync_head_typed h;
        memset(&h, 0, sizeof(h));
        WF_CHECK(wf_sync_head_typed_parse(json, strlen(json), &h) == WF_OK);
        WF_CHECK(h.root && strcmp(h.root, "bafyreigheadxxxx") == 0);
        wf_sync_head_typed_free(&h);
        WF_CHECK(h.root == NULL);

        /* required root missing -> parse error; NULL args -> invalid arg */
        WF_CHECK(wf_sync_head_typed_parse(NULL, 0, &h) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_head_typed_parse("{}", 2, &h) == WF_ERR_PARSE);
        wf_sync_head_typed_free(&h);
    }

    /* ── arg / malformed validation ─────────────────────────────── */
    {
        wf_sync_repo_status_typed s;
        memset(&s, 0, sizeof(s));
        WF_CHECK(wf_sync_repo_status_typed_parse(NULL, 0, &s) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_repo_status_typed_parse("{not json", 9, &s) == WF_ERR_PARSE);
        WF_CHECK(wf_sync_repo_status_typed_parse("{}", 2, &s) == WF_ERR_PARSE);
        wf_sync_repo_status_typed_free(&s);

        wf_sync_latest_commit c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_sync_latest_commit_parse(NULL, 0, &c) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sync_latest_commit_parse("{\"cid\":\"x\"}", 11, &c) ==
                 WF_ERR_PARSE);
        wf_sync_latest_commit_free(&c);
    }

    /* ── getBlocks / getRecord CAR parse ─────────────────────────── */
    size_t car_len = 0;
    unsigned char *car = build_fixture_car(&car_len);
    WF_CHECK(car != NULL);
    WF_CHECK(car_len > 0);

    if (car) {
        /* getBlocks: every CAR block becomes an owned {cid, value} entry. */
        wf_sync_block_list bl;
        memset(&bl, 0, sizeof(bl));
        WF_CHECK(wf_sync_block_list_parse_car(car, car_len, &bl) == WF_OK);
        WF_CHECK(bl.count >= 2);
        for (size_t i = 0; i < bl.count; ++i) {
            WF_CHECK(bl.items[i].cid != NULL);
            WF_CHECK(bl.items[i].value != NULL);
            WF_CHECK(bl.items[i].value_len > 0);
        }
        wf_sync_block_list_free(&bl);
        WF_CHECK(bl.items == NULL && bl.count == 0);

        /* getRecord existence: extract the record CBOR + repo rev. */
        wf_sync_record rec;
        memset(&rec, 0, sizeof(rec));
        WF_CHECK(wf_sync_record_parse_car(car, car_len, "com.example.posts",
                                          "3jui7kd54zh2y", &rec) == WF_OK);
        unsigned char expected[] = {0xA1, 0x64, 't', 'e', 's', 't', 0x18, 0x7B};
        WF_CHECK(rec.record_cbor != NULL);
        WF_CHECK(rec.record_len == sizeof(expected));
        WF_CHECK(memcmp(rec.record_cbor, expected, sizeof(expected)) == 0);
        WF_CHECK(rec.repo_rev != NULL && rec.repo_rev[0] != '\0');
        wf_sync_record_free(&rec);
        WF_CHECK(rec.record_cbor == NULL && rec.repo_rev == NULL);

        /* getRecord non-existence: graceful, no record, rev still present. */
        wf_sync_record absent;
        memset(&absent, 0, sizeof(absent));
        WF_CHECK(wf_sync_record_parse_car(car, car_len, "com.example.posts",
                                          "nopey22222222", &absent) == WF_OK);
        WF_CHECK(absent.record_cbor == NULL);
        WF_CHECK(absent.record_len == 0);
        WF_CHECK(absent.repo_rev != NULL && absent.repo_rev[0] != '\0');
        wf_sync_record_free(&absent);

        /* malformed CAR -> parse error */
        wf_sync_block_list bad;
        memset(&bad, 0, sizeof(bad));
        WF_CHECK(wf_sync_block_list_parse_car((const unsigned char *)"garbage",
                                             7, &bad) != WF_OK);
        wf_sync_block_list_free(&bad);

        wf_sync_record badrec;
        memset(&badrec, 0, sizeof(badrec));
        WF_CHECK(wf_sync_record_parse_car(NULL, 0, "c", "r", &badrec) ==
                 WF_ERR_INVALID_ARG);
        wf_sync_record_free(&badrec);

        free(car);
    }

    /* ── agent wrapper arg validation (offline, no network) ───────── */
    {
        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_sync_repo_status_typed s;
        memset(&s, 0, sizeof(s));
        WF_CHECK(wf_agent_get_repo_status_typed(NULL, "did:plc:x", &s) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_repo_status_typed(agent, NULL, &s) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_repo_status_typed(agent, "did:plc:x", NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_sync_repo_status_typed_free(&s);

        wf_sync_latest_commit c;
        memset(&c, 0, sizeof(c));
        WF_CHECK(wf_agent_get_latest_commit_typed(NULL, "did:plc:x", &c) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_latest_commit_typed(agent, NULL, &c) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_latest_commit_typed(agent, "did:plc:x", NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_sync_latest_commit_free(&c);

        wf_sync_head_typed h;
        memset(&h, 0, sizeof(h));
        WF_CHECK(wf_agent_get_head_typed(NULL, "did:plc:x", &h) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_head_typed(agent, NULL, &h) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_head_typed(agent, "did:plc:x", NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_sync_head_typed_free(&h);

        const char *cids[] = {"bafycid"};
        wf_sync_block_list bl;
        memset(&bl, 0, sizeof(bl));
        WF_CHECK(wf_agent_sync_get_blocks_typed(NULL, "did:plc:x", cids, 1, &bl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blocks_typed(agent, NULL, cids, 1, &bl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blocks_typed(agent, "did:plc:x", NULL, 0, &bl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_blocks_typed(agent, "did:plc:x", cids, 0, &bl) ==
                 WF_ERR_INVALID_ARG);
        wf_sync_block_list_free(&bl);

        wf_sync_record rec;
        memset(&rec, 0, sizeof(rec));
        WF_CHECK(wf_agent_sync_get_record_typed(NULL, "did:plc:x", "c", "r", &rec) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record_typed(agent, NULL, "c", "r", &rec) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record_typed(agent, "did:plc:x", NULL, "r", &rec) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record_typed(agent, "did:plc:x", "c", NULL, &rec) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_sync_get_record_typed(agent, "did:plc:x", "c", "r", NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_sync_record_free(&rec);

        /* ---- getBlob argument validation ---- */
        {
            uint8_t *data = NULL;
            size_t len = 0;
            WF_CHECK(wf_agent_get_blob_typed(NULL, "did:plc:x", "cid", &data,
                                            &len) == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_blob_typed(agent, NULL, "cid", &data,
                                            &len) == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_blob_typed(agent, "did:plc:x", NULL, &data,
                                            &len) == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_blob_typed(agent, "did:plc:x", "cid", NULL,
                                            &len) == WF_ERR_INVALID_ARG);
            WF_CHECK(wf_agent_get_blob_typed(agent, "did:plc:x", "cid", &data,
                                            NULL) == WF_ERR_INVALID_ARG);
        }

        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
