/*
 * main.c — the `wolfram` command-line client.
 *
 * A thin demonstration over the wolfram SDK that exercises the high-level
 * agent API (and a little raw XRPC) end to end. Every subcommand is
 * network-gated: when its required arguments are missing the program prints
 * usage and exits 0 without performing any network I/O.
 *
 * Conventions follow the rest of the SDK: wf_ prefixes, wf_status returns,
 * and cJSON for JSON handling. All allocated resources (sessions, agents,
 * responses) are freed before exit.
 *
 * Usage:
 *   wolfram login       <service> <handle> <password>
 *   wolfram post        <service> <handle> <password> <text...>
 *   wolfram reply       <service> <handle> <password> <parent-at-uri> <text...>
 *   wolfram timeline    <service> <handle> <password> [pages]
 *   wolfram get-post    <service> <at-uri>
 *   wolfram get-record  <service> <handle> <password> <collection> <rkey>
 *   wolfram profile     <service> <actor>
 *   wolfram follow      <service> <handle> <password> <actor>
 *   wolfram unfollow    <service> <handle> <password> <actor>
 *   wolfram like        <service> <handle> <password> <at-uri>
 *   wolfram unlike      <service> <handle> <password> <like-at-uri>
 *   wolfram repost      <service> <handle> <password> <at-uri>
 *   wolfram delete-repost <service> <handle> <password> <repost-at-uri>
 *   wolfram delete      <service> <handle> <password> <at-uri>
 *   wolfram mute        <service> <handle> <password> <actor>
 *   wolfram unmute      <service> <handle> <password> <actor>
 *   wolfram search      <service> <handle> <password> <query> [limit]
 *   wolfram resolve     <service> <handle-or-did>
 *   wolfram thread      <service> <handle> <password> <at-uri> [depth]
 *   wolfram notifications <service> <handle> <password> [limit]
 *   wolfram follows     <service> <actor> [limit]
 *   wolfram followers   <service> <actor> [limit]
 *   wolfram blocks      <service> <handle> <password> [limit]
 *   wolfram mutes       <service> <handle> <password> [limit]
 *   wolfram list        <service> <list-uri> [limit]
 *   wolfram lists       <service> <actor> [limit]
 *   wolfram labels subscribe <service> [--cursor N] [--seconds N]
 *   wolfram video upload <service> <handle> <password> <file.mp4>
 *   wolfram video status <service> <handle> <password> <job-id>
 *   wolfram moderation  <service> <actor> [labeler-did]
 *   wolfram oauth-login <service> <handle> [client-id] [redirect-uri]
 *   wolfram describe-server <service>
 *   wolfram whoami      <service> <handle> <password>
 *   wolfram help [command]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <cJSON.h>
#include <stdbool.h>

#include <inttypes.h>
#include <time.h>

#include "wolfram/agent.h"
#include "wolfram/blob.h"
#include "wolfram/identity.h"
#include "wolfram/label.h"
#include "wolfram/oauth.h"
#include "wolfram/server.h"
#include "wolfram/syntax.h"
#include "wolfram/version.h"
#include "wolfram/xrpc.h"
#include "wolfram/thread_typed.h"
#include "wolfram/feed_typed.h"
#include "wolfram/actor_typed.h"
#include "wolfram/moderation.h"
#include "wolfram/repo_typed.h"
#include "wolfram/feedgen_typed.h"
#include "wolfram/notification_typed.h"
#include "wolfram/moderation_report_typed.h"

/* Global --json flag: when set, list/get commands print the raw JSON body
 * instead of human-readable text. Parsed in main() before dispatch. */
static bool g_json = false;

/* ----------------------------------------------------------------- */
/* Usage                                                             */
/* ----------------------------------------------------------------- */

static void usage_stream(FILE *out) {
    fprintf(out,
        "wolfram %s — a command-line client for the AT Protocol (via wolfram SDK)\n\n"
        "usage: wolfram <command> [args...]\n"
        "       wolfram help <command>   — per-command help\n"
        "       wolfram --version        — print version and exit\n\n"
        "commands:\n"
        "\n"
        "  Session & account:\n"
        "    login            <service> <handle> <password>\n"
        "    whoami           <service> <handle> <password>\n"
        "    describe-server  <service>\n"
        "\n"
         "  Posting & interaction:\n"
         "    post             <service> <handle> <password> <text...>\n"
         "    reply            <service> <handle> <password> <parent-at-uri> <text...>\n"
         "    delete           <service> <handle> <password> <at-uri>\n"
         "    like             <service> <handle> <password> <at-uri>\n"
         "    unlike           <service> <handle> <password> <like-at-uri>\n"
         "    repost           <service> <handle> <password> <at-uri>\n"
         "    delete-repost    <service> <handle> <password> <repost-at-uri>\n"
         "\n"
         "  Feed & discovery:\n"
         "    timeline         <service> <handle> <password> [pages]\n"
         "    get-post         <service> <at-uri>\n"
         "    profile          <service> <actor>\n"
         "    search           <service> <handle> <password> <query> [limit]\n"
         "    thread           <service> <handle> <password> <at-uri> [depth]\n"
         "    notifications    <service> <handle> <password> [limit]\n"
         "\n"
          "  Social graph:\n"
          "    follow           <service> <handle> <password> <actor>\n"
          "    unfollow         <service> <handle> <password> <actor>\n"
          "    block            <service> <handle> <password> <actor>\n"
          "    unblock          <service> <handle> <password> <actor>\n"
          "    mute             <service> <handle> <password> <actor>\n"
          "    unmute           <service> <handle> <password> <actor>\n"
          "\n"
           "  Identity & moderation:\n"
          "    resolve          <service> <handle-or-did>\n"
          "    labels subscribe <service> [--cursor N] [--seconds N]\n"
          "    moderation       <service> <actor> [labeler-did]\n"
          "    moderation report <service> <handle> <password> --subject <uri> --reason <reason> [--reason-type <type>] [--cid <cid>]\n"
          "    oauth-login      <service> <handle> [client-id] [redirect-uri] [--state-file <path>]\n"
          "    oauth-callback   <service> --url <redirect> --state <state> [--state-file <path>] [--client-id <id>] [--redirect-uri <uri>] [--session <path>]\n"
          "\n"
          "  Graph & lists:\n"
          "    follows          <service> <actor> [limit]\n"
          "    followers        <service> <actor> [limit]\n"
          "    blocks           <service> <handle> <password> [limit]\n"
          "    mutes            <service> <handle> <password> [limit]\n"
          "    list             <service> <list-uri> [limit]\n"
          "    lists            <service> <actor> [limit]\n"
          "\n"
          "  Records & media:\n"
          "    get-record       <service> <handle> <password> <collection> <rkey>\n"
          "    repo put-record  <service> <handle> <password> --collection <nsid> --rkey <rkey> --json <record|file>\n"
          "    repo delete-record <service> <handle> <password> --collection <nsid> --rkey <rkey>\n"
          "    repo list-records <service> <handle> <password> --collection <nsid> [--limit N] [--cursor C]\n"
          "    repo describe     <service> <handle> <password> --repo <did-or-handle>\n"
          "    video upload     <service> <handle> <password> <file.mp4>\n"
          "    video status     <service> <handle> <password> <job-id>\n"
          "\n"
          "  Feed & discovery:\n"
          "    feed get         <service> <handle> <password> --feed <generator-uri> [--limit N] [--cursor C]\n"
          "    feed author      <service> <handle> <password> --actor <handle-or-did> [--limit N] [--cursor C]\n"
          "\n"
          "  Global options:\n"
          "    --json           print raw JSON for list/get commands\n"
          "\n"
          "  <at-uri> is an at:// URI (e.g. at://did:plc:xxx/app.bsky.feed.post/rkey)\n"
          "  <actor> is a handle or DID\n", WOLFRAM_VERSION_STRING);
}

/* Per-command help text. */
static void cmd_help_stream(FILE *out, const char *cmd) {
    if (!cmd) return;

    struct { const char *name; const char *usage; const char *desc; } cmds[] = {
        {"login",            "login <service> <handle> <password>",
         "Create a session via com.atproto.server.createSession."},
        {"whoami",           "whoami <service> <handle> <password>",
         "Log in and print the current session DID and handle."},
        {"describe-server",  "describe-server <service>",
         "Fetch server metadata via com.atproto.server.describeServer (no auth)."},
        {"post",             "post <service> <handle> <password> <text...>",
         "Create a new post. Rich-text facets (mentions, links, tags) are auto-detected."},
        {"reply",            "reply <service> <handle> <password> <parent-at-uri> <text...>",
         "Reply to a post. The parent CID and root ref are resolved via getPosts and a reply record is created via wf_agent_create_record."},
        {"delete",           "delete <service> <handle> <password> <at-uri>",
         "Delete a post created by the authenticated user."},
        {"like",             "like <service> <handle> <password> <at-uri>",
         "Like a post. The post's CID is resolved via getPosts, then wf_agent_like is called."},
        {"unlike",           "unlike <service> <handle> <password> <like-at-uri>",
         "Delete a like record by its at:// URI via wf_agent_unlike."},
        {"repost",           "repost <service> <handle> <password> <at-uri>",
         "Repost a post. The post's CID is resolved via getPosts, then wf_agent_repost is called."},
        {"delete-repost",    "delete-repost <service> <handle> <password> <repost-at-uri>",
         "Delete a repost record by its at:// URI via wf_agent_delete_repost."},
        {"unrepost",         "unrepost <service> <handle> <password> <repost-at-uri>",
         "Alias for delete-repost: remove a repost record by its at:// URI."},
        {"timeline",         "timeline <service> <handle> <password> [pages]",
         "Fetch the authenticated user's home timeline. pages=0 (default) fetches until exhausted."},
        {"get-post",         "get-post <service> <at-uri>",
         "Fetch a single record via com.atproto.repo.getRecord (no auth)."},
        {"profile",          "profile <service> <actor>",
         "Fetch and display an actor's profile via wf_agent_get_profile."},
        {"search",           "search <service> <handle> <password> <query> [limit]",
         "Search posts via app.bsky.feed.searchPosts. limit defaults to 25."},
        {"thread",           "thread <service> <handle> <password> <at-uri> [depth]",
         "Fetch and display a post thread tree. depth defaults to 6."},
        {"notifications",    "notifications <service> <handle> <password> [limit]",
         "List recent notifications. limit defaults to 50."},
        {"follow",           "follow <service> <handle> <password> <actor>",
         "Follow an actor (handle or DID)."},
        {"unfollow",         "unfollow <service> <handle> <password> <actor>",
         "Unfollow an actor by looking up and deleting the follow record."},
        {"mute",             "mute <service> <handle> <password> <actor>",
         "Mute an actor (handle or DID)."},
        {"unmute",           "unmute <service> <handle> <password> <actor>",
         "Unmute an actor (handle or DID)."},
        {"resolve",          "resolve <service> <handle-or-did>",
         "Resolve a handle to a DID via wf_handle_resolve. A DID is echoed back unchanged."},
        {"labels",           "labels subscribe <service> [--cursor N] [--seconds N]",
         "Subscribe to com.atproto.label.subscribeLabels via the label.h streaming API and print each arriving label. Bounded by --seconds (default 30) or SIGINT."},
        {"moderation",       "moderation <service> <actor> [labeler-did]",
         "Run the moderation decision engine on an actor's profile."},
        {"oauth-login",      "oauth-login <service> <handle> [client-id] [redirect-uri]",
         "Demonstrate the OAuth login path: discover the authorization server and begin a PAR flow, printing the authorization URL and state."},
        {"follows",          "follows <service> <actor> [limit]",
         "List the accounts an actor follows via wf_agent_get_follows."},
        {"followers",        "followers <service> <actor> [limit]",
         "List an actor's followers via wf_agent_get_followers."},
        {"blocks",           "blocks <service> <handle> <password> [limit]",
         "List the accounts the authenticated user blocks via wf_agent_get_blocks."},
        {"mutes",            "mutes <service> <handle> <password> [limit]",
         "List the accounts the authenticated user mutes via wf_agent_get_mutes."},
        {"list",             "list <service> <list-uri> [limit]",
         "Fetch the items of a list via wf_agent_get_list."},
        {"lists",            "lists <service> <actor> [limit]",
         "List the lists an actor owns via wf_agent_get_lists."},
        {"get-record",       "get-record <service> <handle> <password> <collection> <rkey>",
         "Fetch a single record via wf_agent_get_record and print the JSON."},
        {"video",            "video upload <service> <handle> <password> <file.mp4>",
         "Upload a video blob via wf_agent_upload_video and report the blob CID and size."},
        {"video",            "video status <service> <handle> <password> <job-id>",
          "Poll a video processing job via wf_agent_get_video_job_status."},
        {"oauth-login",      "oauth-login <service> <handle> [client-id] [redirect-uri] [--state-file <path>]",
          "Discover the authorization server, begin a PAR flow, and save the pending state. Prints the authorization URL and the opaque state token."},
        {"oauth-callback",   "oauth-callback <service> --url <redirect> --state <state> [--state-file <path>] [--client-id <id>] [--redirect-uri <uri>] [--session <path>]",
          "Complete the OAuth flow: validate the callback, exchange the code for tokens, and persist the session (default ~/.wolfram_session.json)."},
        {"block",            "block <service> <handle> <password> <actor>",
          "Block an actor (handle or DID) via wf_agent_block."},
        {"unblock",          "unblock <service> <handle> <password> <actor>",
          "Unblock an actor by finding and deleting the block record via wf_agent_unblock."},
        {"notifications",    "notifications update-seen [--seen-at <iso>] <service> <handle> <password>",
          "Mark notifications seen via wf_agent_update_seen_typed."},
        {"repo",             "repo put-record <service> <handle> <password> --collection <nsid> --rkey <rkey> --json <record|file>",
          "Write a record via wf_agent_put_record_typed (read JSON from a string or file)."},
        {"repo",             "repo delete-record <service> <handle> <password> --collection <nsid> --rkey <rkey>",
          "Delete a record via wf_agent_delete_record_typed."},
        {"repo",             "repo list-records <service> <handle> <password> --collection <nsid> [--limit N] [--cursor C]",
          "List records via wf_agent_list_records_typed."},
        {"repo",             "repo describe <service> <handle> <password> --repo <did-or-handle>",
          "Describe a repository via wf_agent_describe_repo_typed."},
        {"feed",             "feed get <service> <handle> <password> --feed <generator-uri> [--limit N] [--cursor C]",
          "Fetch a custom feed via wf_agent_get_feed_typed."},
        {"feed",             "feed author <service> <handle> <password> --actor <handle-or-did> [--limit N] [--cursor C]",
          "Fetch an actor's author feed via wf_agent_get_author_feed_typed."},
        {"moderation",       "moderation report <service> <handle> <password> --subject <uri> --reason <reason> [--reason-type <type>] [--cid <cid>]",
          "Submit a moderation report via wf_agent_report_typed."},
    };

    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); ++i) {
        if (strcmp(cmd, cmds[i].name) == 0) {
            fprintf(out, "wolfram %s\n\n  %s\n\n  %s\n",
                    cmds[i].usage, cmds[i].usage, cmds[i].desc);
            return;
        }
    }

    fprintf(out, "error: no help for unknown command '%s'\n\n", cmd);
    usage_stream(out);
}

/* Print usage and exit 0 (offline-safe: never touches the network). */
static int usage_exit(void) {
    usage_stream(stdout);
    return 0;
}

/* ----------------------------------------------------------------- */
/* Small helpers                                                     */
/* ----------------------------------------------------------------- */

/* Join argv[first..argc-1] into a single heap string (caller frees). */
static char *join_args(int argc, char **argv, int first) {
    size_t total = 1;
    for (int i = first; i < argc; ++i) {
        size_t part = strlen(argv[i]);
        if (part > SIZE_MAX - total - 1) {
            return NULL;
        }
        total += part;
        if (i + 1 < argc) {
            ++total;
        }
    }

    char *out = malloc(total);
    if (!out) {
        return NULL;
    }

    char *dst = out;
    for (int i = first; i < argc; ++i) {
        size_t part = strlen(argv[i]);
        memcpy(dst, argv[i], part);
        dst += part;
        if (i + 1 < argc) {
            *dst++ = ' ';
        }
    }
    *dst = '\0';
    return out;
}

/* Resolve an actor argument (a handle or DID) to a heap-owned DID string.
 * When `actor` is already a valid DID, a copy is returned. Otherwise the
 * agent's handle-resolution endpoint is used. Caller frees *out_did. */
static wf_status resolve_actor_to_did(wf_agent *agent, const char *actor,
                                      char **out_did) {
    if (!agent || !actor || !out_did) {
        return WF_ERR_INVALID_ARG;
    }
    *out_did = NULL;

    if (wf_syntax_did_is_valid(actor)) {
        char *dup = strdup(actor);
        if (!dup) {
            return WF_ERR_ALLOC;
        }
        *out_did = dup;
        return WF_OK;
    }

    return wf_agent_resolve_handle(agent, actor, out_did);
}

/* Create an agent, login, and return it. Returns NULL on failure (error
 * already printed to stderr). Caller frees with wf_agent_free. */
static wf_agent *agent_login_or_err(const char *service, const char *handle,
                                    const char *password) {
    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return NULL;
    }
    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return NULL;
    }
    return agent;
}

/* Resolve a post at-uri to its CID via app.bsky.feed.getPosts.
 * Caller frees *out_cid. Returns WF_OK or an error status. */
static wf_status resolve_post_cid(wf_agent *agent, const char *at_uri,
                                  char **out_cid) {
    if (!agent || !at_uri || !out_cid) {
        return WF_ERR_INVALID_ARG;
    }
    *out_cid = NULL;

    const char *uris[1] = {at_uri};
    wf_response res = {0};
    wf_status s = wf_agent_get_posts(agent, uris, 1, &res);
    if (s != WF_OK) {
        wf_response_free(&res);
        return s;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        return WF_ERR_PARSE;
    }

    cJSON *posts = cJSON_GetObjectItemCaseSensitive(root, "posts");
    if (!cJSON_IsArray(posts) || cJSON_GetArraySize(posts) == 0) {
        cJSON_Delete(root);
        return WF_ERR_NOT_FOUND;
    }

    cJSON *post = cJSON_GetArrayItem(posts, 0);
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(post, "cid");
    if (!cJSON_IsString(cid) || !cid->valuestring) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    *out_cid = strdup(cid->valuestring);
    cJSON_Delete(root);
    return (*out_cid) ? WF_OK : WF_ERR_ALLOC;
}

/* Resolve a post at-uri to its CID and root reply reference (root uri/cid)
 * via app.bsky.feed.getPosts. Used by the reply subcommand to build the
 * proper reply ref. Caller frees *out_cid, *out_root_uri, *out_root_cid.
 * When the parent post has no reply ref, root defaults to the parent itself. */
static wf_status resolve_post_for_reply(wf_agent *agent, const char *at_uri,
                                        char **out_cid, char **out_root_uri,
                                        char **out_root_cid) {
    if (!agent || !at_uri || !out_cid || !out_root_uri || !out_root_cid) {
        return WF_ERR_INVALID_ARG;
    }
    *out_cid = NULL;
    *out_root_uri = NULL;
    *out_root_cid = NULL;

    const char *uris[1] = {at_uri};
    wf_response res = {0};
    wf_status s = wf_agent_get_posts(agent, uris, 1, &res);
    if (s != WF_OK) {
        wf_response_free(&res);
        return s;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        return WF_ERR_PARSE;
    }

    cJSON *posts = cJSON_GetObjectItemCaseSensitive(root, "posts");
    if (!cJSON_IsArray(posts) || cJSON_GetArraySize(posts) == 0) {
        cJSON_Delete(root);
        return WF_ERR_NOT_FOUND;
    }

    cJSON *post = cJSON_GetArrayItem(posts, 0);
    cJSON *uri_j = cJSON_GetObjectItemCaseSensitive(post, "uri");
    cJSON *cid_j = cJSON_GetObjectItemCaseSensitive(post, "cid");
    if (!cJSON_IsString(uri_j) || !cJSON_IsString(cid_j)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    const char *parent_uri = uri_j->valuestring;
    const char *parent_cid = cid_j->valuestring;
    const char *root_uri = parent_uri;
    const char *root_cid = parent_cid;

    /* If the parent post is itself a reply, use its root ref. */
    cJSON *record = cJSON_GetObjectItemCaseSensitive(post, "record");
    if (record) {
        cJSON *reply = cJSON_GetObjectItemCaseSensitive(record, "reply");
        if (cJSON_IsObject(reply)) {
            cJSON *root_ref = cJSON_GetObjectItemCaseSensitive(reply, "root");
            if (cJSON_IsObject(root_ref)) {
                cJSON *ru = cJSON_GetObjectItemCaseSensitive(root_ref, "uri");
                cJSON *rc = cJSON_GetObjectItemCaseSensitive(root_ref, "cid");
                if (cJSON_IsString(ru) && cJSON_IsString(rc)) {
                    root_uri = ru->valuestring;
                    root_cid = rc->valuestring;
                }
            }
        }
    }

    *out_cid = strdup(parent_cid);
    *out_root_uri = strdup(root_uri);
    *out_root_cid = strdup(root_cid);
    cJSON_Delete(root);

    if (!*out_cid || !*out_root_uri || !*out_root_cid) {
        free(*out_cid); *out_cid = NULL;
        free(*out_root_uri); *out_root_uri = NULL;
        free(*out_root_cid); *out_root_cid = NULL;
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/* Format the current UTC time as an RFC 3339 timestamp (e.g. for record
 * createdAt fields). Writes at most `len` bytes into `buf`. */
static void now_rfc3339(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm tm_utc;
#ifdef _WIN32
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    if (strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0) {
        if (len > 0) buf[0] = '\0';
    }
}

/* ----------------------------------------------------------------- */
/* Subcommands                                                       */
/* ----------------------------------------------------------------- */

static int cmd_login(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    printf("logged in as %s (%s)\n", sd.handle ? sd.handle : "?",
           sd.did ? sd.did : "?");
    wf_agent_session_data_free(&sd);
    wf_agent_free(agent);
    return 0;
}

static int cmd_post(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    char *text = join_args(argc, argv, 4);
    if (!text) {
        fprintf(stderr, "error: failed to assemble post text\n");
        return 1;
    }

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        free(text);
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        free(text);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result result = {0};
    s = wf_agent_post(agent, text, &result);
    free(text);
    if (s != WF_OK) {
        fprintf(stderr, "error: post failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", result.uri ? result.uri : "(no uri returned)");
    wf_agent_post_result_free(&result);
    wf_agent_free(agent);
    return 0;
}

typedef struct {
    int printed;
} timeline_ctx;

static wf_status timeline_on_page(wf_agent *agent,
                                  const wf_agent_feed_list *feed,
                                  const char *cursor, void *ud) {
    (void)agent;
    (void)cursor;
    timeline_ctx *ctx = (timeline_ctx *)ud;

    for (size_t i = 0; i < feed->item_count; ++i) {
        const wf_agent_post_view *post = &feed->items[i].post;
        const char *author = post->author.handle
                                 ? post->author.handle
                                 : (post->author.did ? post->author.did : "?");
        const char *text = "";
        if (post->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(post->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("%s: %s\n", author, text);
        ctx->printed++;
    }
    return WF_OK;
}

static int cmd_timeline(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int max_pages = (argc >= 5) ? atoi(argv[4]) : 0; /* 0 = until exhausted */

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    if (g_json) {
        wf_response res = {0};
        s = wf_agent_get_timeline(agent, 50, NULL, NULL, &res);
        if (s != WF_OK) fprintf(stderr, "error: request failed (status %d)\n", (int)s);
        else if (res.body && res.body_len > 0) printf("%s\n", res.body);
        else printf("(empty response, HTTP %ld)\n", res.status);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 0;
    }

    timeline_ctx ctx = {0};
    char *last_cursor = NULL;
    s = wf_agent_get_timeline_paged(agent, 10, max_pages, timeline_on_page,
                                    &ctx, &last_cursor);
    if (s != WF_OK) {
        fprintf(stderr, "error: timeline failed (status %d)\n", (int)s);
        free(last_cursor);
        wf_agent_free(agent);
        return 1;
    }

    if (ctx.printed == 0) {
        printf("(timeline empty)\n");
    }
    free(last_cursor);
    wf_agent_free(agent);
    return 0;
}

static int cmd_get_post(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *uri = argv[2];

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed) || !parsed.authority ||
        !parsed.collection || !parsed.record_key) {
        fprintf(stderr, "error: invalid at-uri: %s\n", uri);
        wf_syntax_aturi_free(&parsed);
        return 1;
    }

    wf_xrpc_client *client = wf_xrpc_client_new(service);
    if (!client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        wf_syntax_aturi_free(&parsed);
        return 1;
    }

    wf_xrpc_param params[] = {
        {"repo", parsed.authority},
        {"collection", parsed.collection},
        {"rkey", parsed.record_key},
    };

    wf_response res = {0};
    wf_status s = wf_xrpc_query_params(client, "com.atproto.repo.getRecord",
                                       params, 3, &res);
    wf_syntax_aturi_free(&parsed);

    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getRecord failed (status %d)\n", (int)s);
        wf_response_free(&res);
        wf_xrpc_client_free(client);
        return 1;
    }

    if (res.body && res.body_len > 0) {
        printf("%s\n", res.body);
    } else {
        printf("(empty response, HTTP %ld)\n", res.status);
    }

    wf_response_free(&res);
    wf_xrpc_client_free(client);
    return 0;
}

static int cmd_profile(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_agent_profile prof = {0};
    wf_status s = wf_agent_get_profile(agent, actor, &prof);
    if (s != WF_OK) {
        fprintf(stderr, "error: getProfile failed (status %d)\n", (int)s);
        wf_agent_profile_free(&prof);
        wf_agent_free(agent);
        return 1;
    }

    if (g_json) {
        cJSON *j = cJSON_CreateObject();
        if (j) {
            if (prof.did) cJSON_AddStringToObject(j, "did", prof.did);
            if (prof.handle) cJSON_AddStringToObject(j, "handle", prof.handle);
            if (prof.display_name)
                cJSON_AddStringToObject(j, "displayName", prof.display_name);
            if (prof.description)
                cJSON_AddStringToObject(j, "description", prof.description);
            if (prof.avatar_cid)
                cJSON_AddStringToObject(j, "avatar", prof.avatar_cid);
            cJSON_AddNumberToObject(j, "followersCount", prof.followers_count);
            cJSON_AddNumberToObject(j, "followsCount", prof.follows_count);
            cJSON_AddNumberToObject(j, "postsCount", prof.posts_count);
            char *out = cJSON_PrintUnformatted(j);
            cJSON_Delete(j);
            if (out) { printf("%s\n", out); free(out); }
        }
        wf_agent_profile_free(&prof);
        wf_agent_free(agent);
        return 0;
    }

    printf("%s\n", prof.display_name ? prof.display_name : "(no display name)");
    if (prof.description) {
        printf("%s\n", prof.description);
    }
    wf_agent_profile_free(&prof);
    wf_agent_free(agent);
    return 0;
}

static int cmd_follow(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    char *subject_did = NULL;
    s = resolve_actor_to_did(agent, actor, &subject_did);
    if (s != WF_OK || !subject_did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result result = {0};
    s = wf_agent_follow(agent, subject_did, &result);
    free(subject_did);
    if (s != WF_OK) {
        fprintf(stderr, "error: follow failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&result);
        wf_agent_free(agent);
        return 1;
    }

    printf("followed: %s\n", result.uri ? result.uri : "(no uri returned)");
    wf_agent_post_result_free(&result);
    wf_agent_free(agent);
    return 0;
}

static int cmd_unfollow(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    char *subject_did = NULL;
    s = resolve_actor_to_did(agent, actor, &subject_did);
    if (s != WF_OK || !subject_did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    /* Look up the existing follow URI via the viewer's relationship state. */
    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    const char *others[1] = {subject_did};
    wf_response res = {0};
    s = wf_agent_get_relationships(agent, sd.did, others, 1, &res);
    wf_agent_session_data_free(&sd);
    free(subject_did);

    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getRelationships failed (status %d)\n", (int)s);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 1;
    }

    char *follow_uri = NULL;
    if (res.body) {
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        if (root) {
            cJSON *rels = cJSON_GetObjectItemCaseSensitive(root, "relationships");
            if (cJSON_IsArray(rels) && cJSON_GetArraySize(rels) > 0) {
                cJSON *rel = cJSON_GetArrayItem(rels, 0);
                cJSON *following =
                    cJSON_GetObjectItemCaseSensitive(rel, "following");
                if (cJSON_IsString(following) && following->valuestring) {
                    follow_uri = strdup(following->valuestring);
                }
            }
            cJSON_Delete(root);
        }
    }
    wf_response_free(&res);

    if (!follow_uri) {
        printf("(not currently following %s)\n", actor);
        wf_agent_free(agent);
        return 0;
    }

    s = wf_agent_unfollow(agent, follow_uri);
    free(follow_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: unfollow failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("unfollowed %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

static int cmd_resolve(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle_or_did = argv[2];

    if (wf_syntax_did_is_valid(handle_or_did)) {
        printf("%s\n", handle_or_did);
        return 0;
    }

    wf_xrpc_client *client = wf_xrpc_client_new(service);
    if (!client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        return 1;
    }

    char *did = NULL;
    wf_status s = wf_handle_resolve(client, handle_or_did, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve '%s' (status %d)\n",
                handle_or_did, (int)s);
        wf_xrpc_client_free(client);
        return 1;
    }

    printf("%s\n", did);
    free(did);
    wf_xrpc_client_free(client);
    return 0;
}

/* Recursively print a thread node and its replies (indented by depth). */
static void print_thread_node(const wf_agent_thread_node *node, int depth) {
    if (!node) {
        return;
    }
    for (int i = 0; i < depth; ++i) {
        fputc(' ', stdout);
        fputc(' ', stdout);
    }

    if (node->kind == WF_AGENT_THREAD_KIND_POST) {
        const char *handle = node->post.author.handle
                                 ? node->post.author.handle
                                 : (node->post.author.did
                                        ? node->post.author.did
                                        : "?");
        const char *did = node->post.author.did
                              ? node->post.author.did
                              : "?";
        const char *text = "";
        if (node->post.record) {
            cJSON *t =
                cJSON_GetObjectItemCaseSensitive(node->post.record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("[%s] %s: %s\n", did, handle, text);
    } else {
        printf("(not found/blocked: %s)\n",
               node->uri ? node->uri : "?");
    }

    for (size_t i = 0; i < node->replies_count; ++i) {
        print_thread_node(&node->replies[i], depth + 1);
    }
}

static int cmd_thread(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];
    int depth = (argc >= 6) ? atoi(argv[5]) : 6;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    if (g_json) {
        wf_response res = {0};
        s = wf_agent_get_post_thread(agent, at_uri, depth, 0, &res);
        if (s != WF_OK) fprintf(stderr, "error: request failed (status %d)\n", (int)s);
        else if (res.body && res.body_len > 0) printf("%s\n", res.body);
        else printf("(empty response, HTTP %ld)\n", res.status);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 0;
    }

    wf_agent_thread thread = {0};
    s = wf_agent_get_post_thread_typed(agent, at_uri, depth, &thread);
    if (s != WF_OK) {
        fprintf(stderr, "error: getPostThread failed (status %d)\n", (int)s);
        wf_agent_thread_free(&thread);
        wf_agent_free(agent);
        return 1;
    }

    print_thread_node(&thread.root, 0);
    wf_agent_thread_free(&thread);
    wf_agent_free(agent);
    return 0;
}

static int cmd_notifications(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }

    /* notifications update-seen [--seen-at <iso>] <service> <handle> <password> */
    if (strcmp(argv[1], "update-seen") == 0) {
        const char *seen_at = NULL;
        const char *pos[4];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--seen-at") == 0 && i + 1 < argc) {
                seen_at = argv[++i];
            } else if (pi < 4) {
                pos[pi++] = argv[i];
            }
        }
        if (pi < 3) {
            fprintf(stderr, "error: usage: wolfram notifications update-seen "
                    "[--seen-at <iso>] <service> <handle> <password>\n");
            return 1;
        }

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        wf_status s = wf_agent_update_seen_typed(agent, seen_at);
        if (s != WF_OK) {
            fprintf(stderr, "error: updateSeen failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("notifications marked seen%s%s\n",
               seen_at ? " at " : "", seen_at ? seen_at : "");
        wf_agent_free(agent);
        return 0;
    }

    /* notifications <service> <handle> <password> [limit] */
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    if (g_json) {
        wf_response res = {0};
        s = wf_agent_list_notifications(agent, limit, NULL, &res);
        if (s != WF_OK) fprintf(stderr, "error: request failed (status %d)\n", (int)s);
        else if (res.body && res.body_len > 0) printf("%s\n", res.body);
        else printf("(empty response, HTTP %ld)\n", res.status);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 0;
    }

    wf_agent_notification_list list = {0};
    s = wf_agent_list_notifications_typed(agent, limit, NULL, &list);
    if (s != WF_OK) {
        fprintf(stderr, "error: listNotifications failed (status %d)\n",
                (int)s);
        wf_agent_notification_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }

    for (size_t i = 0; i < list.notification_count; ++i) {
        const wf_agent_notification *n = &list.notifications[i];
        const char *author =
            n->author.handle
                ? n->author.handle
                : (n->author.did ? n->author.did : "?");
        const char *did = n->author.did ? n->author.did : "?";
        const char *text = "";
        if (n->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(n->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("reason=%s author=%s (%s) read=%d\n  %s\n",
               n->reason ? n->reason : "?", author, did, n->is_read, text);
    }
    if (list.notification_count == 0) {
        printf("(no notifications)\n");
    }

    wf_agent_notification_list_free(&list);
    wf_agent_free(agent);
    return 0;
}

static int cmd_moderation(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }

    /* moderation report <service> <handle> <password> --subject <uri>
     *   --reason <reason> [--reason-type <type>] [--cid <cid>] */
    if (strcmp(argv[1], "report") == 0) {
        const char *subject = NULL, *reason = NULL, *reason_type = NULL,
                   *cid = NULL;
        const char *pos[3]; int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--subject") == 0 && i + 1 < argc) subject = argv[++i];
            else if (strcmp(argv[i], "--reason") == 0 && i + 1 < argc) reason = argv[++i];
            else if (strcmp(argv[i], "--reason-type") == 0 && i + 1 < argc) reason_type = argv[++i];
            else if (strcmp(argv[i], "--cid") == 0 && i + 1 < argc) cid = argv[++i];
            else if (pi < 3) pos[pi++] = argv[i];
        }
        if (pi < 3 || !subject || !reason) {
            fprintf(stderr, "error: usage: wolfram moderation report <service> "
                    "<handle> <password> --subject <uri> --reason <reason> "
                    "[--reason-type <type>] [--cid <cid>]\n");
            return 1;
        }

        /* The SDK requires a reasonType NSID. If --reason-type is omitted, the
         * --reason text is used as the type (best effort); when --reason-type is
         * present, --reason is the optional free-text note. */
        const char *rt = reason_type ? reason_type : reason;
        const char *free_reason = reason_type ? reason : NULL;

        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;

        const char *subject_did = NULL;
        const char *subject_uri = NULL;
        const char *subject_cid = NULL;
        if (strncmp(subject, "did:", 4) == 0) {
            subject_did = subject;
        } else {
            subject_uri = subject;
            subject_cid = cid;
        }

        wf_moderation_report_record out = {0};
        wf_status s = wf_agent_report_typed(agent, subject_did, subject_uri,
                                            subject_cid, rt, free_reason, NULL,
                                            NULL, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: createReport failed (status %d)\n", (int)s);
            wf_moderation_report_record_free(&out);
            wf_agent_free(agent);
            return 1;
        }
        printf("report id=%" PRId64 "\n", out.id);
        if (out.reason_type) printf("reasonType: %s\n", out.reason_type);
        if (out.reported_by) printf("reportedBy: %s\n", out.reported_by);
        wf_moderation_report_record_free(&out);
        wf_agent_free(agent);
        return 0;
    }

    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    /* argv[3] (labeler-did) is accepted but not required by the wrapper. */

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    char *did = NULL;
    wf_status rs = resolve_actor_to_did(agent, actor, &did);
    if (rs != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)rs);
        wf_agent_free(agent);
        return 1;
    }

    wf_mod_decision *decision = NULL;
    wf_status s = wf_agent_moderate_profile(agent, did, &decision);
    free(did);
    if (s != WF_OK || !decision) {
        fprintf(stderr, "error: moderation failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_mod_ui ui = {0};
    s = wf_mod_decision_ui(decision, WF_MOD_CTX_PROFILE_VIEW, &ui);
    if (s != WF_OK) {
        fprintf(stderr, "error: decision UI failed (status %d)\n", (int)s);
        wf_mod_decision_free(decision);
        wf_agent_free(agent);
        return 1;
    }

    printf("moderation for %s: alerts=%zu blurs=%zu informs=%zu\n",
           decision->did ? decision->did : actor, ui.alert_count,
           ui.blur_count, ui.inform_count);

    wf_mod_ui_free(&ui);
    wf_mod_decision_free(decision);
    wf_agent_free(agent);
    return 0;
}

/* wolfram search <service> <handle> <password> <query> [limit] */
static int cmd_search(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *query = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 25;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_agent_actor_list list = {0};
    wf_status s = wf_agent_search_actors_typed(agent, query, limit, NULL,
                                               &list);
    if (s != WF_OK) {
        fprintf(stderr, "error: search failed (status %d)\n", (int)s);
        wf_agent_actor_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }

    for (size_t i = 0; i < list.actor_count; ++i) {
        const wf_agent_profile_view *a = &list.actors[i];
        printf("%s (%s)\n",
               a->handle ? a->handle : "?",
               a->did ? a->did : "?");
        if (a->display_name) {
            printf("  %s\n", a->display_name);
        }
    }
    if (list.actor_count == 0) {
        printf("(no results)\n");
    }

    wf_agent_actor_list_free(&list);
    wf_agent_free(agent);
    return 0;
}

/* wolfram mute <service> <handle> <password> <actor> */
static int cmd_mute(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    s = wf_agent_mute(agent, did);
    free(did);
    if (s != WF_OK) {
        fprintf(stderr, "error: mute failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("muted %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

/* wolfram unmute <service> <handle> <password> <actor> */
static int cmd_unmute(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    s = wf_agent_unmute(agent, did);
    free(did);
    if (s != WF_OK) {
        fprintf(stderr, "error: unmute failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("unmuted %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

/* ----------------------------------------------------------------- */
/* New subcommands                                                  */
/* ----------------------------------------------------------------- */

static int cmd_like(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *cid = NULL;
    wf_status s = resolve_post_cid(agent, at_uri, &cid);
    if (s != WF_OK || !cid) {
        fprintf(stderr, "error: could not resolve CID for %s (status %d)\n",
                at_uri, (int)s);
        free(cid);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_like(agent, at_uri, cid, &out);
    free(cid);
    if (s != WF_OK) {
        fprintf(stderr, "error: like failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

static int cmd_unlike(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *like_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_status s = wf_agent_unlike(agent, like_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: unlike failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("unliked %s\n", like_uri);
    wf_agent_free(agent);
    return 0;
}

static int cmd_repost(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *cid = NULL;
    wf_status s = resolve_post_cid(agent, at_uri, &cid);
    if (s != WF_OK || !cid) {
        fprintf(stderr, "error: could not resolve CID for %s (status %d)\n",
                at_uri, (int)s);
        free(cid);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_repost(agent, at_uri, cid, &out);
    free(cid);
    if (s != WF_OK) {
        fprintf(stderr, "error: repost failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

static int cmd_delete_repost(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *repost_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_status s = wf_agent_delete_repost(agent, repost_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: delete-repost failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("deleted repost %s\n", repost_uri);
    wf_agent_free(agent);
    return 0;
}

static int cmd_reply(int argc, char **argv) {
    if (argc < 6) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *parent_uri = argv[4];

    char *text = join_args(argc, argv, 5);
    if (!text) {
        fprintf(stderr, "error: failed to assemble reply text\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        free(text);
        return 1;
    }

    char *parent_cid = NULL, *root_uri = NULL, *root_cid = NULL;
    wf_status s = resolve_post_for_reply(agent, parent_uri, &parent_cid,
                                         &root_uri, &root_cid);
    if (s != WF_OK || !parent_cid || !root_uri || !root_cid) {
        fprintf(stderr,
                "error: could not resolve reply refs for %s (status %d)\n",
                parent_uri, (int)s);
        free(parent_cid);
        free(root_uri);
        free(root_cid);
        free(text);
        wf_agent_free(agent);
        return 1;
    }

    char created_at[32];
    now_rfc3339(created_at, sizeof(created_at));

    cJSON *rec = cJSON_CreateObject();
    cJSON_AddStringToObject(rec, "$type", "app.bsky.feed.post");
    cJSON_AddStringToObject(rec, "text", text);
    cJSON_AddStringToObject(rec, "createdAt", created_at);

    cJSON *reply = cJSON_CreateObject();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "uri", root_uri);
    cJSON_AddStringToObject(root, "cid", root_cid);
    cJSON *parent = cJSON_CreateObject();
    cJSON_AddStringToObject(parent, "uri", parent_uri);
    cJSON_AddStringToObject(parent, "cid", parent_cid);
    cJSON_AddItemToObject(reply, "root", root);
    cJSON_AddItemToObject(reply, "parent", parent);
    cJSON_AddItemToObject(rec, "reply", reply);

    char *record_json = cJSON_PrintUnformatted(rec);
    cJSON_Delete(rec);
    free(parent_cid);
    free(root_uri);
    free(root_cid);
    free(text);
    if (!record_json) {
        fprintf(stderr, "error: failed to serialize reply record\n");
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_create_record(agent, "app.bsky.feed.post", record_json, &out);
    free(record_json);
    if (s != WF_OK) {
        fprintf(stderr, "error: reply failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

/* Labels streaming — subscribe to com.atproto.label.subscribeLabels and
 * print each arriving label. The subscription blocks until interrupted (Ctrl-C
 * / SIGINT), a time bound elapses, or a small event cap is reached; it is
 * stopped cleanly via the handle pointer trick (mirrors
 * examples/subscribe_labels.c). */
#define CLI_LABEL_MAX_EVENTS 100

static volatile sig_atomic_t g_label_stop = 0;
static int g_label_count = 0;
static time_t g_label_start = 0;
static int g_label_seconds = 30;
static wf_label_subscribe_handle **g_label_handle_ptr = NULL;

static void label_on_sigint(int sig) {
    (void)sig;
    g_label_stop = 1;
    if (g_label_handle_ptr && *g_label_handle_ptr) {
        wf_label_subscribe_stop(*g_label_handle_ptr);
    }
}

static void label_on_label(const wf_label *label, void *userdata) {
    (void)userdata;
    g_label_count++;
    printf("label: uri=%s cid=%s val=%s src=%s exp=%s neg=%d\n",
           label->uri ? label->uri : "",
           label->cid ? label->cid : "",
           label->val ? label->val : "",
           label->src ? label->src : "",
           label->exp ? label->exp : "",
           label->neg);
    fflush(stdout);
    if (g_label_stop ||
        (g_label_seconds > 0 && (time(NULL) - g_label_start) >= g_label_seconds) ||
        (g_label_handle_ptr && *g_label_handle_ptr &&
         g_label_count >= CLI_LABEL_MAX_EVENTS)) {
        if (g_label_handle_ptr && *g_label_handle_ptr) {
            wf_label_subscribe_stop(*g_label_handle_ptr);
        }
    }
}

static void label_on_info(const wf_label_info *info, void *userdata) {
    (void)userdata;
    printf("info: name=%s message=%s\n",
           info->name ? info->name : "",
           info->message ? info->message : "(none)");
    fflush(stdout);
}

static void label_on_error(wf_status status, const char *msg, void *userdata) {
    (void)userdata;
    fprintf(stderr, "error: label stream status=%d %s\n", (int)status,
            msg ? msg : "");
}

/* wolfram labels subscribe <service> [--cursor N] [--seconds N] */
static int cmd_labels(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "subscribe") != 0) {
        fprintf(stderr, "error: unknown labels subcommand '%s' (try 'subscribe')\n",
                sub);
        return 1;
    }

    const char *service = argv[2];

    int64_t cursor = 0;
    int has_cursor = 0;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) {
            cursor = (int64_t)strtoll(argv[++i], NULL, 10);
            has_cursor = 1;
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            g_label_seconds = atoi(argv[++i]);
        } else {
            fprintf(stderr, "error: unexpected labels argument '%s'\n", argv[i]);
            return 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = label_on_sigint;
    sigaction(SIGINT, &sa, NULL);

    g_label_stop = 0;
    g_label_count = 0;
    g_label_start = time(NULL);

    wf_label_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = service;
    opts.cursor = cursor;
    opts.has_cursor = has_cursor;
    opts.reconnect_delay_ms = 1000;
    opts.on_label = label_on_label;
    opts.on_neg = label_on_label;
    opts.on_info = label_on_info;
    opts.on_error = label_on_error;

    printf("subscribing to label stream at %s (cursor=%s, max %d events, %ds)\n",
           service, has_cursor ? argv[2] : "none", CLI_LABEL_MAX_EVENTS,
           g_label_seconds);

    wf_label_subscribe_handle *handle = NULL;
    g_label_handle_ptr = &handle;
    wf_status s = wf_label_subscribe_start(&opts, &handle);
    g_label_handle_ptr = NULL;

    if (s != WF_OK) {
        fprintf(stderr, "error: label subscription ended (status %d)\n", (int)s);
        return 1;
    }
    printf("label subscription ended cleanly (%d labels)\n", g_label_count);
    return 0;
}

/* Print a raw JSON agent response, freeing resources, returning 0 on success.
 * On error prints to stderr and returns 1. */
static int finish_agent_response(wf_agent *agent, wf_status s, wf_response *res) {
    int rc = 0;
    if (s != WF_OK) {
        fprintf(stderr, "error: request failed (status %d)\n", (int)s);
        rc = 1;
    } else if (res->body && res->body_len > 0) {
        printf("%s\n", res->body);
    } else {
        printf("(empty response, HTTP %ld)\n", res->status);
    }
    wf_response_free(res);
    wf_agent_free(agent);
    return rc;
}

/* wolfram follows <service> <actor> [limit] */
static int cmd_follows(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_follows(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram followers <service> <actor> [limit] */
static int cmd_followers(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_followers(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram blocks <service> <handle> <password> [limit] */
static int cmd_blocks(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_blocks(agent, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram mutes <service> <handle> <password> [limit] */
static int cmd_mutes(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_mutes(agent, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram list <service> <list-uri> [limit] */
static int cmd_list(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *list_uri = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_list(agent, list_uri, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram lists <service> <actor> [limit] */
static int cmd_lists(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_lists(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-record <service> <handle> <password> <collection> <rkey> */
static int cmd_get_record(int argc, char **argv) {
    if (argc < 6) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *collection = argv[4];
    const char *rkey = argv[5];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_record(agent, collection, rkey, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram video upload <service> <handle> <password> <file.mp4>
 * wolfram video status <service> <handle> <password> <job-id> */
static int cmd_video(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "upload") == 0) {
        if (argc < 6) {
            usage_stream(stderr);
            return 0;
        }
        const char *service = argv[2];
        const char *handle = argv[3];
        const char *password = argv[4];
        const char *path = argv[5];

        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "error: cannot open '%s'\n", path);
            return 1;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            fprintf(stderr, "error: cannot seek '%s'\n", path);
            return 1;
        }
        long size = ftell(f);
        if (size < 0) {
            fclose(f);
            fprintf(stderr, "error: cannot stat '%s'\n", path);
            return 1;
        }
        rewind(f);
        void *buf = malloc((size_t)size > 0 ? (size_t)size : 1);
        if (!buf) {
            fclose(f);
            fprintf(stderr, "error: out of memory\n");
            return 1;
        }
        if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
            fclose(f);
            free(buf);
            fprintf(stderr, "error: failed to read '%s'\n", path);
            return 1;
        }
        fclose(f);

        wf_agent *agent = agent_login_or_err(service, handle, password);
        if (!agent) {
            free(buf);
            return 1;
        }

        wf_uploaded_blob blob = {0};
        wf_status s = wf_agent_upload_video(agent, buf, (size_t)size,
                                            "video/mp4", &blob);
        free(buf);
        if (s != WF_OK) {
            fprintf(stderr, "error: video upload failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }

        printf("uploaded: cid=%s mime=%s size=%zu\n",
               blob.cid ? blob.cid : "?",
               blob.mime_type ? blob.mime_type : "?",
               blob.size);
        wf_uploaded_blob_free(&blob);
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "status") == 0) {
        if (argc < 6) {
            usage_stream(stderr);
            return 0;
        }
        const char *service = argv[2];
        const char *handle = argv[3];
        const char *password = argv[4];
        const char *job_id = argv[5];

        wf_agent *agent = agent_login_or_err(service, handle, password);
        if (!agent) {
            return 1;
        }

        wf_response res = {0};
        wf_status s = wf_agent_get_video_job_status(agent, job_id, &res);
        return finish_agent_response(agent, s, &res);
    }

    fprintf(stderr, "error: unknown video subcommand '%s' (try 'upload' or 'status')\n",
            sub);
    return 1;
}

/* Return a heap-owned default path for the persisted OAuth pending-state file
 * (~/.wolfram_oauth_state.json), falling back to the cwd when $HOME is unset.
 * Caller frees. */
static char *oauth_default_state_path(void) {
    const char *home = getenv("HOME");
    const char *name = ".wolfram_oauth_state.json";
    if (!home) home = ".";
    size_t n = strlen(home) + 1 + strlen(name) + 1;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/%s", home, name);
    return p;
}

/* Return a heap-owned default path for the persisted OAuth session file
 * (~/.wolfram_session.json). Caller frees. */
static char *oauth_default_session_path(void) {
    const char *home = getenv("HOME");
    const char *name = ".wolfram_session.json";
    if (!home) home = ".";
    size_t n = strlen(home) + 1 + strlen(name) + 1;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/%s", home, name);
    return p;
}

/* Write `data` to `path` (text mode). Returns 0 on success, -1 on failure. */
static int write_text_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, strlen(data), f);
    fclose(f);
    return (n == strlen(data)) ? 0 : -1;
}

/* Read the entire text file `path` into a heap string (NUL terminated).
 * Returns NULL on failure. Caller frees. */
static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *data = malloc((size_t)len + 1);
    if (!data) { fclose(f); return NULL; }
    size_t n = fread(data, 1, (size_t)len, f);
    fclose(f);
    data[n] = '\0';
    return data;
}

/* Parse the query/fragment of a redirect URL into owned callback params.
 * Mirrors examples/oauth_session.c; the caller frees the strdup'd fields. */
static void parse_callback_url(const char *url, wf_oauth_callback_params *params) {
    memset(params, 0, sizeof(*params));
    const char *q = strchr(url, '?');
    if (!q) q = strchr(url, '#');
    if (!q) return;
    q++;
    while (*q) {
        const char *amp = strchr(q, '&');
        size_t pair_len = amp ? (size_t)(amp - q) : strlen(q);
        const char *eq = memchr(q, '=', pair_len);
        if (!eq) { q = amp ? amp + 1 : q + pair_len; continue; }
        size_t name_len = (size_t)(eq - q);
        size_t val_len = pair_len - name_len - 1;
        if (name_len == 5 && memcmp(q, "state", 5) == 0)
            params->state = strndup(eq + 1, val_len);
        else if (name_len == 4 && memcmp(q, "code", 4) == 0)
            params->code = strndup(eq + 1, val_len);
        else if (name_len == 3 && memcmp(q, "iss", 3) == 0)
            params->issuer = strndup(eq + 1, val_len);
        else if (name_len == 5 && memcmp(q, "error", 5) == 0)
            params->error = strndup(eq + 1, val_len);
        q = amp ? amp + 1 : q + pair_len;
    }
}

/* wolfram oauth-callback <service> --url <redirect> --state <state>
 *   [--state-file <path>] [--client-id <id>] [--redirect-uri <uri>]
 *   [--session <path>]
 *
 * Completes the OAuth flow begun by `oauth-login`: validates the callback
 * against the persisted pending state, exchanges the code for tokens, and
 * writes the resulting session to a file (default ~/.wolfram_session.json).
 * No HTTP callback server is run; the user pastes the redirect URL. */
static int cmd_oauth_callback(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = NULL;
    const char *url = NULL;
    const char *state = NULL;
    const char *state_file = NULL;
    const char *client_id = NULL;
    const char *redirect_uri = NULL;
    const char *session_path = NULL;

    char **pos = malloc(sizeof(char *) * (size_t)argc);
    if (!pos) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) url = argv[++i];
        else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) state = argv[++i];
        else if (strcmp(argv[i], "--state-file") == 0 && i + 1 < argc) state_file = argv[++i];
        else if (strcmp(argv[i], "--client-id") == 0 && i + 1 < argc) client_id = argv[++i];
        else if (strcmp(argv[i], "--redirect-uri") == 0 && i + 1 < argc) redirect_uri = argv[++i];
        else if (strcmp(argv[i], "--session") == 0 && i + 1 < argc) session_path = argv[++i];
        else pos[pi++] = argv[i];
    }
    if (pi >= 1) service = pos[0];
    free(pos);

    if (!service || !url || !state) {
        fprintf(stderr,
                "error: usage: wolfram oauth-callback <service> --url <redirect> "
                "--state <state> [--state-file <path>] [--client-id <id>] "
                "[--redirect-uri <uri>] [--session <path>]\n");
        return 1;
    }

    char *def_state = oauth_default_state_path();
    const char *sf = state_file ? state_file : def_state;
    char *state_json = read_text_file(sf);
    free(def_state);
    if (!state_json) {
        fprintf(stderr, "error: could not read pending state file '%s' "
                "(run oauth-login first)\n", sf);
        return 1;
    }

    if (!redirect_uri) redirect_uri = "https://localhost/callback";
    if (!client_id) client_id = redirect_uri;

    wf_xrpc_client *transport = wf_xrpc_client_new(service);
    if (!transport) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        free(state_json);
        return 1;
    }

    wf_oauth_resource_metadata resource = {0};
    wf_oauth_server_metadata server = {0};
    wf_status s = wf_oauth_discover(transport, service, &resource, &server);
    if (s != WF_OK) {
        fprintf(stderr, "error: OAuth discovery failed (status %d)\n", (int)s);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        free(state_json);
        return 1;
    }

    wf_oauth_client_metadata client = {0};
    s = wf_oauth_client_metadata_get(transport, client_id, &client);
    if (s != WF_OK) {
        fprintf(stderr, "error: client metadata fetch failed (status %d)\n", (int)s);
        wf_oauth_client_metadata_free(&client);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        free(state_json);
        return 1;
    }

    wf_oauth_client_auth client_auth = {
        .client_id = client_id,
        .authorization_server_issuer = server.issuer,
        .signing_key = NULL,
    };

    wf_oauth_callback_params cb = {0};
    parse_callback_url(url, &cb);

    wf_oauth_authorization_complete_result complete = {0};
    s = wf_oauth_authorization_complete(
        transport, &server, &client, &client_auth, &cb,
        state, state_json, strlen(state_json), redirect_uri, time(NULL), &complete);

    free((void *)cb.state); free((void *)cb.code);
    free((void *)cb.issuer); free((void *)cb.error);
    wf_oauth_client_metadata_free(&client);
    wf_oauth_resource_metadata_free(&resource);
    wf_oauth_server_metadata_free(&server);
    wf_xrpc_client_free(transport);
    free(state_json);

    if (s != WF_OK) {
        fprintf(stderr, "error: authorization complete failed (status %d)\n", (int)s);
        if (complete.error)
            fprintf(stderr, "server error: %s: %s\n",
                    complete.error,
                    complete.error_description ? complete.error_description : "");
        wf_oauth_authorization_complete_result_free(&complete);
        return 1;
    }

    char *def_session = oauth_default_session_path();
    const char *sp = session_path ? session_path : def_session;
    if (complete.session_json) {
        if (write_text_file(sp, complete.session_json) == 0)
            printf("session saved to %s\n", sp);
        else
            fprintf(stderr, "error: failed to write session file '%s'\n", sp);
    }
    wf_oauth_authorization_complete_result_free(&complete);
    free(def_session);
    return 0;
}

/* OAuth login demonstration — discover the authorization server for the
 * protected resource and begin a PAR flow, printing the authorization URL the
 * user must visit plus the flow state. No callback server is run. */
static int cmd_oauth_login(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = NULL;
    const char *handle = NULL;
    const char *client_id = NULL;
    const char *redirect_uri = "https://localhost/callback";
    const char *state_file = NULL;

    char **pos = malloc(sizeof(char *) * (size_t)argc);
    if (!pos) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--state-file") == 0 && i + 1 < argc) {
            state_file = argv[++i];
        } else {
            pos[pi++] = argv[i];
        }
    }
    if (pi < 2) {
        fprintf(stderr, "error: usage: wolfram oauth-login <service> <handle> "
                "[client-id] [redirect-uri] [--state-file <path>]\n");
        free(pos);
        return 1;
    }
    service = pos[0];
    handle = pos[1];
    if (pi >= 3) client_id = pos[2];
    if (pi >= 4) redirect_uri = pos[3];
    free(pos);

    wf_xrpc_client *transport = wf_xrpc_client_new(service);
    if (!transport) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        return 1;
    }

    wf_oauth_resource_metadata resource = {0};
    wf_oauth_server_metadata server = {0};
    wf_status s = wf_oauth_discover(transport, service, &resource, &server);
    if (s != WF_OK) {
        fprintf(stderr, "error: OAuth discovery failed (status %d)\n", (int)s);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        return 1;
    }

    printf("authorization server: %s\n",
           server.authorization_endpoint ? server.authorization_endpoint : "?");
    if (server.issuer) {
        printf("issuer: %s\n", server.issuer);
    }

    if (!client_id) {
        printf("\nOAuth discovery complete. Provide a client-id "
               "(and redirect-uri) to begin the PAR flow:\n"
               "  wolfram oauth-login %s %s <client-id> [redirect-uri] "
               "[--state-file <path>]\n",
               service, handle);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        return 0;
    }

    wf_oauth_client_metadata client = {0};
    s = wf_oauth_client_metadata_get(transport, client_id, &client);
    if (s != WF_OK) {
        fprintf(stderr, "error: client metadata fetch failed (status %d)\n",
                (int)s);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        return 1;
    }

    wf_oauth_client_auth client_auth = {
        .client_id = client_id,
        .authorization_server_issuer = server.issuer,
        .signing_key = NULL,
    };

    wf_oauth_authorization_begin_options opts = {
        .redirect_uri = redirect_uri,
        .scope = "atproto",
        .login_hint = handle,
        .now = time(NULL),
        .state_ttl = 600,
    };

    wf_oauth_authorization_begin_result begin = {0};
    s = wf_oauth_authorization_begin(transport, &server, &client,
                                     &client_auth, &opts, &begin);
    wf_oauth_client_metadata_free(&client);
    wf_oauth_resource_metadata_free(&resource);
    wf_oauth_server_metadata_free(&server);
    wf_xrpc_client_free(transport);

    if (s != WF_OK) {
        fprintf(stderr, "error: authorization begin failed (status %d)\n", (int)s);
        wf_oauth_authorization_begin_result_free(&begin);
        return 1;
    }

    /* Persist the pending authorization state so oauth-callback can finish the
     * flow. The state file holds the serialized PKCE/DPoP material; the printed
     * `state` is the opaque CSRF token the callback must echo back. */
    char *def_state = oauth_default_state_path();
    const char *sf = state_file ? state_file : def_state;
    if (begin.state_json) {
        if (write_text_file(sf, begin.state_json) == 0)
            printf("\npending state saved to %s\n", sf);
        else
            fprintf(stderr, "warning: failed to write state file '%s'\n", sf);
    }
    free(def_state);

    printf("\nOpen this URL in your browser to authorize:\n%s\n",
           begin.authorization_url ? begin.authorization_url : "(none)");
    printf("\nstate: %s\n", begin.state ? begin.state : "(none)");
    printf("\nAfter authorizing, run:\n"
           "  wolfram oauth-callback %s --url \"<redirect-url>\" --state %s%s%s\n",
           service, begin.state ? begin.state : "",
           state_file ? " --state-file " : "",
           state_file ? state_file : "");

    wf_oauth_authorization_begin_result_free(&begin);
    return 0;
}

/* wolfram block <service> <handle> <password> <actor> */
static int cmd_block(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_block(agent, did, &out);
    free(did);
    if (s != WF_OK) {
        fprintf(stderr, "error: block failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }
    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

/* wolfram unblock <service> <handle> <password> <actor> */
static int cmd_unblock(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_response res = {0};
    s = wf_agent_get_blocks(agent, 100, NULL, &res);
    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getBlocks failed (status %d)\n", (int)s);
        wf_response_free(&res);
        free(did);
        wf_agent_free(agent);
        return 1;
    }

    char *block_uri = NULL;
    if (res.body) {
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        if (root) {
            cJSON *blocks = cJSON_GetObjectItemCaseSensitive(root, "blocks");
            if (cJSON_IsArray(blocks)) {
                for (int i = 0; i < cJSON_GetArraySize(blocks); ++i) {
                    cJSON *b = cJSON_GetArrayItem(blocks, i);
                    cJSON *subj = cJSON_GetObjectItemCaseSensitive(b, "subject");
                    cJSON *sdid = subj
                        ? cJSON_GetObjectItemCaseSensitive(subj, "did") : NULL;
                    cJSON *uri = cJSON_GetObjectItemCaseSensitive(b, "uri");
                    if (cJSON_IsString(sdid) && cJSON_IsString(uri) &&
                        strcmp(sdid->valuestring, did) == 0) {
                        block_uri = strdup(uri->valuestring);
                        break;
                    }
                }
            }
            cJSON_Delete(root);
        }
    }
    wf_response_free(&res);
    free(did);

    if (!block_uri) {
        printf("(not currently blocking %s)\n", actor);
        wf_agent_free(agent);
        return 0;
    }

    s = wf_agent_unblock(agent, block_uri);
    free(block_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: unblock failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("unblocked %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

/* wolfram repo <sub> ...  (put-record / delete-record / list-records / describe) */
static int cmd_repo(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    /* wolfram repo put-record <service> <handle> <password> --collection <nsid>
     *   --rkey <rkey> --json <record|file> */
    if (strcmp(sub, "put-record") == 0) {
        const char *collection = NULL, *rkey = NULL, *json = NULL;
        const char *pos[3]; int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--collection") == 0 && i + 1 < argc) collection = argv[++i];
            else if (strcmp(argv[i], "--rkey") == 0 && i + 1 < argc) rkey = argv[++i];
            else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json = argv[++i];
            else if (pi < 3) pos[pi++] = argv[i];
        }
        if (pi < 3 || !collection || !rkey || !json) {
            fprintf(stderr, "error: usage: wolfram repo put-record <service> "
                    "<handle> <password> --collection <nsid> --rkey <rkey> "
                    "--json <record|file>\n");
            return 1;
        }
        const char *record_json = json;
        char *file_buf = NULL;
        if (json[0] != '{') {
            file_buf = read_text_file(json);
            if (!file_buf) {
                fprintf(stderr, "error: cannot read record file '%s'\n", json);
                return 1;
            }
            record_json = file_buf;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) { free(file_buf); return 1; }
        wf_session_data sd = {0};
        wf_agent_get_session_data(agent, &sd);
        const char *repo = sd.did ? sd.did : pos[1];
        wf_repo_write_record_result out = {0};
        wf_status s = wf_agent_put_record_typed(agent, repo, collection, rkey,
                                                -1, record_json, NULL, NULL, &out);
        free(file_buf);
        wf_agent_session_data_free(&sd);
        if (s != WF_OK) {
            fprintf(stderr, "error: putRecord failed (status %d)\n", (int)s);
            wf_repo_write_record_result_free(&out);
            wf_agent_free(agent);
            return 1;
        }
        printf("%s\n", out.uri ? out.uri : "(no uri returned)");
        wf_repo_write_record_result_free(&out);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram repo delete-record <service> <handle> <password> --collection <nsid>
     *   --rkey <rkey> */
    if (strcmp(sub, "delete-record") == 0) {
        const char *collection = NULL, *rkey = NULL;
        const char *pos[3]; int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--collection") == 0 && i + 1 < argc) collection = argv[++i];
            else if (strcmp(argv[i], "--rkey") == 0 && i + 1 < argc) rkey = argv[++i];
            else if (pi < 3) pos[pi++] = argv[i];
        }
        if (pi < 3 || !collection || !rkey) {
            fprintf(stderr, "error: usage: wolfram repo delete-record <service> "
                    "<handle> <password> --collection <nsid> --rkey <rkey>\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        wf_session_data sd = {0};
        wf_agent_get_session_data(agent, &sd);
        const char *repo = sd.did ? sd.did : pos[1];
        wf_status s = wf_agent_delete_record_typed(agent, repo, collection, rkey,
                                                   NULL, NULL);
        wf_agent_session_data_free(&sd);
        if (s != WF_OK) {
            fprintf(stderr, "error: deleteRecord failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("deleted %s/%s/%s\n", repo, collection, rkey);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram repo list-records <service> <handle> <password> --collection <nsid>
     *   [--limit N] [--cursor C] */
    if (strcmp(sub, "list-records") == 0) {
        const char *collection = NULL, *cursor = NULL;
        int limit = 50;
        const char *pos[3]; int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--collection") == 0 && i + 1 < argc) collection = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) cursor = argv[++i];
            else if (pi < 3) pos[pi++] = argv[i];
        }
        if (pi < 3 || !collection) {
            fprintf(stderr, "error: usage: wolfram repo list-records <service> "
                    "<handle> <password> --collection <nsid> [--limit N] "
                    "[--cursor C]\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        wf_session_data sd = {0};
        wf_agent_get_session_data(agent, &sd);
        const char *repo = sd.did ? sd.did : pos[1];
        if (g_json) {
            wf_response res = {0};
            wf_status s = wf_agent_list_records(agent, collection, limit, cursor, &res);
            wf_agent_session_data_free(&sd);
            return finish_agent_response(agent, s, &res);
        }
        wf_repo_record_list list = {0};
        wf_status s = wf_agent_list_records_typed(agent, repo, collection, limit,
                                                  cursor, 0, &list);
        wf_agent_session_data_free(&sd);
        if (s != WF_OK) {
            fprintf(stderr, "error: listRecords failed (status %d)\n", (int)s);
            wf_repo_record_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        for (size_t i = 0; i < list.count; ++i) {
            const wf_repo_record *r = &list.items[i];
            printf("%s (cid=%s)\n", r->uri ? r->uri : "?",
                   r->has_cid && r->cid ? r->cid : "?");
        }
        if (list.cursor) printf("cursor: %s\n", list.cursor);
        if (list.count == 0) printf("(no records)\n");
        wf_repo_record_list_free(&list);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram repo describe <service> <handle> <password> --repo <did-or-handle> */
    if (strcmp(sub, "describe") == 0) {
        const char *repo = NULL;
        const char *pos[3]; int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--repo") == 0 && i + 1 < argc) repo = argv[++i];
            else if (pi < 3) pos[pi++] = argv[i];
        }
        if (pi < 3 || !repo) {
            fprintf(stderr, "error: usage: wolfram repo describe <service> "
                    "<handle> <password> --repo <did-or-handle>\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        if (g_json) {
            wf_response res = {0};
            wf_status s = wf_agent_describe_repo(agent, repo, &res);
            return finish_agent_response(agent, s, &res);
        }
        wf_repo_description desc = {0};
        wf_status s = wf_agent_describe_repo_typed(agent, repo, &desc);
        if (s != WF_OK) {
            fprintf(stderr, "error: describeRepo failed (status %d)\n", (int)s);
            wf_repo_description_free(&desc);
            wf_agent_free(agent);
            return 1;
        }
        printf("handle: %s\n", desc.handle ? desc.handle : "?");
        printf("did: %s\n", desc.did ? desc.did : "?");
        printf("handleIsCorrect: %d\n", desc.handle_is_correct);
        printf("collections (%zu):\n", desc.collection_count);
        for (size_t i = 0; i < desc.collection_count; ++i) {
            printf("  %s\n", desc.collections[i] ? desc.collections[i] : "?");
        }
        wf_repo_description_free(&desc);
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr, "error: unknown repo subcommand '%s' "
            "(try put-record/delete-record/list-records/describe)\n", sub);
    return 1;
}

/* wolfram feed <sub> ...  (get / author) */
static int cmd_feed(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    /* wolfram feed get <service> <handle> <password> --feed <generator-uri>
     *   [--limit N] [--cursor C] */
    if (strcmp(sub, "get") == 0) {
        const char *feed = NULL, *cursor = NULL;
        int limit = 50;
        const char *pos[3]; int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--feed") == 0 && i + 1 < argc) feed = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) cursor = argv[++i];
            else if (pi < 3) pos[pi++] = argv[i];
        }
        if (pi < 3 || !feed) {
            fprintf(stderr, "error: usage: wolfram feed get <service> <handle> "
                    "<password> --feed <generator-uri> [--limit N] [--cursor C]\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        if (g_json) {
            wf_response res = {0};
            wf_status s = wf_agent_get_feed(agent, feed, limit, cursor, &res);
            return finish_agent_response(agent, s, &res);
        }
        wf_agent_feed_view_list list = {0};
        wf_status s = wf_agent_get_feed_typed(agent, feed, limit, cursor, &list);
        if (s != WF_OK) {
            fprintf(stderr, "error: getFeed failed (status %d)\n", (int)s);
            wf_agent_feed_view_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        for (size_t i = 0; i < list.item_count; ++i) {
            const wf_agent_post_view *post = &list.items[i].post;
            const char *author = post->author.handle
                ? post->author.handle
                : (post->author.did ? post->author.did : "?");
            const char *text = "";
            if (post->record) {
                cJSON *t = cJSON_GetObjectItemCaseSensitive(post->record, "text");
                if (cJSON_IsString(t) && t->valuestring) text = t->valuestring;
            }
            printf("%s: %s\n", author, text);
        }
        if (list.cursor) printf("cursor: %s\n", list.cursor);
        if (list.item_count == 0) printf("(empty feed)\n");
        wf_agent_feed_view_list_free(&list);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram feed author <service> <handle> <password> --actor <handle-or-did>
     *   [--limit N] [--cursor C] */
    if (strcmp(sub, "author") == 0) {
        const char *actor = NULL, *cursor = NULL;
        int limit = 50;
        const char *pos[3]; int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--actor") == 0 && i + 1 < argc) actor = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) cursor = argv[++i];
            else if (pi < 3) pos[pi++] = argv[i];
        }
        if (pi < 3 || !actor) {
            fprintf(stderr, "error: usage: wolfram feed author <service> <handle> "
                    "<password> --actor <handle-or-did> [--limit N] [--cursor C]\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        if (g_json) {
            wf_response res = {0};
            wf_status s = wf_agent_get_author_feed(agent, actor, limit, cursor,
                                                   NULL, false, &res);
            return finish_agent_response(agent, s, &res);
        }
        wf_agent_feed_list list = {0};
        wf_status s = wf_agent_get_author_feed_typed(agent, actor, limit, cursor,
                                                     NULL, &list);
        if (s != WF_OK) {
            fprintf(stderr, "error: getAuthorFeed failed (status %d)\n", (int)s);
            wf_agent_feed_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        for (size_t i = 0; i < list.item_count; ++i) {
            const wf_agent_post_view *post = &list.items[i].post;
            const char *author = post->author.handle
                ? post->author.handle
                : (post->author.did ? post->author.did : "?");
            const char *text = "";
            if (post->record) {
                cJSON *t = cJSON_GetObjectItemCaseSensitive(post->record, "text");
                if (cJSON_IsString(t) && t->valuestring) text = t->valuestring;
            }
            printf("%s: %s\n", author, text);
        }
        if (list.cursor) printf("cursor: %s\n", list.cursor);
        if (list.item_count == 0) printf("(empty feed)\n");
        wf_agent_feed_list_free(&list);
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr, "error: unknown feed subcommand '%s' (try get/author)\n", sub);
    return 1;
}

/* ----------------------------------------------------------------- */
/* Dispatch                                                          */
/* ----------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage_exit();
    }

    /* Strip a leading global --json flag (and any others) so the command and
     * its positional arguments remain contiguous at argv[1..]. */
    {
        int out = 1;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--json") == 0) {
                g_json = true;
                continue;
            }
            argv[out++] = argv[i];
        }
        argc = out;
    }

    const char *cmd = argv[1];

    /* --help / -h / help print usage to stdout and exit 0 — without touching
     * the network. Genuinely unknown commands still print to stderr below. */
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        return usage_exit();
    }

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("%s\n", WOLFRAM_VERSION_STRING);
        return 0;
    }

    if (strcmp(cmd, "help") == 0) {
        if (argc >= 3) {
            cmd_help_stream(stdout, argv[2]);
        } else {
            usage_stream(stdout);
        }
        return 0;
    }

    int rest = argc - 1; /* arguments after the program name */

    /* Re-base argv so handlers see <service> as argv[1] etc. */
    char **cargv = argv + 1;

    if (strcmp(cmd, "login") == 0) {
        return cmd_login(rest, cargv);
    }
    if (strcmp(cmd, "post") == 0) {
        return cmd_post(rest, cargv);
    }
    if (strcmp(cmd, "reply") == 0) {
        return cmd_reply(rest, cargv);
    }
    if (strcmp(cmd, "like") == 0) {
        return cmd_like(rest, cargv);
    }
    if (strcmp(cmd, "unlike") == 0) {
        return cmd_unlike(rest, cargv);
    }
    if (strcmp(cmd, "repost") == 0) {
        return cmd_repost(rest, cargv);
    }
    if (strcmp(cmd, "delete-repost") == 0 || strcmp(cmd, "unrepost") == 0) {
        return cmd_delete_repost(rest, cargv);
    }
    if (strcmp(cmd, "labels") == 0) {
        return cmd_labels(rest, cargv);
    }
    if (strcmp(cmd, "oauth-login") == 0) {
        return cmd_oauth_login(rest, cargv);
    }
    if (strcmp(cmd, "oauth-callback") == 0) {
        return cmd_oauth_callback(rest, cargv);
    }
    if (strcmp(cmd, "timeline") == 0) {
        return cmd_timeline(rest, cargv);
    }
    if (strcmp(cmd, "get-post") == 0) {
        return cmd_get_post(rest, cargv);
    }
    if (strcmp(cmd, "profile") == 0) {
        return cmd_profile(rest, cargv);
    }
    if (strcmp(cmd, "follow") == 0) {
        return cmd_follow(rest, cargv);
    }
    if (strcmp(cmd, "unfollow") == 0) {
        return cmd_unfollow(rest, cargv);
    }
    if (strcmp(cmd, "resolve") == 0) {
        return cmd_resolve(rest, cargv);
    }
    if (strcmp(cmd, "thread") == 0) {
        return cmd_thread(rest, cargv);
    }
    if (strcmp(cmd, "notifications") == 0) {
        return cmd_notifications(rest, cargv);
    }
    if (strcmp(cmd, "search") == 0) {
        return cmd_search(rest, cargv);
    }
    if (strcmp(cmd, "moderation") == 0) {
        return cmd_moderation(rest, cargv);
    }
    if (strcmp(cmd, "follows") == 0) {
        return cmd_follows(rest, cargv);
    }
    if (strcmp(cmd, "followers") == 0) {
        return cmd_followers(rest, cargv);
    }
    if (strcmp(cmd, "blocks") == 0) {
        return cmd_blocks(rest, cargv);
    }
    if (strcmp(cmd, "mutes") == 0) {
        return cmd_mutes(rest, cargv);
    }
    if (strcmp(cmd, "mute") == 0) {
        return cmd_mute(rest, cargv);
    }
    if (strcmp(cmd, "unmute") == 0) {
        return cmd_unmute(rest, cargv);
    }
    if (strcmp(cmd, "list") == 0) {
        return cmd_list(rest, cargv);
    }
    if (strcmp(cmd, "lists") == 0) {
        return cmd_lists(rest, cargv);
    }
    if (strcmp(cmd, "get-record") == 0) {
        return cmd_get_record(rest, cargv);
    }
    if (strcmp(cmd, "video") == 0) {
        return cmd_video(rest, cargv);
    }
    if (strcmp(cmd, "block") == 0) {
        return cmd_block(rest, cargv);
    }
    if (strcmp(cmd, "unblock") == 0) {
        return cmd_unblock(rest, cargv);
    }
    if (strcmp(cmd, "repo") == 0) {
        return cmd_repo(rest, cargv);
    }
    if (strcmp(cmd, "feed") == 0) {
        return cmd_feed(rest, cargv);
    }

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage_stream(stderr);
    return 0;
}
