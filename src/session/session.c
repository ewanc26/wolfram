/**
 * session.c — PDS session management implementation.
 *
 * Wraps com.atproto.server.{createSession,refreshSession,getSession,
 * deleteSession} over the XRPC transport. Manages auth tokens on the
 * underlying client — swapping to refreshJwt for refresh/delete calls,
 * then restoring the accessJwt.
 *
 * JSON parsing uses cJSON. All string fields in wf_session_data are
 * heap-owned and freed by wf_session_free.
 */

#include "wolfram/session.h"
#include "wolfram/xrpc.h"
#include "wolfram/identity.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────── */

static char *wf_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

static void wf_session_data_free(wf_session_data *data) {
    if (!data) return;
    free(data->access_jwt);
    free(data->refresh_jwt);
    free(data->handle);
    free(data->did);
    free(data->email);
    free(data->status);
    free(data->pds_url);
    memset(data, 0, sizeof(*data));
    data->email_confirmed = -1;
    data->email_auth_factor = -1;
    data->active = -1;
}

static void wf_session_data_init(wf_session_data *data) {
    memset(data, 0, sizeof(*data));
    data->email_confirmed = -1;
    data->email_auth_factor = -1;
    data->active = -1;
}

static wf_status wf_session_data_copy(wf_session_data *dst,
                                       const wf_session_data *src) {
    if (!dst || !src || !src->access_jwt || !src->refresh_jwt ||
        !src->handle || !src->did) {
        return WF_ERR_INVALID_ARG;
    }

    wf_session_data_init(dst);
    dst->access_jwt = wf_strdup(src->access_jwt);
    dst->refresh_jwt = wf_strdup(src->refresh_jwt);
    dst->handle = wf_strdup(src->handle);
    dst->did = wf_strdup(src->did);
    dst->email = wf_strdup(src->email);
    dst->status = wf_strdup(src->status);
    dst->pds_url = wf_strdup(src->pds_url);
    dst->email_confirmed = src->email_confirmed;
    dst->email_auth_factor = src->email_auth_factor;
    dst->active = src->active;

    if (!dst->access_jwt || !dst->refresh_jwt || !dst->handle || !dst->did ||
        (src->email && !dst->email) || (src->status && !dst->status) ||
        (src->pds_url && !dst->pds_url)) {
        wf_session_data_free(dst);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/**
 * Parse a session JSON response (from createSession or refreshSession)
 * into wf_session_data. Required fields: accessJwt, refreshJwt, handle, did.
 */
static wf_status wf_session_data_parse(wf_session_data *data, const char *json, size_t json_len) {
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;

    cJSON *access = cJSON_GetObjectItemCaseSensitive(root, "accessJwt");
    cJSON *refresh = cJSON_GetObjectItemCaseSensitive(root, "refreshJwt");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");

    if (!cJSON_IsString(access) || !cJSON_IsString(refresh) ||
        !cJSON_IsString(handle) || !cJSON_IsString(did)) {
        status = WF_ERR_PARSE;
        goto done;
    }

    data->access_jwt = wf_strdup(access->valuestring);
    data->refresh_jwt = wf_strdup(refresh->valuestring);
    data->handle = wf_strdup(handle->valuestring);
    data->did = wf_strdup(did->valuestring);

    if (!data->access_jwt || !data->refresh_jwt ||
        !data->handle || !data->did) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    /* Optional fields */
    cJSON *email = cJSON_GetObjectItemCaseSensitive(root, "email");
    if (cJSON_IsString(email)) {
        data->email = wf_strdup(email->valuestring);
    }

    cJSON *email_confirmed = cJSON_GetObjectItemCaseSensitive(root, "emailConfirmed");
    if (cJSON_IsBool(email_confirmed)) {
        data->email_confirmed = cJSON_IsTrue(email_confirmed) ? 1 : 0;
    }

    cJSON *email_auth = cJSON_GetObjectItemCaseSensitive(root, "emailAuthFactor");
    if (cJSON_IsBool(email_auth)) {
        data->email_auth_factor = cJSON_IsTrue(email_auth) ? 1 : 0;
    }

    cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    if (cJSON_IsBool(active)) {
        data->active = cJSON_IsTrue(active) ? 1 : 0;
    }

    cJSON *status_obj = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(status_obj)) {
        data->status = wf_strdup(status_obj->valuestring);
    }

    if ((cJSON_IsString(email) && !data->email) ||
        (cJSON_IsString(status_obj) && !data->status)) {
        status = WF_ERR_ALLOC;
        goto done;
    }

    /* Discover the account's PDS from the embedded DID document, if present.
     * The endpoint is the didDoc service entry with id "#atproto_pds". A
     * missing or unparseable didDoc is non-fatal — we simply keep whatever
     * service URL the session was created against (backward compatible). */
    cJSON *did_doc = cJSON_GetObjectItemCaseSensitive(root, "didDoc");
    if (cJSON_IsObject(did_doc)) {
        char *did_doc_json = cJSON_PrintUnformatted(did_doc);
        if (did_doc_json) {
            wf_did_document doc = {0};
            if (wf_did_document_parse(did_doc_json, strlen(did_doc_json),
                                      &doc) == WF_OK &&
                doc.pds_endpoint) {
                free(data->pds_url);
                data->pds_url = wf_strdup(doc.pds_endpoint);
                if (!data->pds_url) status = WF_ERR_ALLOC;
            }
            wf_did_document_free(&doc);
            free(did_doc_json);
        }
    }

done:
    cJSON_Delete(root);
    return status;
}

/**
 * Update session data from a getSession response (which lacks
 * accessJwt/refreshJwt). Only updates fields present in the response.
 */
static wf_status wf_session_data_update_from_get(wf_session_data *data,
                                                   const char *json, size_t json_len) {
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");

    if (!cJSON_IsString(handle) || !cJSON_IsString(did) ||
        strcmp(did->valuestring, data->did) != 0) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    char *new_handle = wf_strdup(handle->valuestring);
    char *new_email = NULL;
    char *new_status = NULL;
    if (!new_handle) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    cJSON *email = cJSON_GetObjectItemCaseSensitive(root, "email");
    if (cJSON_IsString(email)) {
        new_email = wf_strdup(email->valuestring);
    }

    cJSON *email_confirmed = cJSON_GetObjectItemCaseSensitive(root, "emailConfirmed");
    if (cJSON_IsBool(email_confirmed)) {
        data->email_confirmed = cJSON_IsTrue(email_confirmed) ? 1 : 0;
    }

    cJSON *email_auth = cJSON_GetObjectItemCaseSensitive(root, "emailAuthFactor");
    if (cJSON_IsBool(email_auth)) {
        data->email_auth_factor = cJSON_IsTrue(email_auth) ? 1 : 0;
    }

    cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    if (cJSON_IsBool(active)) {
        data->active = cJSON_IsTrue(active) ? 1 : 0;
    }

    cJSON *status_obj = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(status_obj)) {
        new_status = wf_strdup(status_obj->valuestring);
    }

    if ((cJSON_IsString(email) && !new_email) ||
        (cJSON_IsString(status_obj) && !new_status)) {
        free(new_handle);
        free(new_email);
        free(new_status);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    free(data->handle);
    data->handle = new_handle;
    if (cJSON_IsString(email)) {
        free(data->email);
        data->email = new_email;
    }
    if (cJSON_IsString(status_obj)) {
        free(data->status);
        data->status = new_status;
    }

    cJSON_Delete(root);
    return WF_OK;
}

/* ── public API ───────────────────────────────────────────── */

/* Re-point the session's XRPC client at the PDS discovered from the didDoc so
 * that subsequent calls go to the account's real PDS rather than the login
 * host. A no-op when no PDS was discovered. */
static void wf_session_apply_pds(wf_session *session) {
    if (!session || !session->data.pds_url || !session->data.pds_url[0]) {
        return;
    }
    /* Best effort: on failure the client keeps its previous base URL. */
    wf_xrpc_client_set_base_url(session->client, session->data.pds_url);
}

/* Classify a failed session XRPC call: auth errors (expired/invalid/failed
 * credentials) should clear the session, while transient network/transport
 * errors should keep it. */
static int wf_session_is_auth_error(wf_status status, const wf_response *res) {
    if (status == WF_OK) return 0;
    if (res && res->status == 401) return 1;

    char *err = NULL;
    if (res && wf_xrpc_error(res, &err, NULL) == WF_OK && err) {
        int match = strcmp(err, "ExpiredToken") == 0 ||
                    strcmp(err, "InvalidToken") == 0 ||
                    strcmp(err, "AuthFailed") == 0;
        free(err);
        return match;
    }
    free(err);
    return 0;
}

wf_session *wf_session_new(const char *service_base_url) {
    wf_session *session = calloc(1, sizeof(*session));
    if (!session) return NULL;

    session->client = wf_xrpc_client_new(service_base_url);
    if (!session->client) {
        free(session);
        return NULL;
    }

    wf_session_data_init(&session->data);
    session->has_session = 0;
    return session;
}

void wf_session_free(wf_session *session) {
    if (!session) return;
    wf_session_data_free(&session->data);
    wf_xrpc_client_free(session->client);
    free(session);
}

wf_status wf_session_login(wf_session *session,
                            const char *identifier,
                            const char *password) {
    return wf_session_login_with_opts(session, identifier, password, NULL);
}

wf_status wf_session_login_with_opts(wf_session *session,
                                     const char *identifier,
                                     const char *password,
                                     const wf_session_login_opts *opts) {
    if (!session || !identifier || !password) {
        return WF_ERR_INVALID_ARG;
    }

    /* Clear any existing session */
    wf_session_data_free(&session->data);
    wf_session_data_init(&session->data);
    wf_xrpc_client_set_auth(session->client, NULL);
    session->has_session = 0;

    /* Build JSON body: {"identifier":"...","password":"...",
     * ["authFactorToken":"...", "allowTakendown":true]} */
    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;

    if (!cJSON_AddStringToObject(body, "identifier", identifier) ||
        !cJSON_AddStringToObject(body, "password", password)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    if (opts && opts->auth_factor_token && opts->auth_factor_token[0]) {
        if (!cJSON_AddStringToObject(body, "authFactorToken",
                                     opts->auth_factor_token)) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }
    if (opts && opts->allow_takendown) {
        if (!cJSON_AddBoolToObject(body, "allowTakendown", 1)) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return WF_ERR_ALLOC;

    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(session->client,
                                          "com.atproto.server.createSession",
                                          json_str, &res);
    free(json_str);

    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_session_data_parse(&session->data, res.body, res.body_len);
    wf_response_free(&res);

    if (status != WF_OK) {
        wf_session_data_free(&session->data);
        wf_session_data_init(&session->data);
        return status;
    }

    /* Set the access JWT as the auth token on the client */
    wf_xrpc_client_set_auth(session->client, session->data.access_jwt);
    session->has_session = 1;

    /* Route subsequent calls at the account's discovered PDS. */
    wf_session_apply_pds(session);

    return WF_OK;
}

wf_status wf_session_resume(wf_session *session, const wf_session_data *data) {
    if (!session || !data) return WF_ERR_INVALID_ARG;

    wf_session_data copy;
    wf_status status = wf_session_data_copy(&copy, data);
    if (status != WF_OK) return status;

    wf_session_data_free(&session->data);
    session->data = copy;
    session->has_session = 1;
    wf_xrpc_client_set_auth(session->client, session->data.access_jwt);

    /* If the persisted data already carries a PDS endpoint, route at it before
     * validating the credentials. */
    wf_session_apply_pds(session);

    /* Match the reference client's semantics: install first, then validate
     * persisted credentials by forcing a token refresh. */
    return wf_session_refresh(session);
}

wf_status wf_session_refresh(wf_session *session) {
    if (!session || !session->has_session) {
        return WF_ERR_INVALID_ARG;
    }

    /* Swap to refresh JWT for this call */
    wf_xrpc_client_set_auth(session->client, session->data.refresh_jwt);

    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(session->client,
                                          "com.atproto.server.refreshSession",
                                          NULL, &res);

    if (status != WF_OK) {
        if (wf_session_is_auth_error(status, &res)) {
            /* Credentials are no longer valid: drop the session entirely, as
             * the reference client does on ExpiredToken/InvalidToken. */
            wf_session_data_free(&session->data);
            wf_session_data_init(&session->data);
            wf_xrpc_client_set_auth(session->client, NULL);
            session->has_session = 0;
        } else {
            /* Transient/network error: keep the session and restore the
             * access JWT so later calls can retry. */
            wf_xrpc_client_set_auth(session->client, session->data.access_jwt);
        }
        wf_response_free(&res);
        return status;
    }

    /* Parse the new tokens */
    wf_session_data new_data;
    wf_session_data_init(&new_data);

    status = wf_session_data_parse(&new_data, res.body, res.body_len);
    wf_response_free(&res);

    if (status != WF_OK) {
        /* Refresh failed — keep old session data, restore access JWT */
        wf_session_data_free(&new_data);
        wf_xrpc_client_set_auth(session->client, session->data.access_jwt);
        return status;
    }

    /* Verify the DID didn't change (security check from the reference) */
    if (strcmp(new_data.did, session->data.did) != 0) {
        wf_session_data_free(&new_data);
        wf_xrpc_client_set_auth(session->client, session->data.access_jwt);
        return WF_ERR_PARSE;
    }

    /* Preserve fields that refreshSession might not return */
    if (!new_data.email && session->data.email) {
        new_data.email = wf_strdup(session->data.email);
    }
    if (!new_data.pds_url && session->data.pds_url) {
        new_data.pds_url = wf_strdup(session->data.pds_url);
    }
    if (new_data.email_confirmed == -1) {
        new_data.email_confirmed = session->data.email_confirmed;
    }
    if (new_data.email_auth_factor == -1) {
        new_data.email_auth_factor = session->data.email_auth_factor;
    }
    if (new_data.active == -1) {
        new_data.active = session->data.active;
    }

    /* Swap in the new data */
    wf_session_data_free(&session->data);
    session->data = new_data;

    /* Set the new access JWT on the client */
    wf_xrpc_client_set_auth(session->client, session->data.access_jwt);

    /* Keep routing at the account's PDS (may have been refreshed above). */
    wf_session_apply_pds(session);

    return WF_OK;
}

wf_status wf_session_get(wf_session *session) {
    if (!session || !session->has_session) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_xrpc_query(session->client,
                                      "com.atproto.server.getSession",
                                      NULL, &res);

    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_session_data_update_from_get(&session->data, res.body, res.body_len);
    wf_response_free(&res);

    return status;
}

wf_status wf_session_delete(wf_session *session) {
    if (!session || !session->has_session) {
        return WF_ERR_INVALID_ARG;
    }

    /* Swap to refresh JWT for this call */
    wf_xrpc_client_set_auth(session->client, session->data.refresh_jwt);

    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(session->client,
                                          "com.atproto.server.deleteSession",
                                          NULL, &res);

    wf_response_free(&res);

    /* Clear session regardless of server response */
    wf_session_data_free(&session->data);
    wf_session_data_init(&session->data);
    wf_xrpc_client_set_auth(session->client, NULL);
    session->has_session = 0;

    return status;
}

int wf_session_has_session(const wf_session *session) {
    return session ? session->has_session : 0;
}
