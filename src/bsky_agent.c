/*
 * bsky_agent.c — implementation of the high-level wf_bsky_agent wrapper.
 *
 * This layer is a thin convenience facade: every method validates its own
 * arguments (b non-NULL, required pointers non-NULL) and then forwards to the
 * matching wf_agent_* call. No network I/O happens here — that stays inside the
 * agent/transport modules, per the project's transport-first rule.
 */

#include "wolfram/bsky_agent.h"

#include "wolfram/syntax.h"

#include <stdlib.h>
#include <string.h>

/* Default PDS used when resuming a session before any explicit login has bound
 * the underlying agent to a service (see wf_bsky_agent_login_session). */
#define WF_BSKY_AGENT_DEFAULT_SERVICE "https://bsky.social"

void wf_bsky_agent_init(wf_bsky_agent *b) {
    if (!b) {
        return;
    }
    memset(b, 0, sizeof(*b));
}

void wf_bsky_agent_free(wf_bsky_agent *b) {
    if (!b) {
        return;
    }
    if (b->agent) {
        wf_agent_free(b->agent);
        b->agent = NULL;
    }
}

wf_status wf_bsky_agent_login(wf_bsky_agent *b, const char *service,
                              const char *identifier, const char *password) {
    if (!b || !service || !identifier || !password) {
        return WF_ERR_INVALID_ARG;
    }

    if (!b->agent) {
        b->agent = wf_agent_new(service);
        if (!b->agent) {
            return WF_ERR_ALLOC;
        }
    }

    return wf_agent_login(b->agent, identifier, password);
}

wf_status wf_bsky_agent_login_session(wf_bsky_agent *b,
                                      const wf_session_data *data) {
    if (!b || !data) {
        return WF_ERR_INVALID_ARG;
    }

    if (!b->agent) {
        b->agent = wf_agent_new(WF_BSKY_AGENT_DEFAULT_SERVICE);
        if (!b->agent) {
            return WF_ERR_ALLOC;
        }
    }

    return wf_agent_resume(b->agent, data);
}

wf_status wf_bsky_agent_logout(wf_bsky_agent *b) {
    if (!b) {
        return WF_ERR_INVALID_ARG;
    }
    if (!b->agent) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_logout(b->agent);
}

wf_status wf_bsky_agent_post(wf_bsky_agent *b, const char *text,
                             wf_agent_post_result *out) {
    if (!b || !text || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_post(b->agent, text, out);
}

wf_status wf_bsky_agent_get_profile(wf_bsky_agent *b,
                                    const char *handle_or_did,
                                    wf_agent_profile *out) {
    if (!b || !handle_or_did || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_get_profile(b->agent, handle_or_did, out);
}

wf_status wf_bsky_agent_get_timeline(wf_bsky_agent *b, int limit,
                                     const char *cursor,
                                     wf_agent_feed_list *out) {
    if (!b || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_get_timeline_typed(b->agent, limit, cursor, out);
}

wf_status wf_bsky_agent_resolve_handle(wf_bsky_agent *b, const char *handle,
                                       char **out_did) {
    if (!b || !handle || !out_did) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_resolve_handle(b->agent, handle, out_did);
}

wf_status wf_bsky_agent_follow(wf_bsky_agent *b, const char *target_handle,
                               wf_agent_post_result *out) {
    if (!b || !target_handle || !out) {
        return WF_ERR_INVALID_ARG;
    }

    /* A DID needs no resolution; a handle does. */
    if (wf_syntax_did_is_valid(target_handle)) {
        return wf_agent_follow(b->agent, target_handle, out);
    }

    char *did = NULL;
    wf_status status = wf_agent_resolve_handle(b->agent, target_handle, &did);
    if (status != WF_OK) {
        return status;
    }

    status = wf_agent_follow(b->agent, did, out);
    free(did);
    return status;
}

wf_status wf_bsky_agent_unfollow(wf_bsky_agent *b, const char *follow_uri) {
    if (!b || !follow_uri) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_unfollow(b->agent, follow_uri);
}

wf_status wf_bsky_agent_like(wf_bsky_agent *b, const char *post_uri,
                             const char *post_cid, wf_agent_post_result *out) {
    if (!b || !post_uri || !post_cid || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_like(b->agent, post_uri, post_cid, out);
}

wf_status wf_bsky_agent_repost(wf_bsky_agent *b, const char *post_uri,
                               const char *post_cid, wf_agent_post_result *out) {
    if (!b || !post_uri || !post_cid || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_repost(b->agent, post_uri, post_cid, out);
}

wf_status wf_bsky_agent_mute(wf_bsky_agent *b, const char *handle_or_did) {
    if (!b || !handle_or_did) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_mute(b->agent, handle_or_did);
}

wf_status wf_bsky_agent_unmute(wf_bsky_agent *b, const char *handle_or_did) {
    if (!b || !handle_or_did) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_unmute(b->agent, handle_or_did);
}

wf_status wf_bsky_agent_get_notifications(wf_bsky_agent *b, int limit,
                                          const char *cursor,
                                          wf_agent_notification_list *out) {
    if (!b || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_list_notifications_typed(b->agent, limit, cursor, out);
}

wf_status wf_bsky_agent_search_actors(wf_bsky_agent *b, const char *query,
                                      int limit, const char *cursor,
                                      wf_agent_actor_list *out) {
    if (!b || !query || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_search_actors_typed(b->agent, query, limit, cursor, out);
}

wf_status wf_bsky_agent_get_thread(wf_bsky_agent *b, const char *uri, int depth,
                                   wf_agent_thread *out) {
    if (!b || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_get_post_thread_typed(b->agent, uri, depth, out);
}
