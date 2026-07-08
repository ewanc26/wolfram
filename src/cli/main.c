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
 *   wolfram labels      <service> [cursor]
 *   wolfram moderation  <service> <actor> [labeler-did]
 *   wolfram oauth-login <service> <handle> [client-id] [redirect-uri]
 *   wolfram describe-server <service>
 *   wolfram whoami      <service> <handle> <password>
 *   wolfram help [command]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include <inttypes.h>
#include <time.h>

#include "wolfram/agent.h"
#include "wolfram/identity.h"
#include "wolfram/label.h"
#include "wolfram/oauth.h"
#include "wolfram/server.h"
#include "wolfram/syntax.h"
#include "wolfram/version.h"
#include "wolfram/xrpc.h"
#include "wolfram/thread_typed.h"
#include "wolfram/moderation.h"

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
         "    mute             <service> <handle> <password> <actor>\n"
         "    unmute           <service> <handle> <password> <actor>\n"
         "\n"
         "  Identity & moderation:\n"
         "    resolve          <service> <handle-or-did>\n"
         "    labels           <service> [cursor]\n"
         "    moderation       <service> <actor> [labeler-did]\n"
         "    oauth-login      <service> <handle> [client-id] [redirect-uri]\n"
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
        {"reply",            "reply <service> <handle> <password> <at-uri> <text...>",
         "Reply to an existing post. The parent post's CID and root reference are resolved via getPosts."},
        {"delete",           "delete <service> <handle> <password> <at-uri>",
         "Delete a post created by the authenticated user."},
        {"like",             "like <service> <handle> <password> <at-uri>",
         "Like a post. The post's CID is resolved via getPosts, then wf_agent_like is called."},
        {"unlike",            "unlike <service> <handle> <password> <like-at-uri>",
         "Delete a like record by its at:// URI via wf_agent_unlike."},
        {"repost",           "repost <service> <handle> <password> <at-uri>",
         "Repost a post. The post's CID is resolved via getPosts, then wf_agent_repost is called."},
        {"delete-repost",    "delete-repost <service> <handle> <password> <repost-at-uri>",
         "Delete a repost record by its at:// URI via wf_agent_delete_repost."},
        {"unrepost",         "delete-repost <service> <handle> <password> <repost-at-uri>",
         "Alias for delete-repost: remove a repost record by its at:// URI."},
        {"reply",            "reply <service> <handle> <password> <parent-at-uri> <text...>",
         "Reply to a post. The parent CID and root ref are resolved via getPosts and a reply record is created via wf_agent_create_record."},
        {"labels",           "labels <service> [cursor]",
         "Subscribe to com.atproto.label.subscribeLabels via the label.h streaming API and print each arriving label."},
        {"oauth-login",      "oauth-login <service> <handle> [client-id] [redirect-uri]",
         "Demonstrate the OAuth login path: discover the authorization server and begin a PAR flow (wf_oauth_authorization_begin), printing the authorization URL and state."},
        {"timeline",         "timeline <service> <handle> <password> [pages]",
         "Fetch the authenticated user's home timeline. pages=0 (default) fetches until exhausted."},
        {"get-post",         "get-post <service> <at-uri>",
         "Fetch a single record via com.atproto.repo.getRecord (no auth)."},
        {"profile",          "profile <service> <actor>",
         "Fetch and display an actor's profile."},
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
         "Resolve a handle to a DID via com.atproto.identity.resolveHandle. A DID is echoed back unchanged."},
        {"labels",           "labels <actor-or-did>",
         "Query labels for an actor's records via com.atproto.label.queryLabels."},
        {"moderation",       "moderation <service> <actor> [labeler-did]",
         "Run the moderation decision engine on an actor's profile."},
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
 * print each arriving label. The subscription blocks until interrupted; a
 * small event cap stops the live subscription cleanly via the handle pointer
 * trick (mirrors examples/subscribe_labels.c). */
#define CLI_LABEL_MAX_EVENTS 20

static int g_label_count = 0;
static wf_label_subscribe_handle **g_label_handle_ptr = NULL;

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
    if (g_label_handle_ptr && *g_label_handle_ptr &&
        g_label_count >= CLI_LABEL_MAX_EVENTS) {
        wf_label_subscribe_stop(*g_label_handle_ptr);
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

static int cmd_labels(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];

    int64_t cursor = 0;
    int has_cursor = 0;
    if (argc >= 3) {
        cursor = (int64_t)strtoll(argv[2], NULL, 10);
        has_cursor = 1;
    }

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

    printf("subscribing to label stream at %s (cursor=%s, max %d events)\n",
           service, has_cursor ? argv[2] : "none", CLI_LABEL_MAX_EVENTS);

    wf_label_subscribe_handle *handle = NULL;
    g_label_handle_ptr = &handle;
    wf_status s = wf_label_subscribe_start(&opts, &handle);
    g_label_handle_ptr = NULL;

    if (s != WF_OK) {
        fprintf(stderr, "error: label subscription ended (status %d)\n", (int)s);
        return 1;
    }
    printf("label subscription ended cleanly\n");
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
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *client_id = (argc >= 4) ? argv[3] : NULL;
    const char *redirect_uri = (argc >= 5) ? argv[4] : "https://localhost/callback";

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
               "  wolfram oauth-login %s %s <client-id> [redirect-uri]\n",
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

    printf("\nOpen this URL in your browser to authorize:\n%s\n",
           begin.authorization_url ? begin.authorization_url : "(none)");
    printf("\nstate: %s\n", begin.state ? begin.state : "(none)");

    wf_oauth_authorization_begin_result_free(&begin);
    return 0;
}

/* ----------------------------------------------------------------- */
/* Dispatch                                                          */
/* ----------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage_exit();
    }

    const char *cmd = argv[1];

    /* --help / -h / help print usage to stdout and exit 0 — without touching
     * the network. Genuinely unknown commands still print to stderr below. */
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        return usage_exit();
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
    if (strcmp(cmd, "labels") == 0) {
        return cmd_labels(rest, cargv);
    }
    if (strcmp(cmd, "moderation") == 0) {
        return cmd_moderation(rest, cargv);
    }

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage_stream(stderr);
    return 0;
}
