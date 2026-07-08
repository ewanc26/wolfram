#include "wolfram/verify.h"
#include "wolfram/agent.h"
#include "wolfram/crypto.h"
#include "wolfram/repo/commit.h"
#include "wolfram/repo/mst.h"
#include "wolfram/repo/car.h"
#include "wolfram/repo/cid.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* Build a repository CAR containing a single signed commit whose MST root is
 * an empty tree. Mirrors test_sync_verify.c's construction but adds the MST
 * node block and a proper CAR root so wf_repo_verify (the core behind
 * wf_sync_verify_commit) can fully validate the commit. */
static int build_signed_car(const wf_signing_key *key, const char *did,
                            unsigned char **out, size_t *out_len)
{
    wf_car car;
    memset(&car, 0, sizeof(car));

    wf_mst_node node;
    memset(&node, 0, sizeof(node));
    if (wf_mst_node_build(0, NULL, NULL, 0, &node) != WF_OK) {
        wf_mst_node_free(&node);
        return 0;
    }
    if (wf_mst_node_finalize(&node, &car) != WF_OK) {
        wf_mst_node_free(&node);
        wf_car_free(&car);
        return 0;
    }
    wf_cid mst_root = node.cid;
    wf_mst_node_free(&node);

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    if (wf_commit_create(did, "3abcdefghijklmnop", &mst_root, NULL,
                         key, &car, &commit) != WF_OK) {
        wf_car_free(&car);
        return 0;
    }

    car.roots = malloc(sizeof(wf_cid));
    if (!car.roots) {
        wf_car_free(&car);
        return 0;
    }
    car.roots[0] = commit.cid;
    car.root_count = 1;

    if (wf_car_write(&car, out, out_len) != WF_OK) {
        wf_car_free(&car);
        return 0;
    }

    wf_car_free(&car);
    return 1;
}

/* ── wf_verify_record_commit argument validation ── */

static void test_verify_record_commit_null_args(void)
{
    unsigned char dummy = 0x01;
    int valid = 0;

    WF_CHECK(wf_verify_record_commit(NULL, &dummy, 1, &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_verify_record_commit("", &dummy, 1, &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_verify_record_commit("zk", NULL, 0, &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_verify_record_commit("zk", &dummy, 0, &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_verify_record_commit("zk", &dummy, 1, NULL) == WF_ERR_INVALID_ARG);
}

/* ── wf_verify_record_commit happy / negative path ── */

static void test_verify_record_commit_good_and_tampered(void)
{
    wf_signing_key key1, key2;
    memset(&key1, 0, sizeof(key1));
    memset(&key2, 0, sizeof(key2));
    if (wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key1) != WF_OK)
        return; /* crypto unavailable (no libsecp256k1) — skip gracefully */
    if (wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key2) != WF_OK)
        return;

    char *didkey1 = NULL;
    char *didkey2 = NULL;
    if (wf_signing_key_public_didkey(&key1, &didkey1) != WF_OK) return;
    if (wf_signing_key_public_didkey(&key2, &didkey2) != WF_OK) {
        free(didkey1);
        return;
    }

    unsigned char *car1 = NULL, *car2 = NULL;
    size_t len1 = 0, len2 = 0;
    if (!build_signed_car(&key1, didkey1, &car1, &len1) ||
        !build_signed_car(&key2, didkey2, &car2, &len2)) {
        free(didkey1); free(didkey2);
        free(car1); free(car2);
        return;
    }

    int valid = 0;

    /* Good commit verifies against its own signing key. */
    WF_CHECK(wf_verify_record_commit(didkey1, car1, len1, &valid) == WF_OK);
    WF_CHECK(valid == 1);

    /* A commit signed by a different key does NOT verify against key1. */
    valid = 1;
    WF_CHECK(wf_verify_record_commit(didkey1, car2, len2, &valid) != WF_OK);
    WF_CHECK(valid == 0);

    /* Sanity: car2 verifies against its own key. */
    WF_CHECK(wf_verify_record_commit(didkey2, car2, len2, &valid) == WF_OK);
    WF_CHECK(valid == 1);

    free(didkey1);
    free(didkey2);
    free(car1);
    free(car2);
}

/* ── wf_agent_verify_record argument validation ── */

static void test_agent_verify_record_null_args(void)
{
    int valid = 0;
    wf_agent *agent = wf_agent_new("https://bsky.social");

    WF_CHECK(wf_agent_verify_record(NULL, "did", "coll", "rkey", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_record(agent, NULL, "coll", "rkey", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_record(agent, "", "coll", "rkey", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_record(agent, "did", NULL, "rkey", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_record(agent, "did", "", "rkey", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_record(agent, "did", "coll", NULL, &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_record(agent, "did", "coll", "", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_verify_record(agent, "did", "coll", "rkey", NULL) == WF_ERR_INVALID_ARG);

    wf_agent_free(agent);
}

int main(void)
{
    test_verify_record_commit_null_args();
    test_verify_record_commit_good_and_tampered();
    test_agent_verify_record_null_args();
    WF_TEST_SUMMARY();
}
