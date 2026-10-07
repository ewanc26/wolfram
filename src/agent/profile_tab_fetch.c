#include "wolfram/profile_tab.h"

wf_status wf_agent_get_profile_tab_typed(wf_agent *agent, const char *actor,
                                         int tab, int limit, const char *cursor,
                                         wf_agent_feed_list *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (tab == WF_PROFILE_TAB_LIKES)
        return wf_agent_get_actor_likes_typed(agent, actor, limit, cursor, out);
    return wf_agent_get_author_feed_typed(agent, actor, limit, cursor,
                                          wf_profile_tab_filter(tab), out);
}
