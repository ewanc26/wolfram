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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

/* DID document served by the offline test handler below. */
static const char *g_did_doc = NULL;

/* Test seam handler (see wf_xrpc_set_handler): returns the canned DID
 * document for any GET, so wf_did_resolve / wf_did_resolve_service can be
 * exercised without network I/O. */
static wf_status did_doc_handler(void *userdata, const char *method,
                                 const char *url, const char *content_type,
                                 const char *body, size_t body_len,
                                 const wf_http_header *headers,
                                 size_t header_count, wf_response *out) {
    (void)userdata;
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;

    size_t len = strlen(g_did_doc);
    out->body = malloc(len + 1);
    if (!out->body) {
        return WF_ERR_ALLOC;
    }
    memcpy(out->body, g_did_doc, len + 1);
    out->body_len = len;
    out->status = 200;
    return WF_OK;
}

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

    size_t len = (size_t)size;
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

/* Helper to create a minimal client for testing argument validation. */
static wf_xrpc_client *test_client(void) {
    return wf_xrpc_client_new("https://example.com");
}

static char *test_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *b = malloc(n);
    if (b) memcpy(b, s, n);
    return b;
}

/* Records the URL the handler was called with (for asserting did:web URL
 * construction) and returns the canned DID document. */
static char *g_last_url = NULL;

static wf_status did_doc_url_handler(void *userdata, const char *method,
                                     const char *url, const char *content_type,
                                     const char *body, size_t body_len,
                                     const wf_http_header *headers,
                                     size_t header_count, wf_response *out) {
    (void)userdata;
    (void)method;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;

    free(g_last_url);
    g_last_url = test_strdup(url);

    size_t len = strlen(g_did_doc);
    out->body = malloc(len + 1);
    if (!out->body) {
        return WF_ERR_ALLOC;
    }
    memcpy(out->body, g_did_doc, len + 1);
    out->body_len = len;
    out->status = 200;
    return WF_OK;
}

/* Returns a configurable well-known atproto-did body (no trailing newline
 * required) so the well-known handle fallback can be exercised offline. */
static const char *g_well_known_body = NULL;
static size_t g_well_known_len = 0;

static wf_status well_known_handler(void *userdata, const char *method,
                                    const char *url, const char *content_type,
                                    const char *body, size_t body_len,
                                    const wf_http_header *headers,
                                    size_t header_count, wf_response *out) {
    (void)userdata;
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;

    size_t len = g_well_known_len;
    out->body = malloc(len + 1);
    if (!out->body) {
        return WF_ERR_ALLOC;
    }
    memcpy(out->body, g_well_known_body, len);
    out->body[len] = '\0';
    out->body_len = len;
    out->status = 200;
    return WF_OK;
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

    /* --- wf_did_resolve_service (offline, via test handler) --- */
    char *doc_json = NULL;
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/did_doc_chat.json",
                 WF_TEST_FIXTURE_DIR);
        doc_json = read_entire_file(path, NULL);
    }
    WF_CHECK(doc_json != NULL);

    g_did_doc = doc_json;
    wf_xrpc_set_handler(client, did_doc_handler, NULL);

    /* Argument validation. */
    char *ep = NULL;
    WF_CHECK(wf_did_resolve_service(NULL, "did:plc:chatdid",
                                    "BskyChatService", &ep) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_did_resolve_service(client, NULL,
                                    "BskyChatService", &ep) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_did_resolve_service(client, "did:plc:chatdid",
                                    NULL, &ep) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_did_resolve_service(client, "did:plc:chatdid",
                                    "BskyChatService", NULL) == WF_ERR_INVALID_ARG);

    /* Resolves the BskyChatService endpoint. */
    WF_CHECK(wf_did_resolve_service(client, "did:plc:chatdid",
                                    "BskyChatService", &ep) == WF_OK);
    WF_CHECK(ep != NULL && strcmp(ep, "https://chat.example.com") == 0);
    free(ep);

    /* Resolves the AtprotoChatProxy endpoint. */
    ep = NULL;
    WF_CHECK(wf_did_resolve_service(client, "did:plc:chatdid",
                                    "AtprotoChatProxy", &ep) == WF_OK);
    WF_CHECK(ep != NULL && strcmp(ep, "https://proxy.example.com") == 0);
    free(ep);

    /* Non-existent service type yields WF_ERR_NOT_FOUND with NULL out. */
    ep = NULL;
    WF_CHECK(wf_did_resolve_service(client, "did:plc:chatdid",
                                    "DoesNotExist", &ep) == WF_ERR_NOT_FOUND);
    WF_CHECK(ep == NULL);

    /* Regression: the refactored wf_did_resolve still parses the document. */
    wf_did_document doc3 = {0};
    WF_CHECK(wf_did_resolve(client, "did:plc:chatdid", &doc3) == WF_OK);
    WF_CHECK(doc3.did != NULL && strcmp(doc3.did, "did:plc:chatdid") == 0);
    wf_did_document_free(&doc3);

    wf_xrpc_set_handler(client, NULL, NULL);
    free(doc_json);

    /* --- did:web URL construction (FIX 1) --- */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/did_doc_chat.json",
                 WF_TEST_FIXTURE_DIR);
        char *web_doc = read_entire_file(path, NULL);
        WF_CHECK(web_doc != NULL);
        g_did_doc = web_doc;
        wf_xrpc_set_handler(client, did_doc_url_handler, NULL);

        wf_did_document doc4 = {0};
        WF_CHECK(wf_did_resolve(client, "did:web:example.com", &doc4) == WF_OK);
        WF_CHECK(g_last_url != NULL &&
                  strcmp(g_last_url, "https://example.com/.well-known/did.json") == 0);
        wf_did_document_free(&doc4);

        WF_CHECK(wf_did_resolve(client, "did:web:example.com:user", &doc4) == WF_OK);
        WF_CHECK(g_last_url != NULL &&
                  strcmp(g_last_url,
                         "https://example.com/user/.well-known/did.json") == 0);
        wf_did_document_free(&doc4);

        WF_CHECK(wf_did_resolve(client, "did:web:localhost:foo", &doc4) == WF_OK);
        WF_CHECK(g_last_url != NULL &&
                  strcmp(g_last_url,
                         "http://localhost/foo/.well-known/did.json") == 0);
        wf_did_document_free(&doc4);

        /* did:web with encoded port uses http for localhost. */
        WF_CHECK(wf_did_resolve(client, "did:web:localhost%3A8080", &doc4) == WF_OK);
        WF_CHECK(g_last_url != NULL &&
                  strcmp(g_last_url,
                         "http://localhost:8080/.well-known/did.json") == 0);
        wf_did_document_free(&doc4);

        /* Empty host (leading ':') is rejected before any fetch. */
        WF_CHECK(wf_did_resolve(client, "did:web:", &doc4) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_did_resolve(client, "did:web::example.com", &doc4) == WF_ERR_INVALID_ARG);

    wf_xrpc_set_handler(client, NULL, NULL);
    free(web_doc);
    }

    /* --- well-known atproto-did fallback accepts body without trailing
     *     newline (FIX 2) --- */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/did_doc_chat.json",
                 WF_TEST_FIXTURE_DIR);
        char *ignored = read_entire_file(path, NULL);
        (void)ignored;
        free(ignored);
        (void)path;

        g_well_known_body = "did:plc:abc";
        g_well_known_len = strlen(g_well_known_body);
        wf_xrpc_set_handler(client, well_known_handler, NULL);

        char *hk_did = NULL;
        WF_CHECK(wf_handle_resolve(client, "resolve.invalid", &hk_did) == WF_OK);
        WF_CHECK(hk_did != NULL && strcmp(hk_did, "did:plc:abc") == 0);
        free(hk_did);

        wf_xrpc_set_handler(client, NULL, NULL);
    }

    wf_xrpc_client_free(client);
    WF_TEST_SUMMARY();
}
