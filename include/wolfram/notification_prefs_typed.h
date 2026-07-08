/*
 * notification_prefs_typed.h — owned "typed" wrappers for
 * app.bsky.notification.putPreferences / getPreferences (per-user
 * notification settings).
 *
 * Wire format (authoritative, app.bsky.notification):
 *   putPreferences input : { priority?: boolean,
 *                            priorities?: { [key: string]: boolean } }
 *   getPreferences output: { priority: boolean,
 *                            priorities?: { [key: string]: boolean } }
 *
 * Every owning output has a matching `_free`. `priorities` is an owned cJSON
 * object (string keys -> bool values), detached from the parsed document and
 * released by `wf_notification_prefs_free`.
 */

#ifndef WOLFRAM_NOTIFICATION_PREFS_TYPED_H
#define WOLFRAM_NOTIFICATION_PREFS_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Owned per-user notification preferences. */
typedef struct wf_notification_prefs {
    int    priority;     /* boolean value when has_priority != 0 */
    int    has_priority; /* 1 if `priority` was present in the source JSON */
    cJSON *priorities;   /* owned map of string -> bool; NULL if absent */
} wf_notification_prefs;

/*
 * Parse a getPreferences output body (`{ priority?, priorities? }`) into an
 * owned wf_notification_prefs. On any error `out` is left zeroed and any
 * partially-detached `priorities` is released.
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
 * `priority` is the global priority toggle. `priorities_json` is an optional
 * JSON object literal such as `{"like": true}` (or NULL). A non-NULL
 * `priorities_json` that is not a valid JSON object yields WF_ERR_INVALID_ARG.
 * `agent` must be non-NULL (WF_ERR_INVALID_ARG otherwise).
 */
wf_status wf_agent_put_notification_prefs(wf_agent *agent, int priority,
                                          const char *priorities_json);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_NOTIFICATION_PREFS_TYPED_H */
