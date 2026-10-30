/*
 * chat_group.h — group-conversation helpers shared by the clients.
 *
 * These read a parsed `wf_chat_convo` (chat_typed.h) and turn its group fields
 * into the values a client draws. They do no I/O and take no ownership, so a
 * client does not re-derive the chat.bsky.group rules itself.
 */

#ifndef WOLFRAM_CHAT_GROUP_H
#define WOLFRAM_CHAT_GROUP_H

#include "wolfram/chat_typed.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* chat.bsky.group.defs#joinRule known values. Pass one to
 * wf_agent_chat_create_join_link_typed / wf_agent_chat_edit_join_link_typed. */
#define WF_CHAT_JOIN_RULE_ANYONE "anyone"
#define WF_CHAT_JOIN_RULE_FOLLOWED_BY_OWNER "followedByOwner"

/* True when the convo is a group conversation: its `kind` carried group
 * fields, or its type is chat.bsky.convo.defs#groupConvo. A direct convo and a
 * NULL pointer are false. This reads the parsed `kind`, not the member count,
 * so a group with one other member is still a group. */
bool wf_chat_convo_is_group(const wf_chat_convo *convo);

/* The public web address of a group's join link, https://bsky.app/chat/<code>.
 * Writes "" and returns false when `code` is empty or the result does not fit
 * `cap`; `out` may not be NULL. */
bool wf_chat_join_link_url(const char *code, char *out, size_t cap);

/* True when a joinLinkView's enabledStatus is "enabled". Accepts the string
 * from wf_chat_join_link.enabled_status or wf_chat_convo.join_link_status;
 * NULL, "disabled" and "invalid" are false. */
bool wf_chat_join_link_is_enabled(const char *enabled_status);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CHAT_GROUP_H */
