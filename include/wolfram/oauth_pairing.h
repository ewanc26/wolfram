/*
 * oauth_pairing.h -- client side of the sign-in pairing contract spoken by the
 * hosted OAuth node. docs/oauth-pairing.md is the single specification and
 * test/vectors/oauth_pairing.json holds the shared test vectors.
 *
 * A console that cannot run a browser OAuth flow asks the node to begin a
 * pairing for a handle, shows the returned URL to the user, then polls the
 * node until the user has finished signing in on another device. This header
 * owns the request body, the response parsing and the begin/poll loop, so a
 * client does not hand-roll cJSON walks over `uk.ewancroft.oauth.begin` and
 * `uk.ewancroft.oauth.poll`.
 *
 * Builds on every target, console targets included (fixed buffers, no
 * threads). The node itself (oauth_node.h) is hosted-only.
 *
 * Security: the poll `token` is a bearer credential. Nothing here logs it, the
 * pair code, a poll URL or a raw response body. Call
 * wf_oauth_pair_poll_wipe() on a result when finished with it.
 */

#ifndef WOLFRAM_OAUTH_PAIRING_H
#define WOLFRAM_OAUTH_PAIRING_H

#include "wolfram/xrpc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_OAUTH_PAIR_BEGIN_NSID "uk.ewancroft.oauth.begin"
#define WF_OAUTH_PAIR_POLL_NSID "uk.ewancroft.oauth.poll"

/* Field capacities, including the terminating NUL. A response value that does
 * not fit is a parse error, never a silent truncation (except `message`, which
 * is only human-readable text and is truncated). */
#define WF_OAUTH_PAIR_CODE_MAX 64
#define WF_OAUTH_PAIR_URL_MAX 512
#define WF_OAUTH_PAIR_TOKEN_MAX 256
#define WF_OAUTH_PAIR_HANDLE_MAX 256
#define WF_OAUTH_PAIR_DID_MAX 256
#define WF_OAUTH_PAIR_MESSAGE_MAX 256

/* Recommended cadence: one poll every 1500 ms, at most 360 polls (nine
 * minutes). The node's default pairing lifetime is ten minutes. */
#define WF_OAUTH_PAIR_POLL_INTERVAL_MS 1500u
#define WF_OAUTH_PAIR_MAX_POLLS 360u

typedef struct wf_oauth_pair_begin {
    char pair_code[WF_OAUTH_PAIR_CODE_MAX];
    char pair_url[WF_OAUTH_PAIR_URL_MAX]; /* http(s) URL to open elsewhere */
    int64_t expires_at;                   /* Unix seconds; 0 if not given */
} wf_oauth_pair_begin;

typedef enum wf_oauth_poll_state {
    WF_OAUTH_POLL_PENDING = 0,  /* keep polling */
    WF_OAUTH_POLL_COMPLETE = 1, /* token, handle, did, service are set */
    WF_OAUTH_POLL_ERROR = 2     /* terminal; message may be set (or empty) */
} wf_oauth_poll_state;

typedef struct wf_oauth_pair_poll {
    wf_oauth_poll_state state;
    char token[WF_OAUTH_PAIR_TOKEN_MAX]; /* opaque node bearer */
    char handle[WF_OAUTH_PAIR_HANDLE_MAX];
    char did[WF_OAUTH_PAIR_DID_MAX]; /* starts with "did:" */
    /* The node's public base URL. Use it as the bearer's audience: send the
     * token to this host only, and do not follow a different one. */
    char service[WF_OAUTH_PAIR_URL_MAX];
    char message[WF_OAUTH_PAIR_MESSAGE_MAX]; /* ERROR only; may be empty */
} wf_oauth_pair_poll;

/* Write the begin request body `{"handle":...}` into `out`. Rejects an empty
 * handle or one with control characters (WF_ERR_INVALID_ARG) and a body that
 * does not fit `cap` (WF_ERR_INVALID_ARG). */
wf_status wf_oauth_pair_build_begin(const char *handle, char *out, size_t cap);

/* Pure parsers. On any error `out` is zeroed, so a half-filled session is
 * never handed out. Failure to satisfy the contract is WF_ERR_PARSE. */
wf_status wf_oauth_pair_parse_begin(const char *body, size_t len,
                                    wf_oauth_pair_begin *out);
wf_status wf_oauth_pair_parse_poll(const char *body, size_t len,
                                   wf_oauth_pair_poll *out);

/* Securely zero a poll result (it holds the bearer token). Not optimised away.
 */
void wf_oauth_pair_poll_wipe(wf_oauth_pair_poll *p);

/* One begin request / one poll request against the node at `client`'s base
 * URL. For poll: a transport failure or 5xx is returned as its wf_status and
 * is transient (poll again); a parsed reply, including ERROR, is WF_OK; an
 * HTTP 404 (unknown or purged code) is WF_OK with state ERROR and message
 * "Unknown pairing code.", which is terminal. */
wf_status wf_oauth_pair_begin_request(wf_xrpc_client *client,
                                      const char *handle,
                                      wf_oauth_pair_begin *out);
wf_status wf_oauth_pair_poll_once(wf_xrpc_client *client, const char *code,
                                  wf_oauth_pair_poll *out);

/* Hooks for the driver. Every hook is called synchronously on the thread
 * that calls wf_oauth_pair_run (a client's session worker, typically), never
 * from another thread, so a hook may take that client's own locks. `sleep_ms` is required (the SDK has no portable
 * sleep); the rest may be NULL. */
typedef struct wf_oauth_pair_hooks {
    /* Called once, after begin succeeds, to show the user the URL and code. */
    void (*on_code)(const wf_oauth_pair_begin *begin, void *userdata);
    /* Return nonzero to stop; checked before every poll. */
    int (*cancel)(void *userdata);
    void (*sleep_ms)(unsigned ms, void *userdata);
    /* Current Unix time in seconds; NULL uses time(NULL). */
    int64_t (*now)(void *userdata);
    void *userdata;
} wf_oauth_pair_hooks;

/*
 * The whole begin/poll loop, written once. Returns:
 *   WF_OK            signed in; `out` is COMPLETE
 *   WF_ERR_AUTH      the node reported a terminal error (including a 404 for
 *                    an unknown code, which is not polled again); `out` is
 *                    ERROR with `message`
 *   WF_ERR_TIMEOUT   expires_at passed (or WF_OAUTH_PAIR_MAX_POLLS reached
 *                    when the node gave no expiry) without completion
 *   WF_ERR_STATE     cancelled by the hook
 *   WF_ERR_PARSE     the node broke the contract on a 2xx reply
 *   other            begin failed
 * Transient poll failures are retried. `out` is wiped on every non-OK return
 * except ERROR, which keeps only its message.
 */
wf_status wf_oauth_pair_run(wf_xrpc_client *client, const char *handle,
                            const wf_oauth_pair_hooks *hooks,
                            wf_oauth_pair_poll *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_PAIRING_H */
