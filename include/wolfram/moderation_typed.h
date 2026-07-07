/*
 * moderation_typed.h — typed wrappers for moderation/social-graph list
 * endpoints that return an array of profileView plus a cursor.
 *
 * These wrap the raw agent calls for `app.bsky.graph.getBlocks`,
 * `app.bsky.graph.getMutes`, and `app.bsky.graph.getKnownFollowers`, parsing
 * the response body into an owned `wf_agent_actor_list` via the shared
 * `wf_agent_parse_profile_views` parser (declared in graph_typed.h). Callers
 * free the result with `wf_agent_actor_list_free`.
 */

#ifndef WOLFRAM_MODERATION_TYPED_H
#define WOLFRAM_MODERATION_TYPED_H

#include "wolfram/graph_typed.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration: wf_agent_actor_list is fully defined in graph_typed.h
 * and used here only as a pointer parameter (these wrappers reuse the shared
 * wf_agent_parse_profile_views parser). */
typedef struct wf_agent_actor_list wf_agent_actor_list;

/* Issue app.bsky.graph.getBlocks and parse the `blocks` profileView array. */
wf_status wf_agent_get_blocks_typed(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_agent_actor_list *out);

/* Issue app.bsky.graph.getMutes and parse the `mutes` profileView array. */
wf_status wf_agent_get_mutes_typed(wf_agent *agent, int limit,
                                   const char *cursor,
                                   wf_agent_actor_list *out);

/* Issue app.bsky.graph.getKnownFollowers and parse the `followers` array. */
wf_status wf_agent_get_known_followers_typed(wf_agent *agent, const char *actor,
                                             int limit, const char *cursor,
                                             wf_agent_actor_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MODERATION_TYPED_H */
