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
 * an empty tree, mirroring test_verify_record.c's construction. */
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

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c)
        memcpy(c, s, n);
    return c;
}

/* ── fake resolvers ── */

typedef struct fake_resolver_ctx {
    const char *did;     /* DID this resolver knows about */
    const char *key;     /* did:key to return for `did` (may be NULL) */
    int unavailable;     /* when 1, report WF_ERR_NOT_FOUND */
} fake_resolver_ctx;

static wf_status fake_resolver(const char *did, const char *key_id,
                               void *userdata, char **out_key)
{
    (void)key_id;
    fake_resolver_ctx *ctx = (fake_resolver_ctx *)userdata;
    if (out_key)
        *out_key = NULL;
    if (ctx->unavailable)
        return WF_ERR_NOT_FOUND;
    if (strcmp(did, ctx->did) != 0)
        return WF_ERR_NOT_FOUND;
    if (!ctx->key)
        return WF_ERR_NOT_FOUND;
    *out_key = dup_str(ctx->key);
    return *out_key ? WF_OK : WF_ERR_ALLOC;
}

/* ── resolver: happy + wrong-key + unavailable ── */

static void test_resolved_happy_wrong_unavailable(void)
{
    wf_signing_key key1, key2;
    memset(&key1, 0, sizeof(key1));
    memset(&key2, 0, sizeof(key2));
    if (wf_signing_key_generate(WF_KEY_TYPE_P256, &key1) != WF_OK)
        return; /* P-256 crypto unavailable — skip gracefully */
    if (wf_signing_key_generate(WF_KEY_TYPE_P256, &key2) != WF_OK)
        return;

    char *didkey1 = NULL;
    char *didkey2 = NULL;
    if (wf_signing_key_public_didkey(&key1, &didkey1) != WF_OK) return;
    if (wf_signing_key_public_didkey(&key2, &didkey2) != WF_OK) {
        free(didkey1);
        return;
    }

    unsigned char *car = NULL;
    size_t len = 0;
    if (!build_signed_car(&key1, didkey1, &car, &len)) {
        free(didkey1);
        free(didkey2);
        return;
    }

    int valid = 0;

    /* Resolver returns the correct key for the DID -> verifies OK. */
    fake_resolver_ctx ctx = { didkey1, didkey1, 0 };
    wf_verify_set_key_resolver(fake_resolver, &ctx);
    WF_CHECK(wf_verify_record_commit_resolved(didkey1, car, len, &valid) == WF_OK);
    WF_CHECK(valid == 1);

    /* Resolver returns the WRONG key -> signature check fails honestly. */
    fake_resolver_ctx wrong = { didkey1, didkey2, 0 };
    wf_verify_set_key_resolver(fake_resolver, &wrong);
    valid = 1;
    wf_status s = wf_verify_record_commit_resolved(didkey1, car, len, &valid);
    WF_CHECK(s != WF_OK || valid == 0);
    WF_CHECK(valid == 0);

    /* Resolver reports the key unavailable -> honest error, no crash, no pass. */
    fake_resolver_ctx gone = { didkey1, NULL, 1 };
    wf_verify_set_key_resolver(fake_resolver, &gone);
    valid = 1;
    WF_CHECK(wf_verify_record_commit_resolved(didkey1, car, len, &valid) == WF_ERR_NOT_FOUND);
    WF_CHECK(valid == 0);

    wf_verify_set_key_resolver(NULL, NULL);
    free(didkey1);
    free(didkey2);
    free(car);
}

/* ── tampered commit fails even with the correct resolver ── */

static void test_resolved_tampered(void)
{
    wf_signing_key key;
    memset(&key, 0, sizeof(key));
    if (wf_signing_key_generate(WF_KEY_TYPE_P256, &key) != WF_OK)
        return;

    char *didkey = NULL;
    if (wf_signing_key_public_didkey(&key, &didkey) != WF_OK)
        return;

    unsigned char *car = NULL;
    size_t len = 0;
    if (!build_signed_car(&key, didkey, &car, &len)) {
        free(didkey);
        return;
    }

    /* Tamper with a byte in the CAR (skip the CBOR header) to break the
     * signed payload; the signature must no longer verify. */
    unsigned char *bad = malloc(len);
    if (!bad) {
        free(didkey);
        free(car);
        return;
    }
    memcpy(bad, car, len);
    size_t off = len > 8 ? 8 : 0;
    bad[off] ^= 0xFF;

    int valid = 0;
    fake_resolver_ctx ctx = { didkey, didkey, 0 };
    wf_verify_set_key_resolver(fake_resolver, &ctx);
    wf_status s = wf_verify_record_commit_resolved(didkey, bad, len, &valid);
    WF_CHECK(s != WF_OK || valid == 0);
    WF_CHECK(valid == 0);

    wf_verify_set_key_resolver(NULL, NULL);
    free(didkey);
    free(car);
    free(bad);
}

/* ── no resolver installed: honest WF_ERR_INVALID_ARG, never a false pass ── */

static void test_no_resolver_honest_error(void)
{
    wf_signing_key key;
    memset(&key, 0, sizeof(key));
    if (wf_signing_key_generate(WF_KEY_TYPE_P256, &key) != WF_OK)
        return;

    char *didkey = NULL;
    if (wf_signing_key_public_didkey(&key, &didkey) != WF_OK)
        return;

    unsigned char *car = NULL;
    size_t len = 0;
    if (!build_signed_car(&key, didkey, &car, &len)) {
        free(didkey);
        return;
    }

    wf_verify_set_key_resolver(NULL, NULL);

    int valid = 1;
    WF_CHECK(wf_verify_record_commit_resolved(didkey, car, len, &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(valid == 0);

    /* The agent wrapper must also refuse honestly when no resolver is set. */
    wf_agent *agent = wf_agent_new("https://bsky.social");
    valid = 1;
    WF_CHECK(wf_agent_verify_record(agent, didkey, "app.bsky.feed.post",
                                    "abcde", &valid) == WF_ERR_INVALID_ARG);
    WF_CHECK(valid == 0);
    wf_agent_free(agent);

    free(didkey);
    free(car);
}

/* ── agent wrapper consults the resolver (no network reached on failure) ── */

static void test_agent_wrapper_consults_resolver(void)
{
    wf_signing_key key;
    memset(&key, 0, sizeof(key));
    if (wf_signing_key_generate(WF_KEY_TYPE_P256, &key) != WF_OK)
        return;

    char *didkey = NULL;
    if (wf_signing_key_public_didkey(&key, &didkey) != WF_OK)
        return;

    wf_agent *agent = wf_agent_new("https://bsky.social");

    /* Resolver unavailable: agent wrapper returns the resolver's error
     * before any network fetch — proves the resolver is wired in. */
    fake_resolver_ctx gone = { didkey, NULL, 1 };
    wf_verify_set_key_resolver(fake_resolver, &gone);
    int valid = 1;
    WF_CHECK(wf_agent_verify_record(agent, didkey, "app.bsky.feed.post",
                                    "abcde", &valid) == WF_ERR_NOT_FOUND);
    WF_CHECK(valid == 0);

    wf_verify_set_key_resolver(NULL, NULL);
    wf_agent_free(agent);
    free(didkey);
}

static void test_builtin_resolver_rejects_non_atproto_key_id(void)
{
    wf_xrpc_client *client = wf_xrpc_client_new("https://example.invalid");
    char *key = (char *)0x1;
    WF_CHECK(client != NULL);
    if (client) {
        WF_CHECK(wf_verify_resolve_via_did(
                     "did:plc:alice", "#unrelated", client, &key) ==
                 WF_ERR_NOT_FOUND);
        WF_CHECK(key == NULL);
        key = (char *)0x1;
        WF_CHECK(wf_verify_resolve_signing_key(
                     "did:plc:alice", "did:plc:alice#unrelated", client,
                     &key) == WF_ERR_NOT_FOUND);
        WF_CHECK(key == NULL);
        wf_xrpc_client_free(client);
    }
}

int main(void)
{
    test_resolved_happy_wrong_unavailable();
    test_resolved_tampered();
    test_no_resolver_honest_error();
    test_agent_wrapper_consults_resolver();
    test_builtin_resolver_rejects_non_atproto_key_id();
    WF_TEST_SUMMARY();
}
