/*
 * saved_feeds.h -- the account's saved custom feeds, with names.
 *
 * Every client that offers a feed picker needs the same thing: the feeds the
 * account has saved, in order, each with something to show. It is read from the
 * raw preferences rather than the strict typed parse, because one preference
 * type the parser rejects must not take the whole picker down with it (a real
 * account returned status 5 from the typed call). Cobalt and Indigo each wrote
 * this out; it is here once.
 */

#ifndef WOLFRAM_SAVED_FEEDS_H
#define WOLFRAM_SAVED_FEEDS_H

#include "wolfram/agent.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_SAVED_FEED_URI_MAX 256
#define WF_SAVED_FEED_NAME_MAX 128

typedef struct wf_saved_feed {
    char uri[WF_SAVED_FEED_URI_MAX];   /* the feed generator's AT-URI */
    char name[WF_SAVED_FEED_NAME_MAX]; /* display name, or the record key */
} wf_saved_feed;

/*
 * Read the saved feed URIs out of a getPreferences `preferences` array, in
 * order: the items of savedFeedsPrefV2 whose type is "feed" (timelines and
 * lists are not custom feeds), or, for an older account with none, the `saved`
 * list of savedFeedsPref. Empty values and anything beyond `cap` are skipped.
 * Each name is set to the URI's record key for now. Stores the count in
 * *count. WF_ERR_INVALID_ARG for a NULL argument, WF_ERR_PARSE when the text is
 * not a JSON array.
 */
wf_status wf_saved_feeds_parse(const char *preferences_json, wf_saved_feed *out,
                               size_t cap, size_t *count);

/*
 * The account's saved feeds with their display names: getPreferences, then
 * wf_saved_feeds_parse, then one getFeedGenerators call for the names. If that
 * last call fails the feeds are still returned, named by their record keys,
 * because the URIs alone make a usable picker. Errors from getPreferences
 * are returned as they are and *count is 0.
 */
wf_status wf_agent_get_saved_feeds(wf_agent *agent, wf_saved_feed *out,
                                   size_t cap, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SAVED_FEEDS_H */
