/**
 * identity.c — DID and handle resolution.
 *
 * Implements:
 *   - did:plc resolution via https://plc.directory/<did>
 *   - did:web resolution via https://<host>/.well-known/did.json
 *   - handle resolution via DNS TXT _atproto.<handle> then
 *     https://<handle>/.well-known/atproto-did fallback
 *
 * Depends on cJSON for parsing DID documents (JSON).
 * DNS TXT lookups prefer c-ares, with the POSIX resolver as a fallback.
 */

#include "wolfram/identity.h"
#include "wolfram/xrpc.h"
#include "wolfram/version.h"

#include <cJSON.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CARES
#include <ares.h>
#endif

#ifdef HAVE_RESOLV
#include <resolv.h>
#include <arpa/nameser.h>
#endif

static char *wf_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

static void wf_did_doc_init(wf_did_document *doc) {
    doc->did = NULL;
    doc->pds_endpoint = NULL;
    doc->signing_key = NULL;
    doc->notif_endpoint = NULL;
    doc->method = WF_DID_METHOD_UNKNOWN;
}

static wf_status wf_did_doc_parse_json(wf_did_document *doc, cJSON *root) {
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsString(id) || !id->valuestring) {
        return WF_ERR_PARSE;
    }
    doc->did = wf_strdup(id->valuestring);
    if (!doc->did) return WF_ERR_ALLOC;

    cJSON *service = cJSON_GetObjectItemCaseSensitive(root, "service");
    if (cJSON_IsArray(service)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, service) {
            cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
            cJSON *endpoint = cJSON_GetObjectItemCaseSensitive(item, "serviceEndpoint");
            if (cJSON_IsString(type) && cJSON_IsString(endpoint) && endpoint->valuestring) {
                if (strcmp(type->valuestring, "AtprotoPersonalDataServer") == 0 && !doc->pds_endpoint) {
                    doc->pds_endpoint = wf_strdup(endpoint->valuestring);
                } else if (strcmp(type->valuestring, "BskyNotificationService") == 0 && !doc->notif_endpoint) {
                    doc->notif_endpoint = wf_strdup(endpoint->valuestring);
                }
                // Continue scanning to capture both if present.
            }
        }
    }

    cJSON *verification = cJSON_GetObjectItemCaseSensitive(root, "verificationMethod");
    if (cJSON_IsArray(verification)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, verification) {
            cJSON *controller = cJSON_GetObjectItemCaseSensitive(item, "controller");
            cJSON *public_key = cJSON_GetObjectItemCaseSensitive(item, "publicKeyMultibase");
            if (cJSON_IsString(controller) && controller->valuestring && doc->did &&
                strcmp(controller->valuestring, doc->did) == 0 &&
                cJSON_IsString(public_key) && public_key->valuestring) {
                doc->signing_key = wf_strdup(public_key->valuestring);
                break;
            }
        }
    }

    return WF_OK;
}

static wf_status wf_did_resolve_plc(wf_xrpc_client *client, const char *did, wf_did_document *out) {
    size_t url_len = strlen("https://plc.directory/") + strlen(did) + 1;
    char *url = malloc(url_len);
    if (!url) return WF_ERR_ALLOC;
    snprintf(url, url_len, "https://plc.directory/%s", did);

    wf_response res = {0};
    wf_status status = wf_http_get(client, url, &res);
    free(url);

    if (status != WF_OK) {
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        return WF_ERR_PARSE;
    }

    status = wf_did_doc_parse_json(out, root);
    cJSON_Delete(root);
    return status;
}

static wf_status wf_did_resolve_web(wf_xrpc_client *client, const char *did, wf_did_document *out) {
    const char *host = did + strlen("did:web:");
    if (!host || host[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    size_t url_len = strlen("https://") + strlen(host) + strlen("/.well-known/did.json") + 1;
    char *url = malloc(url_len);
    if (!url) return WF_ERR_ALLOC;
    snprintf(url, url_len, "https://%s/.well-known/did.json", host);

    wf_response res = {0};
    wf_status status = wf_http_get(client, url, &res);
    free(url);

    if (status != WF_OK) {
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        return WF_ERR_PARSE;
    }

    status = wf_did_doc_parse_json(out, root);
    cJSON_Delete(root);
    return status;
}

wf_did_method wf_did_method_of(const char *did) {
    if (!did) return WF_DID_METHOD_UNKNOWN;

    if (strncmp(did, "did:plc:", 8) == 0) {
        return WF_DID_METHOD_PLC;
    }
    if (strncmp(did, "did:web:", 8) == 0) {
        return WF_DID_METHOD_WEB;
    }
    return WF_DID_METHOD_UNKNOWN;
}

wf_status wf_did_resolve(wf_xrpc_client *client, const char *did, wf_did_document *out) {
    if (!client || !did || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_did_doc_init(out);
    out->method = wf_did_method_of(did);

    switch (out->method) {
        case WF_DID_METHOD_PLC:
            return wf_did_resolve_plc(client, did, out);
        case WF_DID_METHOD_WEB:
            return wf_did_resolve_web(client, did, out);
        default:
            return WF_ERR_INVALID_ARG;
    }
}

void wf_did_document_free(wf_did_document *doc) {
    if (!doc) return;
    free(doc->did);
    free(doc->pds_endpoint);
    free(doc->signing_key);
    free(doc->notif_endpoint);
    doc->did = NULL;
    doc->pds_endpoint = NULL;
    doc->signing_key = NULL;
    doc->notif_endpoint = NULL;
    doc->method = WF_DID_METHOD_UNKNOWN;
}

wf_status wf_handle_parse_dns_txt(const wf_dns_txt_chunk *chunks,
                                  size_t chunk_count,
                                  char **out_did) {
    if (!chunks || chunk_count == 0 || !out_did) return WF_ERR_INVALID_ARG;
    *out_did = NULL;

    size_t matches = 0;
    char *match = NULL;
    size_t i = 0;
    while (i < chunk_count) {
        if (!chunks[i].record_start) {
            free(match);
            return WF_ERR_PARSE;
        }
        size_t end = i + 1;
        size_t length = chunks[i].length;
        while (end < chunk_count && !chunks[end].record_start) {
            if (SIZE_MAX - length < chunks[end].length) {
                free(match);
                return WF_ERR_ALLOC;
            }
            length += chunks[end].length;
            end++;
        }

        if (length == SIZE_MAX) {
            free(match);
            return WF_ERR_ALLOC;
        }
        char *record = malloc(length + 1);
        if (!record) {
            free(match);
            return WF_ERR_ALLOC;
        }
        size_t offset = 0;
        for (size_t part = i; part < end; part++) {
            if (chunks[part].length > 0 && !chunks[part].data) {
                free(record);
                free(match);
                return WF_ERR_INVALID_ARG;
            }
            memcpy(record + offset, chunks[part].data, chunks[part].length);
            offset += chunks[part].length;
        }
        record[length] = '\0';

        if (!memchr(record, '\0', length) && length >= 4 &&
            memcmp(record, "did=", 4) == 0) {
            matches++;
            free(match);
            match = NULL;
            if (length >= 8 && memcmp(record + 4, "did:", 4) == 0) {
                match = wf_strdup(record + 4);
                if (!match) {
                    free(record);
                    return WF_ERR_ALLOC;
                }
            }
        }
        free(record);
        i = end;
    }

    if (matches != 1 || !match) {
        free(match);
        return WF_ERR_PARSE;
    }
    *out_did = match;
    return WF_OK;
}

#ifdef HAVE_CARES
typedef struct wf_cares_result {
    wf_status status;
    char *did;
} wf_cares_result;

static void wf_cares_txt_callback(void *arg, ares_status_t status, size_t timeouts,
                                  const ares_dns_record_t *dnsrec) {
    (void)timeouts;
    wf_cares_result *result = arg;
    if (status != ARES_SUCCESS) {
        result->status = (status == ARES_ENODATA || status == ARES_ENOTFOUND)
                             ? WF_ERR_NOT_FOUND
                             : WF_ERR_NETWORK;
        return;
    }

    size_t count = 0;
    size_t answers = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
    for (size_t i = 0; i < answers; i++) {
        const ares_dns_rr_t *rr = ares_dns_record_rr_get_const(
            dnsrec, ARES_SECTION_ANSWER, i);
        if (rr && ares_dns_rr_get_type(rr) == ARES_REC_TYPE_TXT)
            count += ares_dns_rr_get_abin_cnt(rr, ARES_RR_TXT_DATA);
    }
    if (count == 0) {
        result->status = WF_ERR_NOT_FOUND;
        return;
    }
    wf_dns_txt_chunk *chunks = calloc(count, sizeof(*chunks));
    if (!chunks) {
        result->status = WF_ERR_ALLOC;
        return;
    }
    size_t chunk_index = 0;
    for (size_t answer = 0; answer < answers; answer++) {
        const ares_dns_rr_t *rr = ares_dns_record_rr_get_const(
            dnsrec, ARES_SECTION_ANSWER, answer);
        if (!rr || ares_dns_rr_get_type(rr) != ARES_REC_TYPE_TXT) continue;
        size_t parts = ares_dns_rr_get_abin_cnt(rr, ARES_RR_TXT_DATA);
        for (size_t part = 0; part < parts; part++) {
            chunks[chunk_index].data = ares_dns_rr_get_abin(
                rr, ARES_RR_TXT_DATA, part, &chunks[chunk_index].length);
            chunks[chunk_index].record_start = part == 0;
            chunk_index++;
        }
    }
    result->status = wf_handle_parse_dns_txt(chunks, count, &result->did);
    free(chunks);
}

static wf_status wf_handle_resolve_cares(const char *qname, char **out_did) {
    static atomic_flag init_lock = ATOMIC_FLAG_INIT;
    static atomic_int initialized = 0;
    int init_state = atomic_load_explicit(&initialized, memory_order_acquire);
    if (init_state == 0) {
        while (atomic_flag_test_and_set_explicit(&init_lock, memory_order_acquire)) {}
        init_state = atomic_load_explicit(&initialized, memory_order_relaxed);
        if (init_state == 0) {
            init_state = ares_library_init(ARES_LIB_INIT_ALL) == ARES_SUCCESS ? 1 : -1;
            if (init_state == 1) atexit(ares_library_cleanup);
            atomic_store_explicit(&initialized, init_state, memory_order_release);
        }
        atomic_flag_clear_explicit(&init_lock, memory_order_release);
    }
    if (init_state < 0) return WF_ERR_NETWORK;

    ares_channel_t *channel = NULL;
    struct ares_options options = {0};
    options.evsys = ARES_EVSYS_DEFAULT;
    int status = ares_init_options(&channel, &options, ARES_OPT_EVENT_THREAD);
    if (status != ARES_SUCCESS) return WF_ERR_NETWORK;

    wf_cares_result result = {WF_ERR_NETWORK, NULL};
    status = ares_query_dnsrec(channel, qname, ARES_CLASS_IN, ARES_REC_TYPE_TXT,
                               wf_cares_txt_callback, &result, NULL);
    if (status != ARES_SUCCESS) {
        ares_destroy(channel);
        return WF_ERR_NETWORK;
    }
    status = ares_queue_wait_empty(channel, -1);
    ares_destroy(channel);
    if (status != ARES_SUCCESS) {
        free(result.did);
        return WF_ERR_NETWORK;
    }
    *out_did = result.did;
    return result.status;
}
#endif

static wf_status wf_handle_resolve_dns_txt(wf_xrpc_client *client, const char *handle, char **out_did) {
    (void)client;

    if (!handle || !out_did || handle[0] == '\0') return WF_ERR_INVALID_ARG;

    size_t qname_len = strlen("_atproto.") + strlen(handle) + 1;
    char *qname = malloc(qname_len);
    if (!qname) return WF_ERR_ALLOC;
    snprintf(qname, qname_len, "_atproto.%s", handle);

#ifdef HAVE_CARES
    wf_status status = wf_handle_resolve_cares(qname, out_did);
    free(qname);
    return status;

#elif defined(HAVE_RESOLV)
    unsigned char buf[1024];
    int len = res_query(qname, ns_c_in, ns_t_txt, buf, sizeof(buf));
    free(qname);

    if (len < 0) return WF_ERR_NETWORK;

    ns_msg msg;
    if (ns_initparse(buf, len, &msg) < 0) return WF_ERR_PARSE;

    int ancount = ns_msg_count(msg, ns_s_an);
    if (ancount < 1) return WF_ERR_NOT_FOUND;

    wf_dns_txt_chunk *chunks = calloc((size_t)ancount, sizeof(*chunks));
    if (!chunks) return WF_ERR_ALLOC;
    size_t chunk_count = 0;
    wf_status result = WF_OK;
    for (int answer = 0; answer < ancount && result == WF_OK; answer++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, answer, &rr) < 0) {
            result = WF_ERR_PARSE;
            break;
        }
        if (ns_rr_type(rr) != ns_t_txt) continue;
        const unsigned char *rdata = ns_rr_rdata(rr);
        size_t rdlen = ns_rr_rdlen(rr);
        size_t offset = 0;
        int first = 1;
        while (offset < rdlen) {
            size_t length = rdata[offset++];
            if (length > rdlen - offset) {
                result = WF_ERR_PARSE;
                break;
            }
            wf_dns_txt_chunk *grown = realloc(chunks, (chunk_count + 1) * sizeof(*chunks));
            if (!grown) {
                result = WF_ERR_ALLOC;
                break;
            }
            chunks = grown;
            chunks[chunk_count++] = (wf_dns_txt_chunk){rdata + offset, length, first};
            first = 0;
            offset += length;
        }
    }
    if (result == WF_OK) result = chunk_count ? wf_handle_parse_dns_txt(chunks, chunk_count, out_did)
                                               : WF_ERR_NOT_FOUND;
    free(chunks);
    return result;
#else
    free(qname);
    /* DNS library not available — skip to well-known fallback */
    return WF_ERR_NETWORK;
#endif
}

static wf_status wf_handle_resolve_well_known(wf_xrpc_client *client, const char *handle, char **out_did) {
    size_t url_len = strlen("https://") + strlen(handle) + strlen("/.well-known/atproto-did") + 1;
    char *url = malloc(url_len);
    if (!url) return WF_ERR_ALLOC;
    snprintf(url, url_len, "https://%s/.well-known/atproto-did", handle);

    wf_response res = {0};
    wf_status status = wf_http_get(client, url, &res);
    free(url);

    if (status != WF_OK) {
        return status;
    }

    if (res.body_len == 0 || res.body[res.body_len - 1] != '\n') {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    char *did = wf_strdup(res.body);
    wf_response_free(&res);
    if (!did) return WF_ERR_ALLOC;

    char *newline = strchr(did, '\n');
    if (newline) *newline = '\0';

    if (strncmp(did, "did:", 4) != 0) {
        free(did);
        return WF_ERR_PARSE;
    }

    *out_did = did;
    return WF_OK;
}

wf_status wf_handle_resolve(wf_xrpc_client *client, const char *handle, char **out_did) {
    if (!client || !handle || !out_did || handle[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    *out_did = NULL;

    wf_status status = wf_handle_resolve_dns_txt(client, handle, out_did);
    if (status == WF_OK) {
        return WF_OK;
    }

    return wf_handle_resolve_well_known(client, handle, out_did);
}
