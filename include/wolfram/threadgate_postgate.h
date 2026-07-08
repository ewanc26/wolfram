#ifndef WOLFRAM_THREADGATE_POSTGATE_H
#define WOLFRAM_THREADGATE_POSTGATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wolfram/agent.h"

/* Create a threadgate record (reply controls) on a post.
 *
 * post_uri   — AT-URI of the post to gate.
 * allow_json — JSON array of allow-rule objects, or NULL to omit (anyone can
 *              reply). Pass "[]" for an empty array (no one can reply).
 *              Example: '[{"$type":"app.bsky.feed.threadgate#mentionRule"}]'
 * hidden_replies — array of AT-URI strings to hide, or NULL.
 * hidden_count   — number of hidden-reply entries.
 * out        — receives the created record's URI and CID.
 */
wf_status wf_agent_create_threadgate(wf_agent *agent,
                                     const char *post_uri,
                                     const char *allow_json,
                                     const char **hidden_replies,
                                     size_t hidden_count,
                                     wf_agent_post_result *out);

/* Create a postgate record (embedding rules) on a post.
 *
 * post_uri             — AT-URI of the post to gate.
 * embedding_rules_json — JSON array of embed-rule objects, or NULL to omit.
 *                        Example: '[{"$type":"app.bsky.feed.postgate#disableRule"}]'
 * detached_uris        — array of AT-URI strings to detach, or NULL.
 * detached_count       — number of detached-URI entries.
 * out                  — receives the created record's URI and CID.
 */
wf_status wf_agent_create_postgate(wf_agent *agent,
                                   const char *post_uri,
                                   const char *embedding_rules_json,
                                   const char **detached_uris,
                                   size_t detached_count,
                                   wf_agent_post_result *out);

/* Delete any record by its AT-URI (delegates to com.atproto.repo.deleteRecord).
 * The record must belong to the currently logged-in user's repository. */
wf_status wf_agent_delete_record_by_uri(wf_agent *agent,
                                         const char *record_uri);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_THREADGATE_POSTGATE_H */
