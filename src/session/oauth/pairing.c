/*
 * pairing.c -- client side of the hosted OAuth node's pairing contract
 * (see wolfram/oauth_pairing.h and docs/oauth-pairing.md).
 *
 * Deliberately never logs: the poll reply carries a bearer token.
 */

#include "wolfram/oauth_pairing.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Borrowed non-empty string member, or NULL. */
static const char *pair_str(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0])
        return NULL;
    return v->valuestring;
}

/* Copy into a fixed buffer; 0 if it does not fit (never truncates). */
static int pair_copy(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) return 0;
    memcpy(dst, src, n + 1);
    return 1;
}

static int pair_is_http_url(const char *s) {
    return strncmp(s, "https://", 8) == 0 || strncmp(s, "http://", 7) == 0;
}

void wf_oauth_pair_poll_wipe(wf_oauth_pair_poll *p) {
    volatile unsigned char *b = (volatile unsigned char *)p;
    if (!p) return;
    for (size_t i = 0; i < sizeof(*p); ++i) b[i] = 0;
}

wf_status wf_oauth_pair_build_begin(const char *handle, char *out, size_t cap) {
    if (!handle || !handle[0] || !out || cap == 0) return WF_ERR_INVALID_ARG;
    out[0] = '\0';
    for (const unsigned char *p = (const unsigned char *)handle; *p; ++p)
        if (*p < 0x20 || *p == 0x7f) return WF_ERR_INVALID_ARG;
    cJSON *o = cJSON_CreateObject();
    if (!o) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(o, "handle", handle)) {
        cJSON_Delete(o);
        return WF_ERR_ALLOC;
    }
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s) return WF_ERR_ALLOC;
    int ok = pair_copy(out, cap, s);
    free(s);
    return ok ? WF_OK : WF_ERR_INVALID_ARG;
}

wf_status wf_oauth_pair_parse_begin(const char *body, size_t len,
                                    wf_oauth_pair_begin *out) {
    if (!body || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(body, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    const char *code = pair_str(root, "pair_code");
    const char *url = pair_str(root, "pair_url");
    const cJSON *exp = cJSON_GetObjectItemCaseSensitive(root, "expires_at");
    if (!code || !url || !pair_is_http_url(url) ||
        (exp && !cJSON_IsNumber(exp)) ||
        !pair_copy(out->pair_code, sizeof out->pair_code, code) ||
        !pair_copy(out->pair_url, sizeof out->pair_url, url)) {
        memset(out, 0, sizeof(*out));
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    if (exp && exp->valuedouble > 0)
        out->expires_at = (int64_t)exp->valuedouble;
    cJSON_Delete(root);
    return WF_OK;
}

wf_status wf_oauth_pair_parse_poll(const char *body, size_t len,
                                   wf_oauth_pair_poll *out) {
    if (!body || !out) return WF_ERR_INVALID_ARG;
    wf_oauth_pair_poll_wipe(out);
    cJSON *root = cJSON_ParseWithLength(body, len);
    const cJSON *st = cJSON_IsObject(root)
                          ? cJSON_GetObjectItemCaseSensitive(root, "status")
                          : NULL;
    wf_status rc = WF_ERR_PARSE;
    if (cJSON_IsString(st) && st->valuestring) {
        if (strcmp(st->valuestring, "pending") == 0) {
            out->state = WF_OAUTH_POLL_PENDING;
            rc = WF_OK;
        } else if (strcmp(st->valuestring, "error") == 0) {
            const char *m = pair_str(root, "message");
            out->state = WF_OAUTH_POLL_ERROR;
            if (m) {
                strncpy(out->message, m, sizeof out->message - 1);
                out->message[sizeof out->message - 1] = '\0';
            }
            rc = WF_OK;
        } else if (strcmp(st->valuestring, "complete") == 0) {
            const char *token = pair_str(root, "token");
            const char *handle = pair_str(root, "handle");
            const char *did = pair_str(root, "did");
            const char *service = pair_str(root, "service");
            if (token && handle && did && service &&
                strncmp(did, "did:", 4) == 0 && pair_is_http_url(service) &&
                pair_copy(out->token, sizeof out->token, token) &&
                pair_copy(out->handle, sizeof out->handle, handle) &&
                pair_copy(out->did, sizeof out->did, did) &&
                pair_copy(out->service, sizeof out->service, service)) {
                out->state = WF_OAUTH_POLL_COMPLETE;
                rc = WF_OK;
            }
        }
    }
    cJSON_Delete(root);
    if (rc != WF_OK) wf_oauth_pair_poll_wipe(out);
    return rc;
}

wf_status wf_oauth_pair_begin_request(wf_xrpc_client *client,
                                      const char *handle,
                                      wf_oauth_pair_begin *out) {
    if (!client || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    char body[512];
    wf_status st = wf_oauth_pair_build_begin(handle, body, sizeof body);
    if (st != WF_OK) return st;
    wf_response resp;
    memset(&resp, 0, sizeof(resp));
    st = wf_xrpc_procedure(client, WF_OAUTH_PAIR_BEGIN_NSID, body, &resp);
    if (st == WF_OK)
        st = wf_oauth_pair_parse_begin(resp.body ? resp.body : "",
                                       resp.body_len, out);
    wf_response_free(&resp);
    return st;
}

wf_status wf_oauth_pair_poll_once(wf_xrpc_client *client, const char *code,
                                  wf_oauth_pair_poll *out) {
    if (!client || !code || !code[0] || !out) return WF_ERR_INVALID_ARG;
    wf_oauth_pair_poll_wipe(out);
    wf_xrpc_param param = {"code", code};
    wf_response resp;
    memset(&resp, 0, sizeof(resp));
    wf_status st =
        wf_xrpc_query_params(client, WF_OAUTH_PAIR_POLL_NSID, &param, 1, &resp);
    if (st == WF_OK) {
        st = wf_oauth_pair_parse_poll(resp.body ? resp.body : "", resp.body_len,
                                      out);
    } else if (st == WF_ERR_HTTP && resp.status == 404) {
        out->state = WF_OAUTH_POLL_ERROR;
        strcpy(out->message, "Unknown pairing code.");
        st = WF_OK;
    }
    /* The reply body holds the token on completion: wipe before freeing. */
    if (resp.body) {
        volatile char *b = (volatile char *)resp.body;
        for (size_t i = 0; i < resp.body_len; ++i) b[i] = 0;
    }
    wf_response_free(&resp);
    return st;
}

wf_status wf_oauth_pair_run(wf_xrpc_client *client, const char *handle,
                            const wf_oauth_pair_hooks *hooks,
                            wf_oauth_pair_poll *out) {
    if (!client || !handle || !hooks || !hooks->sleep_ms || !out)
        return WF_ERR_INVALID_ARG;
    wf_oauth_pair_poll_wipe(out);

    wf_oauth_pair_begin begin;
    wf_status st = wf_oauth_pair_begin_request(client, handle, &begin);
    if (st != WF_OK) return st;
    if (hooks->on_code) hooks->on_code(&begin, hooks->userdata);

    for (unsigned polls = 0;; ++polls) {
        if (hooks->cancel && hooks->cancel(hooks->userdata))
            return WF_ERR_STATE;
        if (polls >= WF_OAUTH_PAIR_MAX_POLLS && begin.expires_at == 0)
            return WF_ERR_TIMEOUT;
        if (polls > 0)
            hooks->sleep_ms(WF_OAUTH_PAIR_POLL_INTERVAL_MS, hooks->userdata);
        if (begin.expires_at > 0) {
            int64_t now =
                hooks->now ? hooks->now(hooks->userdata) : (int64_t)time(NULL);
            if (now > begin.expires_at) return WF_ERR_TIMEOUT;
        }

        st = wf_oauth_pair_poll_once(client, begin.pair_code, out);
        if (st == WF_OK) {
            if (out->state == WF_OAUTH_POLL_COMPLETE) return WF_OK;
            if (out->state == WF_OAUTH_POLL_ERROR) return WF_ERR_AUTH;
            continue; /* pending */
        }
        if (st == WF_ERR_PARSE || st == WF_ERR_ALLOC ||
            st == WF_ERR_INVALID_ARG)
            return st; /* the node broke the contract: not transient */
        /* Network failure or non-404 HTTP error: transient, poll again. */
    }
}
