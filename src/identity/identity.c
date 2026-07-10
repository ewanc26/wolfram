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
#include "wolfram/atproto_lex.h"
#include "wolfram/plc.h"
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

#ifdef WOLFRAM_BUILD_IDN
#include <idn2.h>
#endif

static char *wf_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

static char *wf_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t copy = 0;
    while (copy < n && s[copy] != '\0') copy++;
    char *dup = malloc(copy + 1);
    if (dup) {
        memcpy(dup, s, copy);
        dup[copy] = '\0';
    }
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

/* Build the `/.well-known/did.json` URL for a did:web per the W3C/atproto
 * spec (cf. packages/did/src/methods/web.ts). The MSID (text after
 * `did:web:`) is the host with optional path/port segments:
 *   - the first `:` separates the host from the path; an MSID that starts
 *     with `:` (empty host) is rejected.
 *   - `%3A` in the host is decoded to `:` (port encoding).
 *   - remaining segments become a URL path with `:` replaced by `/`
 *     (e.g. `did:web:example.com:user` -> `https://example.com/user/...`).
 *   - `http://` is used (instead of `https://`) when the host is exactly
 *     `localhost` or `localhost:<port>`.
 * Allocates `*out_url` on success. */
static wf_status did_web_build_url(const char *did, char **out_url) {
    const char *msid = did + strlen("did:web:");
    if (msid[0] == '\0' || msid[0] == ':') {
        return WF_ERR_INVALID_ARG;
    }

    /* Copy the MSID; the host is everything up to the first ':'. */
    char *host = wf_strdup(msid);
    if (!host) return WF_ERR_ALLOC;

    char *path_sep = strchr(host, ':');
    if (path_sep) {
        *path_sep = '\0';
    }

    /* Decode %3A -> : in the host (percent-encoded port). */
    {
        char *out = host;
        const char *p = host;
        while (*p) {
            if (p[0] == '%' && p[1] == '3' && (p[2] == 'A' || p[2] == 'a')) {
                *out++ = ':';
                p += 3;
            } else {
                *out++ = *p++;
            }
        }
        *out = '\0';
    }

    /* Remaining MSID segments (after the first ':') form a URL path with
     * ':' replaced by '/'. */
    char *path = NULL;
    if (path_sep) {
        const char *p = strchr(msid, ':');
        size_t path_len = strlen(p);
        path = malloc(path_len + 1);
        if (!path) {
            free(host);
            return WF_ERR_ALLOC;
        }
        char *o = path;
        for (const char *q = p; *q; q++) {
            *o++ = (*q == ':' ? '/' : *q);
        }
        *o = '\0';
    }

    const char *proto = "https";
    if (strncmp(host, "localhost", 9) == 0 &&
        (host[9] == '\0' || host[9] == ':')) {
        proto = "http";
    }

    size_t url_len = strlen(proto) + strlen("://") + strlen(host) +
                     strlen(path ? path : "") +
                     strlen("/.well-known/did.json") + 1;
    char *url = malloc(url_len);
    if (!url) {
        free(path);
        free(host);
        return WF_ERR_ALLOC;
    }
    snprintf(url, url_len, "%s://%s%s/.well-known/did.json", proto, host,
             path ? path : "");

    free(path);
    free(host);
    *out_url = url;
    return WF_OK;
}

static wf_status did_fetch_document(wf_xrpc_client *client, const char *did,
                                     cJSON **out_root) {
    wf_did_method method = wf_did_method_of(did);
    char *url = NULL;
    if (method == WF_DID_METHOD_PLC) {
        size_t url_len = strlen("https://plc.directory/") + strlen(did) + 1;
        url = malloc(url_len);
        if (!url) return WF_ERR_ALLOC;
        snprintf(url, url_len, "https://plc.directory/%s", did);
    } else if (method == WF_DID_METHOD_WEB) {
        wf_status st = did_web_build_url(did, &url);
        if (st != WF_OK) {
            return st;
        }
    } else {
        return WF_ERR_INVALID_ARG;
    }

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

    *out_root = root;
    return WF_OK;
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

    cJSON *root = NULL;
    wf_status status = did_fetch_document(client, did, &root);
    if (status != WF_OK) {
        return status;
    }

    status = wf_did_doc_parse_json(out, root);
    cJSON_Delete(root);
    return status;
}

wf_status wf_did_resolve_service(wf_xrpc_client *client, const char *did,
                                 const char *service_type, char **out_endpoint) {
    if (!client || !did || !service_type || !out_endpoint) {
        return WF_ERR_INVALID_ARG;
    }
    *out_endpoint = NULL;

    cJSON *root = NULL;
    wf_status status = did_fetch_document(client, did, &root);
    if (status != WF_OK) {
        return status;
    }

    wf_status result = WF_ERR_NOT_FOUND;
    cJSON *service = cJSON_GetObjectItemCaseSensitive(root, "service");
    if (cJSON_IsArray(service)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, service) {
            cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
            cJSON *endpoint =
                cJSON_GetObjectItemCaseSensitive(item, "serviceEndpoint");
            if (cJSON_IsString(type) && cJSON_IsString(endpoint) &&
                endpoint->valuestring &&
                strcmp(type->valuestring, service_type) == 0) {
                *out_endpoint = wf_strdup(endpoint->valuestring);
                if (!*out_endpoint) {
                    result = WF_ERR_ALLOC;
                } else {
                    result = WF_OK;
                }
                break;
            }
        }
    }

    cJSON_Delete(root);
    return result;
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

    if (res.body_len == 0) {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    /* Take the first line of the body (no trailing-newline required) and
     * trim surrounding whitespace, matching atproto's
     * `(await res.text()).split('\n')[0].trim()`. */
    size_t line_len = res.body_len;
    char *nl = memchr(res.body, '\n', res.body_len);
    if (nl) line_len = (size_t)(nl - res.body);

    char *did = wf_strndup(res.body, line_len);
    wf_response_free(&res);
    if (!did) return WF_ERR_ALLOC;

    /* Trim leading and trailing ASCII whitespace (incl. trailing '\r'). */
    char *start = did;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' ||
                      *start == '\n' || *start == '\f' || *start == '\v')) {
        start++;
    }
    size_t trim_len = strlen(start);
    while (trim_len > 0) {
        char c = start[trim_len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
            c == '\v') {
            trim_len--;
        } else {
            break;
        }
    }
    if (start != did) {
        memmove(did, start, trim_len + 1);
    }
    did[trim_len] = '\0';

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

    /* The handle actually used for the wire lookup. By default this is the
     * caller's handle unchanged. When libidn2 is compiled in, an
     * internationalized (Unicode) handle is first converted to its IDNA2008
     * ASCII Compatible Encoding (ACE, `xn--…` punycode) so the DNS TXT and
     * well-known HTTPS lookups operate on valid ASCII — the DNS layer cannot
     * accept raw Unicode. */
    const char *lookup_handle = handle;

#ifdef WOLFRAM_BUILD_IDN
    char *ascii_handle = NULL;
    int idn_rc = idn2_to_ascii_8z(handle, &ascii_handle, IDN2_NONTRANSITIONAL);
    if (idn_rc == IDN2_OK && ascii_handle) {
        /* Recover the canonical Unicode form for display: a caller that
         * supplied a punycode handle gets the decoded Unicode back, and a
         * caller that supplied Unicode gets the NFC-normalized form. The
         * original handle remains in `handle` for the caller; `ascii_handle`
         * (owned by libidn2, freed below) is used solely for the wire. The
         * decode result is owned by libidn2 and must be freed with idn2_free.
         */
        char *display_handle = NULL;
        if (idn2_to_unicode_8z8z(ascii_handle, &display_handle, IDN2_NONTRANSITIONAL) == IDN2_OK) {
            idn2_free(display_handle);
        }
        lookup_handle = ascii_handle;
    }
    /* If IDNA conversion fails (e.g. a disallowed label), fall through to the
     * unchanged handle so behavior matches the non-IDN build. */
#endif

    wf_status status = wf_handle_resolve_dns_txt(client, lookup_handle, out_did);

    if (status == WF_OK) {
#ifdef WOLFRAM_BUILD_IDN
        if (ascii_handle) idn2_free(ascii_handle);
#endif
        return WF_OK;
    }

    status = wf_handle_resolve_well_known(client, lookup_handle, out_did);

#ifdef WOLFRAM_BUILD_IDN
    if (ascii_handle) idn2_free(ascii_handle);
#endif

    return status;
}

/* ------------------------------------------------------------------ */
/* com.atproto.identity account / identity-management operations.      */
/* ------------------------------------------------------------------ */

static wf_status wf_identity_dup_string_array(const char *const *src,
                                              size_t count, char ***out,
                                              size_t *out_count) {
    char **items = NULL;

    if (!out || !out_count) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    *out_count = 0;
    if (count == 0) {
        return WF_OK;
    }

    items = calloc(count, sizeof(*items));
    if (!items) {
        return WF_ERR_ALLOC;
    }
    for (size_t i = 0; i < count; i++) {
        items[i] = wf_strdup(src[i]);
        if (!items[i]) {
            for (size_t j = 0; j < i; j++) {
                free(items[j]);
            }
            free(items);
            return WF_ERR_ALLOC;
        }
    }
    *out = items;
    *out_count = count;
    return WF_OK;
}

static char *wf_identity_json_string_dup(const wf_lex_json *json) {
    if (!json || !json->data || json->length == 0) {
        return NULL;
    }
    char *copy = malloc(json->length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, json->data, json->length);
    copy[json->length] = '\0';
    return copy;
}

void wf_identity_recommended_did_credentials_free(
    wf_identity_recommended_did_credentials *creds) {
    if (!creds) {
        return;
    }
    if (creds->rotation_keys) {
        for (size_t i = 0; i < creds->rotation_keys_count; i++) {
            free(creds->rotation_keys[i]);
        }
        free(creds->rotation_keys);
    }
    if (creds->also_known_as) {
        for (size_t i = 0; i < creds->also_known_as_count; i++) {
            free(creds->also_known_as[i]);
        }
        free(creds->also_known_as);
    }
    free(creds->verification_methods);
    free(creds->services);
    memset(creds, 0, sizeof(*creds));
}

wf_status wf_identity_get_recommended_did_credentials(
    wf_xrpc_client *client, wf_identity_recommended_did_credentials *out) {
    wf_response res = {0};
    wf_lex_com_atproto_identity_get_recommended_did_credentials_main_output *lex =
        NULL;
    wf_status status;

    if (!client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    status = wf_lex_com_atproto_identity_get_recommended_did_credentials_main_call(
        client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_lex_com_atproto_identity_get_recommended_did_credentials_main_output_decode_json(
        res.body, res.body_len, &lex);
    wf_response_free(&res);
    if (status != WF_OK || !lex) {
        return status != WF_OK ? status : WF_ERR_PARSE;
    }

    if (lex->has_rotation_keys) {
        status = wf_identity_dup_string_array(lex->rotation_keys.items,
                                              lex->rotation_keys.count,
                                              &out->rotation_keys,
                                              &out->rotation_keys_count);
    }
    if (status == WF_OK && lex->has_also_known_as) {
        status = wf_identity_dup_string_array(lex->also_known_as.items,
                                              lex->also_known_as.count,
                                              &out->also_known_as,
                                              &out->also_known_as_count);
    }
    if (status == WF_OK && lex->has_verification_methods) {
        out->verification_methods =
            wf_identity_json_string_dup(&lex->verification_methods);
        if (lex->verification_methods.length && !out->verification_methods) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK && lex->has_services) {
        out->services = wf_identity_json_string_dup(&lex->services);
        if (lex->services.length && !out->services) {
            status = WF_ERR_ALLOC;
        }
    }

    wf_lex_com_atproto_identity_get_recommended_did_credentials_main_output_free(
        lex);
    if (status != WF_OK) {
        wf_identity_recommended_did_credentials_free(out);
    }
    return status;
}

wf_status wf_identity_request_plc_operation_signature(wf_xrpc_client *client,
                                                      const char *did) {
    return wf_plc_request_signature(client, did);
}

void wf_identity_sign_plc_operation_result_free(
    wf_identity_sign_plc_operation_result *result) {
    if (!result) {
        return;
    }
    free(result->operation);
    memset(result, 0, sizeof(*result));
}

wf_status wf_identity_sign_plc_operation(
    wf_xrpc_client *client, const wf_identity_sign_plc_operation_input *input,
    wf_identity_sign_plc_operation_result *out) {
    wf_response res = {0};
    wf_lex_com_atproto_identity_sign_plc_operation_main_input lex_in = {0};
    wf_lex_com_atproto_identity_sign_plc_operation_main_output *lex_out = NULL;
    wf_status status;

    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    if (input->token && input->token[0] != '\0') {
        lex_in.has_token = 1;
        lex_in.token = input->token;
    }
    if (input->rotation_keys_count > 0 && input->rotation_keys) {
        lex_in.has_rotation_keys = 1;
        lex_in.rotation_keys.items = input->rotation_keys;
        lex_in.rotation_keys.count = input->rotation_keys_count;
    }
    if (input->also_known_as_count > 0 && input->also_known_as) {
        lex_in.has_also_known_as = 1;
        lex_in.also_known_as.items = input->also_known_as;
        lex_in.also_known_as.count = input->also_known_as_count;
    }
    if (input->verification_methods_json && input->verification_methods_json[0]) {
        lex_in.has_verification_methods = 1;
        lex_in.verification_methods.data = input->verification_methods_json;
        lex_in.verification_methods.length =
            strlen(input->verification_methods_json);
    }
    if (input->services_json && input->services_json[0]) {
        lex_in.has_services = 1;
        lex_in.services.data = input->services_json;
        lex_in.services.length = strlen(input->services_json);
    }

    status = wf_lex_com_atproto_identity_sign_plc_operation_main_call(client,
                                                                      &lex_in, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_lex_com_atproto_identity_sign_plc_operation_main_output_decode_json(
        res.body, res.body_len, &lex_out);
    wf_response_free(&res);
    if (status != WF_OK || !lex_out) {
        return status != WF_OK ? status : WF_ERR_PARSE;
    }

    out->operation = wf_identity_json_string_dup(&lex_out->operation);
    wf_lex_com_atproto_identity_sign_plc_operation_main_output_free(lex_out);
    if (!out->operation && lex_out->operation.length) {
        wf_identity_sign_plc_operation_result_free(out);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

wf_status wf_identity_submit_plc_operation(wf_xrpc_client *client,
                                           const char *signed_op_json) {
    return wf_plc_submit_operation(client, signed_op_json);
}

wf_status wf_identity_update_handle(wf_xrpc_client *client, const char *handle) {
    return wf_plc_update_handle(client, handle);
}

wf_status wf_identity_check_handle(wf_xrpc_client *client,
                                   const wf_identity_check_handle_input *input,
                                   wf_identity_check_handle_result *out) {
    wf_response res = {0};
    wf_xrpc_param params[2];
    size_t param_count = 0;
    cJSON *root = NULL;
    const cJSON *valid;
    wf_status status;

    if (!client || !input || !out || !input->handle || input->handle[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->valid = -1;

    params[param_count].name = "handle";
    params[param_count].value = input->handle;
    param_count++;
    if (input->did && input->did[0] != '\0') {
        params[param_count].name = "did";
        params[param_count].value = input->did;
        param_count++;
    }

    status = wf_xrpc_query_params(client, "com.atproto.identity.checkHandle",
                                  params, param_count, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    valid = cJSON_GetObjectItemCaseSensitive(root, "valid");
    if (valid && cJSON_IsBool(valid)) {
        out->valid = cJSON_IsTrue(valid) ? 1 : 0;
    }
    cJSON_Delete(root);
    return WF_OK;
}

wf_status wf_identity_verify_handle(wf_xrpc_client *client, const char *handle,
                                    int *out_valid) {
    char *did = NULL;
    cJSON *root = NULL;
    wf_status status;

    if (!client || !handle || handle[0] == '\0' || !out_valid) {
        return WF_ERR_INVALID_ARG;
    }
    *out_valid = 0;

    status = wf_handle_resolve(client, handle, &did);
    if (status != WF_OK) {
        return status;
    }

    status = did_fetch_document(client, did, &root);
    if (status == WF_OK && root) {
        int match = 1;
        cJSON *aka = cJSON_GetObjectItemCaseSensitive(root, "alsoKnownAs");
        if (aka && cJSON_IsArray(aka)) {
            match = 0;
            cJSON *item = NULL;
            const char *prefix = "at://";
            size_t prefix_len = strlen(prefix);
            cJSON_ArrayForEach(item, aka) {
                if (cJSON_IsString(item) && item->valuestring &&
                    strncmp(item->valuestring, prefix, prefix_len) == 0 &&
                    strcmp(item->valuestring + prefix_len, handle) == 0) {
                    match = 1;
                    break;
                }
            }
        }
        *out_valid = match;
        cJSON_Delete(root);
    }
    free(did);
    return status;
}
