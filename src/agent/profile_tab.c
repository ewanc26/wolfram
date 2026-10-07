#include "wolfram/profile_tab.h"

const char *wf_profile_tab_name(int tab) {
    switch (tab) {
        case WF_PROFILE_TAB_REPLIES:
            return "Replies";
        case WF_PROFILE_TAB_MEDIA:
            return "Media";
        case WF_PROFILE_TAB_LIKES:
            return "Likes";
        default:
            return "Posts";
    }
}

const char *wf_profile_tab_filter(int tab) {
    switch (tab) {
        case WF_PROFILE_TAB_REPLIES:
            return "posts_with_replies";
        case WF_PROFILE_TAB_MEDIA:
            return "posts_with_media";
        case WF_PROFILE_TAB_LIKES:
            return NULL;
        default:
            return "posts_and_author_threads";
    }
}

int wf_profile_tab_next(int tab, int is_self) {
    int count = is_self ? WF_PROFILE_TAB_COUNT : WF_PROFILE_TAB_COUNT - 1;
    if (tab < 0 || tab >= count) return WF_PROFILE_TAB_POSTS;
    return (tab + 1) % count;
}

wf_status wf_agent_get_profile_tab_typed(wf_agent *agent, const char *actor,
                                         int tab, int limit, const char *cursor,
                                         wf_agent_feed_list *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (tab == WF_PROFILE_TAB_LIKES)
        return wf_agent_get_actor_likes_typed(agent, actor, limit, cursor, out);
    return wf_agent_get_author_feed_typed(agent, actor, limit, cursor,
                                          wf_profile_tab_filter(tab), out);
}
