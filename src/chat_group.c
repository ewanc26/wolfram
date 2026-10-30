/*
 * chat_group.c — group-conversation helpers declared in chat_group.h.
 *
 * No I/O and no allocation: a client calls these after wf_agent_parse_convo()
 * to decide whether a conversation is a group and to show its join link.
 */

#include "wolfram/chat_group.h"

#include <stdio.h>
#include <string.h>

bool wf_chat_convo_is_group(const wf_chat_convo *convo) {
    const char *tag;
    if (!convo) return false;
    if (convo->group_name || convo->has_group_member_count ||
        convo->has_group_member_limit || convo->has_join_request_count ||
        convo->has_join_link) {
        return true;
    }
    if (!convo->type) return false;
    tag = strrchr(convo->type, '#');
    tag = tag ? tag + 1 : convo->type;
    return strcmp(tag, "groupConvo") == 0 || strcmp(tag, "group") == 0;
}

bool wf_chat_join_link_url(const char *code, char *out, size_t cap) {
    int n;
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!code || !code[0]) return false;
    n = snprintf(out, cap, "https://bsky.app/chat/%s", code);
    if (n <= 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool wf_chat_join_link_is_enabled(const char *enabled_status) {
    return enabled_status && strcmp(enabled_status, "enabled") == 0;
}
