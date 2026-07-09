/*
 * bsky_agent.h — high-level "BskyAgent" convenience wrapper.
 *
 * Bundles an existing wf_agent (session + XRPC transport + identity) behind a
 * flat, ergonomic API. This layer deliberately does NO network I/O of its own:
 * every method validates its inputs and forwards to the matching wf_agent_*
 * call, exactly as an application would use wf_agent directly.
 *
 * Composition note:
 *   wf_agent is an opaque type (its full definition lives in src/agent/
 *   _internal.h and is intentionally not exposed through the public headers),
 *   so it cannot be embedded by value in a public header. We therefore hold a
 *   wf_agent * here — the standard C pattern for composing an opaque type. This
 *   still satisfies the design goal of NOT duplicating session/xrpc/client
 *   state: the single pointed-to wf_agent owns and manages all of that.
 *
 * Ownership:
 *   - wf_bsky_agent_init zeroes a struct; no allocation.
 *   - wf_bsky_agent_login / wf_bsky_agent_login_session lazily allocate the
 *     underlying wf_agent on first use. The bsky_agent owns that pointer.
 *   - wf_bsky_agent_free releases the underlying wf_agent (and thus its
 *     session/client) via wf_agent_free, then zeroes the struct. It is a
 *     teardown only — it does NOT issue a network logout; callers wanting to
 *     invalidate the server session should call wf_bsky_agent_logout first.
 */

#ifndef WOLFRAM_BSKY_AGENT_H
#define WOLFRAM_BSKY_AGENT_H

#include "wolfram/agent.h"
#include "wolfram/feed_typed.h"
#include "wolfram/thread_typed.h"
#include "wolfram/actor_typed.h"
#include "wolfram/session.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The high-level agent. Holds a single wf_agent by pointer (see the
 * composition note above); all session/xrpc/identity state lives in that
 * pointed-to agent.
 */
typedef struct wf_bsky_agent {
    wf_agent *agent;
} wf_bsky_agent;

/* Zero a bsky agent. No allocation; safe to call on a stack-local struct. */
void wf_bsky_agent_init(wf_bsky_agent *b);

/*
 * Release the underlying wf_agent (and its session/client) and zero the
 * struct. Safe to call with a zeroed/b->agent==NULL struct, and safe to call
 * more than once. Does not perform a network logout; call
 * wf_bsky_agent_logout first if the server session must be invalidated.
 */
void wf_bsky_agent_free(wf_bsky_agent *b);

/*
 * Log in by creating (or reusing) the underlying agent bound to `service`
 * (a PDS base URL, e.g. "https://bsky.social") and authenticating with
 * `identifier` (handle or email) and `password`. Forwards to wf_agent_login.
 * Returns WF_ERR_INVALID_ARG when b/service/identifier/password are NULL;
 * WF_ERR_ALLOC if the underlying agent cannot be allocated.
 */
wf_status wf_bsky_agent_login(wf_bsky_agent *b, const char *service,
                              const char *identifier, const char *password);

/*
 * Resume a previously persisted session. The underlying agent is created with
 * the default Bluesky PDS ("https://bsky.social") when it does not already
 * exist; callers on a different PDS should log in once first so the agent is
 * bound to the correct service. Forwards to wf_agent_resume.
 * Returns WF_ERR_INVALID_ARG when b or data is NULL.
 */
wf_status wf_bsky_agent_login_session(wf_bsky_agent *b,
                                      const wf_session_data *data);

/* Log out, invalidating the server session. Forwards to wf_agent_logout. */
wf_status wf_bsky_agent_logout(wf_bsky_agent *b);

/* Post `text` as the authenticated user. Forwards to wf_agent_post.
 * NULL b or text or out -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_post(wf_bsky_agent *b, const char *text,
                             wf_agent_post_result *out);

/* Fetch a profile by handle or DID. Forwards to wf_agent_get_profile.
 * NULL b or handle_or_did or out -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_get_profile(wf_bsky_agent *b,
                                    const char *handle_or_did,
                                    wf_agent_profile *out);

/* Fetch the authenticated user's timeline (typed). Forwards to
 * wf_agent_get_timeline_typed. NULL b or out -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_get_timeline(wf_bsky_agent *b, int limit,
                                     const char *cursor,
                                     wf_agent_feed_list *out);

/*
 * Resolve a handle to a DID. On WF_OK, *out_did is a heap-allocated string the
 * caller must free() (exactly as wf_agent_resolve_handle does). Forwards to
 * wf_agent_resolve_handle. NULL b or handle or out_did -> WF_ERR_INVALID_ARG.
 */
wf_status wf_bsky_agent_resolve_handle(wf_bsky_agent *b, const char *handle,
                                       char **out_did);

/*
 * Follow `target_handle` (a handle or a DID). Resolves the handle to a DID
 * when necessary, then forwards to wf_agent_follow. On WF_OK, *out receives the
 * created follow record uri/cid (free with wf_agent_post_result_free).
 * NULL b or target_handle or out -> WF_ERR_INVALID_ARG.
 */
wf_status wf_bsky_agent_follow(wf_bsky_agent *b, const char *target_handle,
                               wf_agent_post_result *out);

/* Unfollow by deleting the follow record at `follow_uri`. Forwards to
 * wf_agent_unfollow. NULL b or follow_uri -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_unfollow(wf_bsky_agent *b, const char *follow_uri);

/* Like a post. Forwards to wf_agent_like. NULL b or post_uri or post_cid or out
 * -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_like(wf_bsky_agent *b, const char *post_uri,
                             const char *post_cid, wf_agent_post_result *out);

/* Repost a post. Forwards to wf_agent_repost. NULL b or post_uri or post_cid or
 * out -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_repost(wf_bsky_agent *b, const char *post_uri,
                               const char *post_cid, wf_agent_post_result *out);

/* Mute an actor (handle or DID). Forwards to wf_agent_mute.
 * NULL b or handle_or_did -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_mute(wf_bsky_agent *b, const char *handle_or_did);

/* Unmute an actor (handle or DID). Forwards to wf_agent_unmute.
 * NULL b or handle_or_did -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_unmute(wf_bsky_agent *b, const char *handle_or_did);

/* Fetch notifications (typed). Forwards to wf_agent_list_notifications_typed.
 * NULL b or out -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_get_notifications(wf_bsky_agent *b, int limit,
                                          const char *cursor,
                                          wf_agent_notification_list *out);

/* Search actors (typed). Forwards to wf_agent_search_actors_typed.
 * NULL b or query or out -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_search_actors(wf_bsky_agent *b, const char *query,
                                      int limit, const char *cursor,
                                      wf_agent_actor_list *out);

/* Fetch a post thread (typed). Forwards to wf_agent_get_post_thread_typed.
 * NULL b or uri or out -> WF_ERR_INVALID_ARG. */
wf_status wf_bsky_agent_get_thread(wf_bsky_agent *b, const char *uri, int depth,
                                   wf_agent_thread *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_BSKY_AGENT_H */
