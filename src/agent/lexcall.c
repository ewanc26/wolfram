/**
 * lexcall.c — registry of generated lexicon output decoders.
 *
 * Each entry binds an NSID to the generated owning decoder/free produced by
 * the lexicon generator in `atproto_lex.h`. Add an NSID here once both its
 * `_main_output_decode_json` and `_main_output_free` functions are present.
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/atproto_lex.h"
#include "wolfram/lexcall.h"

/* --- decode shims ------------------------------------------------------- */

static wf_status dec_unread(const char *json, size_t length, void **out)
{
    return wf_lex_app_bsky_notification_get_unread_count_main_output_decode_json(
        json, length,
        (wf_lex_app_bsky_notification_get_unread_count_main_output **)out);
}

static void free_unread(void *out)
{
    wf_lex_app_bsky_notification_get_unread_count_main_output_free(
        (wf_lex_app_bsky_notification_get_unread_count_main_output *)out);
}

static wf_status dec_prefs(const char *json, size_t length, void **out)
{
    return wf_lex_app_bsky_notification_get_preferences_main_output_decode_json(
        json, length,
        (wf_lex_app_bsky_notification_get_preferences_main_output **)out);
}

static void free_prefs(void *out)
{
    wf_lex_app_bsky_notification_get_preferences_main_output_free(
        (wf_lex_app_bsky_notification_get_preferences_main_output *)out);
}

static wf_status dec_describe_repo(const char *json, size_t length, void **out)
{
    return wf_lex_com_atproto_repo_describe_repo_main_output_decode_json(
        json, length,
        (wf_lex_com_atproto_repo_describe_repo_main_output **)out);
}

static void free_describe_repo(void *out)
{
    wf_lex_com_atproto_repo_describe_repo_main_output_free(
        (wf_lex_com_atproto_repo_describe_repo_main_output *)out);
}

static wf_status dec_get_head(const char *json, size_t length, void **out)
{
    return wf_lex_com_atproto_sync_get_head_main_output_decode_json(
        json, length,
        (wf_lex_com_atproto_sync_get_head_main_output **)out);
}

static void free_get_head(void *out)
{
    wf_lex_com_atproto_sync_get_head_main_output_free(
        (wf_lex_com_atproto_sync_get_head_main_output *)out);
}

/* --- registry ----------------------------------------------------------- */

static const wf_lex_entry LEX_ENTRIES[] = {
    {
        "app.bsky.notification.getUnreadCount",
        dec_unread,
        free_unread,
        sizeof(wf_lex_app_bsky_notification_get_unread_count_main_output),
    },
    {
        "app.bsky.notification.getPreferences",
        dec_prefs,
        free_prefs,
        sizeof(wf_lex_app_bsky_notification_get_preferences_main_output),
    },
    {
        "com.atproto.repo.describeRepo",
        dec_describe_repo,
        free_describe_repo,
        sizeof(wf_lex_com_atproto_repo_describe_repo_main_output),
    },
    {
        "com.atproto.sync.getHead",
        dec_get_head,
        free_get_head,
        sizeof(wf_lex_com_atproto_sync_get_head_main_output),
    },
};

static const wf_lex_entry *lookup(const char *nsid)
{
    if (!nsid)
        return NULL;
    for (size_t i = 0; i < sizeof(LEX_ENTRIES) / sizeof(LEX_ENTRIES[0]); i++) {
        if (strcmp(LEX_ENTRIES[i].nsid, nsid) == 0)
            return &LEX_ENTRIES[i];
    }
    return NULL;
}

/* --- public API --------------------------------------------------------- */

wf_status wf_lex_decode_output(const char *nsid, const char *json,
                               size_t json_len, void **out)
{
    if (!nsid || !json || !out)
        return WF_ERR_INVALID_ARG;

    *out = NULL;

    const wf_lex_entry *e = lookup(nsid);
    if (!e)
        return WF_ERR_NOT_FOUND;

    wf_status st = e->decode(json, json_len, out);
    if (st != WF_OK) {
        if (*out)
            e->free(*out);
        *out = NULL;
    }
    return st;
}

wf_status wf_lex_call_output(const char *nsid, const char *json,
                             size_t json_len, void **out)
{
    return wf_lex_decode_output(nsid, json, json_len, out);
}

void wf_lex_output_free(const char *nsid, void *out)
{
    if (!out)
        return;
    const wf_lex_entry *e = lookup(nsid);
    if (e)
        e->free(out);
}
