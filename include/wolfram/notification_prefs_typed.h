/*
 * notification_prefs_typed.h — owned "typed" wrappers for
 * app.bsky.notification.putPreferences / getPreferences (per-user
 * notification settings).
 *
 * Wire format (authoritative, app.bsky.notification):
 *   putPreferences input : { priority: boolean }
 *   getPreferences output: { preferences: defs#preferences }
 *
 * `defs#preferences` is the same 13-slot object returned by
 * putPreferencesV2, so this module reuses its typed representation.
 */

#ifndef WOLFRAM_NOTIFICATION_PREFS_TYPED_H
#define WOLFRAM_NOTIFICATION_PREFS_TYPED_H

#include "wolfram/agent.h"
#include "wolfram/notification_v2_typed.h"

#include <cJSON.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Owned per-user notification preferences (`defs#preferences`). */
typedef wf_notification_v2_preferences wf_notification_prefs;

/*
 * Parse a getPreferences output body (`{ preferences: {...} }`) or a bare
 * `defs#preferences` object into an owned wf_notification_prefs. On error
 * `out` is left zeroed.
 */
wf_status wf_notification_prefs_parse(const char *json, size_t len,
                                      wf_notification_prefs *out);

/* Release all owned state in `p`. Safe to call on a zeroed struct. */
void wf_notification_prefs_free(wf_notification_prefs *p);

/*
 * Fetch the authenticated account's notification preferences and parse them.
 * Returns WF_ERR_INVALID_ARG if `agent` or `out` is NULL.
 */
wf_status wf_agent_get_notification_prefs(wf_agent *agent,
                                          wf_notification_prefs *out);

/*
 * Update the authenticated account's notification preferences.
 *
 * `priority` is the global priority toggle. The upstream v1 lexicon has no
 * `priorities` property; `priorities_json` is retained for source compatibility
 * and must be NULL. A non-NULL value returns WF_ERR_INVALID_ARG rather than
 * silently discarding caller input. Prefer wf_agent_put_notification_priority.
 */
wf_status wf_agent_put_notification_prefs(wf_agent *agent, int priority,
                                          const char *priorities_json);

/* Update the v1 global priority toggle using the exact `{priority}` schema. */
wf_status wf_agent_put_notification_priority(wf_agent *agent, int priority);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_NOTIFICATION_PREFS_TYPED_H */
