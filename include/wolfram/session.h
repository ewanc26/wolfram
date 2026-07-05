/**
 * session.h — PDS session management.
 *
 * Wraps an XRPC client with credential-based session management:
 * login (createSession), refresh (refreshSession), get (getSession),
 * and logout (deleteSession). Manages auth tokens on the underlying
 * client automatically.
 *
 * The session owns its XRPC client — freeing the session frees the client.
 */

#ifndef WOLFRAM_SESSION_H
#define WOLFRAM_SESSION_H

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Session data, populated by login/refresh and read by the caller. */
typedef struct wf_session_data {
    char  *access_jwt;
    char  *refresh_jwt;
    char  *handle;
    char  *did;
    char  *email;           /* NULL if not returned by server */
    int    email_confirmed; /* -1 if unknown */
    int    email_auth_factor; /* -1 if unknown */
    int    active;          /* -1 if unknown */
    char  *status;          /* NULL if not returned */
} wf_session_data;

/** A session manager. Owns its XRPC client. */
typedef struct wf_session {
    wf_xrpc_client  *client;
    wf_session_data  data;
    int              has_session;
} wf_session;

/**
 * Create a new session manager bound to a PDS service URL.
 * Returns NULL on allocation failure.
 */
wf_session *wf_session_new(const char *service_base_url);

/** Free a session and its underlying XRPC client. Safe to call with NULL. */
void wf_session_free(wf_session *session);

/**
 * Log in with an identifier (handle or email) and password.
 * Calls com.atproto.server.createSession.
 *
 * On WF_OK, session->data is populated and the access JWT is set
 * as the auth token on the underlying client.
 */
wf_status wf_session_login(wf_session *session,
                            const char *identifier,
                            const char *password);

/**
 * Resume previously persisted credentials, then immediately refresh them.
 *
 * `data` and all strings it points to remain caller-owned; the session makes
 * a deep copy. At minimum access_jwt, refresh_jwt, handle, and did are
 * required. The copied credentials remain installed if the refresh fails.
 */
wf_status wf_session_resume(wf_session *session, const wf_session_data *data);

/**
 * Refresh the session using the refresh JWT.
 * Calls com.atproto.server.refreshSession.
 *
 * Temporarily swaps the auth token to the refresh JWT for this call,
 * then restores the new access JWT afterwards.
 *
 * On WF_OK, session->data is updated with fresh tokens.
 */
wf_status wf_session_refresh(wf_session *session);

/**
 * Get current session info from the server.
 * Calls com.atproto.server.getSession (GET, requires access JWT).
 *
 * On WF_OK, session->data fields are updated from the server response.
 */
wf_status wf_session_get(wf_session *session);

/**
 * Log out — delete the current session on the server.
 * Calls com.atproto.server.deleteSession (POST, requires refresh JWT).
 *
 * Clears session data and auth token regardless of server response.
 */
wf_status wf_session_delete(wf_session *session);

/** Returns 1 if the session has tokens (login succeeded), 0 otherwise. */
int wf_session_has_session(const wf_session *session);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SESSION_H */
