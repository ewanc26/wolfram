/*
 * graph_typed.h — typed (owned-struct) parser for actor-list responses.
 *
 * Parses the raw JSON returned by `app.bsky.graph.getFollows`,
 * `app.bsky.graph.getFollowers`, `app.bsky.actor.getProfiles`, and
 * `app.bsky.actor.searchActors` (all of which return an `actors` array of
 * `app.bsky.actor.defs#profileView` plus an optional `cursor`) into owned C
 * structs. Free the whole list with `wf_agent_actor_list_free`.
 */

#ifndef WOLFRAM_GRAPH_TYPED_H
#define WOLFRAM_GRAPH_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A parsed list of actor profile views plus an optional cursor. */
typedef struct wf_agent_actor_list {
    wf_agent_profile_view *actors;
    size_t actor_count;
    char *cursor;
} wf_agent_actor_list;

/* Parse an actor-list JSON body of `json_len` bytes into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing/invalid `actors` array, WF_ERR_ALLOC on allocation failure, WF_OK
 * on success. On any error `out` is left fully reset. */
wf_status wf_agent_parse_actors(const char *json, size_t json_len,
                                wf_agent_actor_list *out);

/* Free a parsed actor list and every owned field it holds. */
void wf_agent_actor_list_free(wf_agent_actor_list *list);

/* Typed high-level wrappers — issue the corresponding agent call and parse the
 * JSON body into `out`. On success `out` is owned by the caller (free with
 * `wf_agent_actor_list_free`); on error it is left reset. */
wf_status wf_agent_get_follows_typed(wf_agent *agent, const char *actor,
                                     int limit, const char *cursor,
                                     wf_agent_actor_list *out);
wf_status wf_agent_get_followers_typed(wf_agent *agent, const char *actor,
                                       int limit, const char *cursor,
                                       wf_agent_actor_list *out);
wf_status wf_agent_search_actors_typed(wf_agent *agent, const char *query,
                                       int limit, const char *cursor,
                                       wf_agent_actor_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_GRAPH_TYPED_H */
