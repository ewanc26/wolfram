/*
 * moderation_typed.c — typed wrappers for moderation/social-graph list
 * endpoints returning a profileView array plus a cursor.
 *
 * Each wrapper validates its arguments, calls the matching raw agent method
 * (which returns raw JSON in a wf_response), then hands the body to the shared
 * `wf_agent_parse_profile_views` parser under the endpoint-specific key
 * ("blocks", "mutes", "followers"). Implemented following the conventions of
 * graph_typed.c.
 */

#include "wolfram/moderation_typed.h"
#include "wolfram/agent.h"

#include <stdlib.h>

wf_status wf_agent_get_blocks_typed(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_agent_actor_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_blocks(agent, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_profile_views(res.body, res.body_len, "blocks",
                                          out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_mutes_typed(wf_agent *agent, int limit,
                                   const char *cursor,
                                   wf_agent_actor_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_mutes(agent, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_profile_views(res.body, res.body_len, "mutes",
                                          out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_known_followers_typed(wf_agent *agent, const char *actor,
                                             int limit, const char *cursor,
                                             wf_agent_actor_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_known_followers(agent, actor, limit,
                                                    cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_profile_views(res.body, res.body_len, "followers",
                                          out);
    wf_response_free(&res);
    return status;
}
