/*
 * profile_tab.h -- the tabs of a profile and what each one fetches.
 *
 * Posts, Replies and Media are getAuthorFeed filters; Likes is its own
 * endpoint, which the server only answers for the viewer's own account. Every
 * client with profile tabs needs the same names, the same cycle order and the
 * same filter strings, so they are here once.
 */

#ifndef WOLFRAM_PROFILE_TAB_H
#define WOLFRAM_PROFILE_TAB_H

#include "wolfram/feed_typed.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_profile_tab {
    WF_PROFILE_TAB_POSTS = 0, /* the person's posts and their own threads */
    WF_PROFILE_TAB_REPLIES,   /* posts with replies */
    WF_PROFILE_TAB_MEDIA,     /* posts with media */
    WF_PROFILE_TAB_LIKES,     /* own account only */
    WF_PROFILE_TAB_COUNT
} wf_profile_tab;

/* "Posts", "Replies", "Media" or "Likes"; anything else reads as "Posts". */
const char *wf_profile_tab_name(int tab);

/* The getAuthorFeed filter for a tab, or NULL for Likes (its own endpoint).
 * An out-of-range tab is Posts. */
const char *wf_profile_tab_filter(int tab);

/* The tab after `tab`, wrapping, skipping Likes unless `is_self`. An
 * out-of-range tab (or one the account cannot see) goes to Posts. */
int wf_profile_tab_next(int tab, int is_self);

/* One page of `tab` for `actor`: the author feed with the tab's filter, or the
 * actor's likes. Same list and ownership as wf_agent_get_author_feed_typed.
 * WF_ERR_INVALID_ARG for a NULL argument. */
wf_status wf_agent_get_profile_tab_typed(wf_agent *agent, const char *actor,
                                         int tab, int limit, const char *cursor,
                                         wf_agent_feed_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_PROFILE_TAB_H */
