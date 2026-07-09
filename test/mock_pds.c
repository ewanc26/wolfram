/**
 * mock_pds.c — embedded mock XRPC/PDS server backed by libmicrohttpd.
 *
 * See mock_pds.h for the public API and intended use. The daemon runs a
 * single internal polling thread, so the canned-response map is accessed
 * by only one thread at a time; no extra locking is required here.
 */

#include "mock_pds.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <microhttpd.h>

struct wf_mock_pds_entry {
    char *nsid;
    char *json;
};

/* Per-connection upload accumulator (only used for POST/PUT bodies). */
struct wf_mock_pds_upload {
    char *body;
    size_t len;
    size_t cap;
};

struct wf_mock_pds {
    struct MHD_Daemon *daemon;
    int port;
    struct wf_mock_pds_entry *entries;
    size_t count;
    size_t cap;
    /* Most-recent request (borrowed until the next request / free). */
    char *last_nsid;
    char *last_method;
    char *last_body;
};

static const char *const MOCK_NOT_FOUND_BODY =
    "{\"error\":\"NotFoundError\",\"message\":\"unknown NSID\"}";

static const char *mock_lookup(wf_mock_pds *pds, const char *nsid) {
    for (size_t i = 0; i < pds->count; i++) {
        if (strcmp(pds->entries[i].nsid, nsid) == 0) {
            return pds->entries[i].json;
        }
    }
    return NULL;
}

static enum MHD_Result
mock_handler(void *cls,
             struct MHD_Connection *connection,
             const char *url,
             const char *method,
             const char *version,
             const char *upload_data,
             size_t *upload_data_size,
             void **con_cls)
{
    (void)version;
    (void)connection;

    wf_mock_pds *pds = (wf_mock_pds *)cls;

    /* First call per connection: allocate per-connection upload state and tell
     * MHD to keep streaming the body (return YES with no response yet). */
    if (*con_cls == NULL) {
        struct wf_mock_pds_upload *st =
            (struct wf_mock_pds_upload *)calloc(1, sizeof(*st));
        if (st == NULL) {
            return MHD_NO;
        }
        *con_cls = st;
        return MHD_YES;
    }

    struct wf_mock_pds_upload *st = (struct wf_mock_pds_upload *)*con_cls;

    /* For POST/PUT the transport streams the request body in chunks. Accumulate
     * it so we can surface it to tests, then drain it so MHD advances. */
    if (strcmp(method, "GET") != 0 && *upload_data_size != 0) {
        if (st->len + *upload_data_size + 1 > st->cap) {
            size_t ncap = st->cap == 0 ? 256 : st->cap * 2;
            while (ncap < st->len + *upload_data_size + 1) {
                ncap *= 2;
            }
            char *nb = (char *)realloc(st->body, ncap);
            if (nb == NULL) {
                return MHD_NO;
            }
            st->body = nb;
            st->cap = ncap;
        }
        memcpy(st->body + st->len, upload_data, *upload_data_size);
        st->len += *upload_data_size;
        st->body[st->len] = '\0';
        *upload_data_size = 0;
        return MHD_YES;
    }

    /* upload_data_size == 0: body fully read (or a GET / bodyless request). */

    const char *body = NULL;
    unsigned int status = MHD_HTTP_OK;

    const char *nsid = url;
    if (strncmp(url, "/xrpc/", 6) == 0) {
        nsid = url + 6;
        body = mock_lookup(pds, nsid);
    }
    if (body == NULL) {
        status = MHD_HTTP_NOT_FOUND;
        body = MOCK_NOT_FOUND_BODY;
    }

    /* Capture the request for test assertions (replace any previous one). */
    free(pds->last_nsid);
    free(pds->last_method);
    free(pds->last_body);
    pds->last_nsid = strdup(nsid);
    pds->last_method = strdup(method);
    if (*con_cls != NULL) {
        struct wf_mock_pds_upload *st = (struct wf_mock_pds_upload *)*con_cls;
        pds->last_body = st->len > 0 ? strdup(st->body) : NULL;
        free(st->body);
        free(st);
        *con_cls = NULL;
    } else {
        pds->last_body = NULL;
    }

    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(body), (void *)(uintptr_t)body, MHD_RESPMEM_MUST_COPY);
    if (resp == NULL) {
        return MHD_NO;
    }

    MHD_add_response_header(resp, "Content-Type", "application/json");
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");

    enum MHD_Result ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

wf_status wf_mock_pds_start(wf_mock_pds **out, int *out_port) {
    if (out == NULL || out_port == NULL) {
        return WF_ERR_INVALID_ARG;
    }

    wf_mock_pds *pds = (wf_mock_pds *)calloc(1, sizeof(*pds));
    if (pds == NULL) {
        return WF_ERR_ALLOC;
    }

    pds->daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD,
                                   0, /* port 0 => ephemeral */
                                   NULL, NULL,
                                   mock_handler, pds,
                                   MHD_OPTION_END);
    if (pds->daemon == NULL) {
        free(pds);
        return WF_ERR_NETWORK;
    }

    const union MHD_DaemonInfo *info =
        MHD_get_daemon_info(pds->daemon, MHD_DAEMON_INFO_BIND_PORT);
    if (info == NULL) {
        MHD_stop_daemon(pds->daemon);
        free(pds);
        return WF_ERR_NETWORK;
    }

    pds->port = (int)info->port;
    *out = pds;
    *out_port = pds->port;
    return WF_OK;
}

wf_status wf_mock_pds_register(wf_mock_pds *pds,
                               const char *nsid,
                               const char *json) {
    if (pds == NULL || nsid == NULL || json == NULL) {
        return WF_ERR_INVALID_ARG;
    }

    /* Replace an existing entry for the same NSID. */
    for (size_t i = 0; i < pds->count; i++) {
        if (strcmp(pds->entries[i].nsid, nsid) == 0) {
            char *dup = strdup(json);
            if (dup == NULL) {
                return WF_ERR_ALLOC;
            }
            free(pds->entries[i].json);
            pds->entries[i].json = dup;
            return WF_OK;
        }
    }

    if (pds->count == pds->cap) {
        size_t ncap = pds->cap == 0 ? 8 : pds->cap * 2;
        struct wf_mock_pds_entry *n =
            (struct wf_mock_pds_entry *)realloc(pds->entries,
                                                ncap * sizeof(*n));
        if (n == NULL) {
            return WF_ERR_ALLOC;
        }
        pds->entries = n;
        pds->cap = ncap;
    }

    char *nsid_dup = strdup(nsid);
    char *json_dup = strdup(json);
    if (nsid_dup == NULL || json_dup == NULL) {
        free(nsid_dup);
        free(json_dup);
        return WF_ERR_ALLOC;
    }

    pds->entries[pds->count].nsid = nsid_dup;
    pds->entries[pds->count].json = json_dup;
    pds->count++;
    return WF_OK;
}

wf_status wf_mock_pds_stop(wf_mock_pds *pds) {
    if (pds == NULL) {
        return WF_ERR_INVALID_ARG;
    }
    if (pds->daemon != NULL) {
        MHD_stop_daemon(pds->daemon);
        pds->daemon = NULL;
    }
    return WF_OK;
}

void wf_mock_pds_free(wf_mock_pds *pds) {
    if (pds == NULL) {
        return;
    }
    wf_mock_pds_stop(pds);
    for (size_t i = 0; i < pds->count; i++) {
        free(pds->entries[i].nsid);
        free(pds->entries[i].json);
    }
    free(pds->entries);
    free(pds->last_nsid);
    free(pds->last_method);
    free(pds->last_body);
    free(pds);
}

wf_status wf_mock_pds_get_last_request(wf_mock_pds *pds,
                                        const char **nsid,
                                        const char **method,
                                        const char **body) {
    if (pds == NULL || nsid == NULL || method == NULL || body == NULL) {
        return WF_ERR_INVALID_ARG;
    }
    /* When no request has been served yet, all out-params are set to NULL. */
    *nsid = pds->last_nsid;
    *method = pds->last_method;
    *body = pds->last_body;
    return WF_OK;
}
