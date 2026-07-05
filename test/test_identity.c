/**
 * test_identity.c — unit tests for identity module.
 *
 * Tests DID method sniffing, DID document handling, and argument
 * validation. Network-dependent resolution tests are not included here
 * (they're slow, flaky, and belong in integration tests if needed).
 */

#include "wolfram/identity.h"
#include "wolfram/xrpc.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* Helper to create a minimal client for testing argument validation. */
static wf_xrpc_client *test_client(void) {
    return wf_xrpc_client_new("https://example.com");
}

int main(void) {
    /* --- DID method sniffing --- */
    WF_CHECK(wf_did_method_of("did:plc:ofrbh253gwicbkc5nktqepol") == WF_DID_METHOD_PLC);
    WF_CHECK(wf_did_method_of("did:web:example.com") == WF_DID_METHOD_WEB);
    WF_CHECK(wf_did_method_of("not-a-did") == WF_DID_METHOD_UNKNOWN);
    WF_CHECK(wf_did_method_of(NULL) == WF_DID_METHOD_UNKNOWN);

    /* --- wf_did_resolve argument validation --- */
    wf_xrpc_client *client = test_client();
    wf_did_document doc = {0};

    WF_CHECK(wf_did_resolve(NULL, "did:plc:abc", &doc) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_did_resolve(client, NULL, &doc) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_did_resolve(client, "did:plc:abc", NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_did_resolve(client, "did:unknown:abc", &doc) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_did_resolve(client, "did:web:", &doc) == WF_ERR_INVALID_ARG);

    /* --- wf_did_document_free is safe with NULL / zeroed struct --- */
    wf_did_document_free(NULL);
    wf_did_document_free(&doc);

    /* --- wf_handle_resolve argument validation --- */
    char *out_did = NULL;
    WF_CHECK(wf_handle_resolve(NULL, "example.com", &out_did) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_handle_resolve(client, NULL, &out_did) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_handle_resolve(client, "example.com", NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_handle_resolve(client, "", &out_did) == WF_ERR_INVALID_ARG);

    /* TXT chunks are joined per record; unrelated records are ignored. */
    const wf_dns_txt_chunk chunked[] = {
        {(const unsigned char *)"noise", 5, 1},
        {(const unsigned char *)"did=did:plc:", 12, 1},
        {(const unsigned char *)"abc123", 6, 0},
    };
    WF_CHECK(wf_handle_parse_dns_txt(chunked, 3, &out_did) == WF_OK);
    WF_CHECK(out_did != NULL && strcmp(out_did, "did:plc:abc123") == 0);
    free(out_did);
    out_did = NULL;

    const wf_dns_txt_chunk multiple[] = {
        {(const unsigned char *)"did=did:plc:first", 17, 1},
        {(const unsigned char *)"did=did:web:second", 18, 1},
    };
    WF_CHECK(wf_handle_parse_dns_txt(multiple, 2, &out_did) == WF_ERR_PARSE);
    WF_CHECK(out_did == NULL);

    const wf_dns_txt_chunk missing_prefix[] = {
        {(const unsigned char *)"did:plc:abc", 11, 1},
    };
    WF_CHECK(wf_handle_parse_dns_txt(missing_prefix, 1, &out_did) == WF_ERR_PARSE);

    const wf_dns_txt_chunk invalid_did[] = {
        {(const unsigned char *)"did=not-a-did", 13, 1},
    };
    WF_CHECK(wf_handle_parse_dns_txt(invalid_did, 1, &out_did) == WF_ERR_PARSE);

    const wf_dns_txt_chunk continuation_first[] = {
        {(const unsigned char *)"did=did:plc:abc", 15, 0},
    };
    WF_CHECK(wf_handle_parse_dns_txt(continuation_first, 1, &out_did) == WF_ERR_PARSE);
    WF_CHECK(wf_handle_parse_dns_txt(NULL, 0, &out_did) == WF_ERR_INVALID_ARG);

    /* --- DID document struct lifecycle --- */
    wf_did_document doc2 = {0};
    doc2.did = strdup("did:plc:test");
    doc2.pds_endpoint = strdup("https://pds.example.com");
    doc2.signing_key = strdup("z6Mkxyz...");
    doc2.method = WF_DID_METHOD_PLC;
    WF_CHECK(doc2.did != NULL);
    WF_CHECK(doc2.pds_endpoint != NULL);
    WF_CHECK(doc2.signing_key != NULL);
    wf_did_document_free(&doc2);
    WF_CHECK(doc2.did == NULL);
    WF_CHECK(doc2.pds_endpoint == NULL);
    WF_CHECK(doc2.signing_key == NULL);
    WF_CHECK(doc2.method == WF_DID_METHOD_UNKNOWN);

    wf_xrpc_client_free(client);
    WF_TEST_SUMMARY();
}
