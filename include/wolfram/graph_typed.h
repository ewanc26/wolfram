/*
 * graph_typed.h — typed (owned-struct) parsers for social-graph list
 * responses (follows / followers / blocks / mutes) plus the shared
 * like-list parser. Actor-profile parsing and the actor/search/suggestions
 * typed wrappers live in actor_typed.h (which includes this header) to avoid
 * duplicating the profileView ownership logic. Free actor lists with
 * `wf_agent_actor_list_free` (from actor_typed.h); free like lists with
 * `wf_agent_like_list_free` (below).
 *
 * NOTE: wf_agent_actor_list is defined here (not in actor_typed.h) because
 * agent.h transitively includes this header; actor_typed.h in turn includes
 * graph_typed.h to obtain the type. Keeping the type here breaks the include
 * cycle and guarantees it is available wherever the header is pulled in.
 */

#ifndef WOLFRAM_GRAPH_TYPED_H
#define WOLFRAM_GRAPH_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A parsed list of actor profile views plus an optional cursor. Defined here
 * (see note above) and reused by actor_typed.h for getProfiles / searchActors /
 * getRepostedBy / follows / followers. */
typedef struct wf_agent_actor_list {
    wf_agent_profile_view *actors;
    size_t actor_count;
    char *cursor;
} wf_agent_actor_list;

/* A parsed "like" record (app.bsky.feed.getLikes). */
typedef struct wf_agent_like_item {
    wf_agent_profile_view actor;
    char *created_at;
    char *indexed_at;
} wf_agent_like_item;

/* A parsed like list (app.bsky.feed.getLikes). */
typedef struct wf_agent_like_list {
    wf_agent_like_item *likes;
    size_t like_count;
    char *cursor;
} wf_agent_like_list;

/* Parse a `likes` array of {actor, createdAt, indexedAt} into owned structs. */
wf_status wf_agent_parse_likes(const char *json, size_t json_len,
                               wf_agent_like_list *out);

/* Free a parsed like list and every owned field it holds. */
void wf_agent_like_list_free(wf_agent_like_list *list);

/* A single bi-directional relationship (app.bsky.graph.getRelationships).
 * Each at-uri field is NULL when the relationship is absent. */
typedef struct wf_agent_relationship {
    char *did;               /* target actor DID (required) */
    char *following;         /* at-uri of follow record, or NULL */
    char *followed_by;       /* at-uri, or NULL */
    char *blocking;          /* at-uri of block record, or NULL */
    char *blocked_by;        /* at-uri, or NULL */
    char *blocking_by_list;  /* at-uri of listblock, or NULL */
    char *blocked_by_list;   /* at-uri of listblock, or NULL */
} wf_agent_relationship;

/* A parsed getRelationships response. */
typedef struct wf_agent_relationship_list {
    char *actor;
    wf_agent_relationship *rels;
    size_t rel_count;
} wf_agent_relationship_list;

/* Parse a getRelationships JSON body into owned structs. Returns
 * WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing `relationships` array, WF_ERR_ALLOC on allocation failure. */
wf_status wf_agent_parse_relationships(const char *json, size_t json_len,
                                       wf_agent_relationship_list *out);

/* Free a parsed relationship list and every owned field it holds. */
void wf_agent_relationship_list_free(wf_agent_relationship_list *list);

/* Typed high-level wrappers for graph follow relationships. On success `out`
 * is owned by the caller (free with wf_agent_actor_list_free); on error it is
 * left reset. */
wf_status wf_agent_get_follows_typed(wf_agent *agent, const char *actor,
                                      int limit, const char *cursor,
                                      wf_agent_actor_list *out);
wf_status wf_agent_get_followers_typed(wf_agent *agent, const char *actor,
                                        int limit, const char *cursor,
                                        wf_agent_actor_list *out);

/* Typed high-level wrappers for graph relationships and thread muting. On
 * success `out` is owned by the caller (free with wf_agent_relationship_list_free);
 * on error it is left reset. The mute/unmute procedures return only a wf_status
 * (the thread root URI is validated). */
wf_status wf_agent_get_relationships_typed(wf_agent *agent, const char *actor,
                                            const char *const *others,
                                            size_t others_count,
                                            wf_agent_relationship_list *out);
wf_status wf_agent_mute_thread_typed(wf_agent *agent, const char *root_uri);
wf_status wf_agent_unmute_thread_typed(wf_agent *agent, const char *root_uri);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_GRAPH_TYPED_H */
