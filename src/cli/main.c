/*
 * main.c — the `wolf` command-line client.
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
 *   wolf login       <service> <handle> <password>
 *   wolf post        <service> <handle> <password> <text...>
 *   wolf reply       <service> <handle> <password> <parent-at-uri> <text...>
 *   wolf timeline    <service> <handle> <password> [pages]
 *   wolf get-post    <service> <at-uri>
 *   wolf get-record  <service> <handle> <password> <collection> <rkey>
 *   wolf profile     <service> <actor>
 *   wolf follow      <service> <handle> <password> <actor>
 *   wolf unfollow    <service> <handle> <password> <actor>
 *   wolf like        <service> <handle> <password> <at-uri>
 *   wolf unlike      <service> <handle> <password> <like-at-uri>
 *   wolf repost      <service> <handle> <password> <at-uri>
 *   wolf delete-repost <service> <handle> <password> <repost-at-uri>
 *   wolf delete      <service> <handle> <password> <at-uri>
 *   wolf mute        <service> <handle> <password> <actor>
 *   wolf unmute      <service> <handle> <password> <actor>
 *   wolf search      <service> <handle> <password> <query> [limit]
 *   wolf resolve     <service> <handle-or-did>
 *   wolf thread      <service> <handle> <password> <at-uri> [depth]
 *   wolf notifications <service> <handle> <password> [limit]
 *   wolf follows     <service> <actor> [limit]
 *   wolf followers   <service> <actor> [limit]
 *   wolf blocks      <service> <handle> <password> [limit]
 *   wolf mutes       <service> <handle> <password> [limit]
 *   wolf list        <service> <list-uri> [limit]
 *   wolf lists       <service> <actor> [limit]
 *   wolf labels subscribe <service> [--cursor N] [--seconds N]
 *   wolf video upload <service> <handle> <password> <file.mp4>
 *   wolf video status <service> <handle> <password> <job-id>
 *   wolf moderation  <service> <actor> [labeler-did]
 *   wolf oauth-login <service> <handle> [client-id] [redirect-uri]
 *   wolf describe-server <service>
 *   wolf whoami      <service> <handle> <password>
 *   wolf help [command]
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
#include "wolfram/identity_typed.h"
#include "wolfram/label.h"
#include "wolfram/oauth.h"
#include "wolfram/server.h"
#include "wolfram/server_typed.h"
#include "wolfram/syntax.h"
#include "wolfram/xrpc.h"
#include "wolfram/thread_typed.h"
#include "wolfram/feed_typed.h"
#include "wolfram/actor_typed.h"
#include "wolfram/moderation.h"
#include "wolfram/repo_typed.h"
#include "wolfram/feedgen_typed.h"
#include "wolfram/notification_typed.h"
#include "wolfram/moderation_report_typed.h"
#include "wolfram/temp_typed.h"
#include "wolfram/ozone_typed.h"
#include "wolfram/sync_typed.h"
#include "wolfram/jetstream.h"
#include "wolfram/unspecced_typed.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/graph_typed.h"
#include "wolfram/graph_social_typed.h"
#include "wolfram/graph_write.h"
#include "wolfram/chat_typed.h"
#include "wolfram/actor_prefs_typed.h"
#include "wolfram/admin_typed.h"

#include "main_internal.h"
#include "cli_oauth.h"
#include "cli_auth.h"
#include "cli_post.h"
#include "cli_thread.h"
#include "cli_social.h"
#include "cli_search.h"
#include "cli_feed.h"
#include "cli_graph.h"
#include "cli_server.h"
#include "cli_identity.h"
#include "cli_sync.h"
#include "cli_prefs.h"
#include "cli_chat.h"
#include "cli_stream.h"
#include "cli_repo.h"
#include "cli_video.h"
#include "cli_ozone.h"
#include "wolfram/thread_typed.h"
#include "wolfram/feed_typed.h"
#include "wolfram/actor_typed.h"
#include "wolfram/moderation.h"
#include "wolfram/repo_typed.h"
#include "wolfram/feedgen_typed.h"
#include "wolfram/notification_typed.h"
#include "wolfram/moderation_report_typed.h"
#include "wolfram/temp_typed.h"

/* Global --json flag: when set, list/get commands print the raw JSON body
 * instead of human-readable text. Parsed in main() before dispatch. */
bool g_json = false;

/* ----------------------------------------------------------------- */
/* Usage                                                             */
/* ----------------------------------------------------------------- */

void usage_stream(FILE *out) {
    fprintf(
         out,
         "wolf %s — a command-line client for the AT Protocol (via wolfram "
         "SDK)\n\n"
         "usage: wolf <command> [args...]\n"
         "       wolf help <command>   — per-command help\n"
         "       wolf --version        — print version and exit\n\n"
         "commands:\n"
         "\n"
         "  Session & account:\n"
         "    login            <service> <handle> <password>\n"
         "    whoami           <service> <handle> <password>\n"
         "    describe-server  <service>\n"
         "\n"
         "  Posting & interaction:\n"
         "    post             <service> <handle> <password> <text...>\n"
         "    reply            <service> <handle> <password> <parent-at-uri> "
         "<text...>\n"
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
         "    get-profiles     <service> <actor>...\n"
         "    search           <service> <handle> <password> <query> [limit]\n"
         "    search-actors    <service> <handle> <password> <query> [limit]\n"
         "    search-typeahead <service> <handle> <password> <query> [limit]\n"
         "    search-starter-packs <service> <handle> <password> <query> [limit]\n"
         "    thread           <service> <handle> <password> <at-uri> [depth]\n"
         "    notifications    <service> <handle> <password> [limit]\n"
         "    unread-count     <service> <handle> <password>\n"
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
         "    resolve                   <service> <handle-or-did>\n"
         "    revoke-account-credentials <service> <handle> <password> "
         "<account>\n"
         "    labels subscribe          <service> [--cursor N] [--seconds N]\n"
         "    moderation       <service> <actor> [labeler-did]\n"
         "    moderation report <service> <handle> <password> --subject <uri> "
         "--reason <reason> [--reason-type <type>] [--cid <cid>]\n"
         "    oauth-login      <service> <handle> [client-id] [redirect-uri] "
         "[--state-file <path>]\n"
         "    oauth-callback   <service> --url <redirect> --state <state> "
         "[--state-file <path>] [--client-id <id>] [--redirect-uri <uri>] "
         "[--session <path>]\n"
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
         "    get-record       <service> <handle> <password> <collection> "
         "<rkey>\n"
         "    repo put-record  <service> <handle> <password> --collection "
         "<nsid> --rkey <rkey> --json <record|file>\n"
         "    repo delete-record <service> <handle> <password> --collection "
         "<nsid> --rkey <rkey>\n"
         "    repo list-records <service> <handle> <password> --collection "
         "<nsid> [--limit N] [--cursor C]\n"
         "    repo describe     <service> <handle> <password> --repo "
         "<did-or-handle>\n"
         "    video upload     <service> <handle> <password> <file.mp4>\n"
         "    video status     <service> <handle> <password> <job-id>\n"
         "\n"
         "  Feed & discovery:\n"
         "    feed get         <service> <handle> <password> --feed "
         "<generator-uri> [--limit N] [--cursor C]\n"
         "    feed author      <service> <handle> <password> --actor "
         "<handle-or-did> [--limit N] [--cursor C]\n"
         "    get-actor-feeds  <service> <handle> <password> --actor "
         "<handle-or-did> [--limit N] [--cursor C]\n"
         "    get-actor-likes  <service> <handle> <password> --actor "
         "<handle-or-did> [--limit N] [--cursor C]\n"
         "    get-likes        <service> <handle> <password> <at-uri> "
         "[--limit N] [--cursor C]\n"
         "    get-reposted-by  <service> <handle> <password> <at-uri> "
         "[--limit N] [--cursor C]\n"
         "    get-quotes       <service> <handle> <password> <at-uri> "
         "[--limit N] [--cursor C]\n"
         "    describe-feed    <service> <handle> <password> --feed "
         "<generator-uri>\n"
         "    get-suggested-feeds <service> <handle> <password>\n"
         "    get-suggestions  <service> <handle> <password> [limit]\n"
         "    known-followers  <service> <handle> <password> <actor>\n"
         "    relationships    <service> <handle> <password> <actor> "
         "<other>...\n"
         "    get-actor-status <service> <actor>\n"
         "    get-feed-generators <service> <feed-uri>...\n"
         "    get-suggested-follows-by-actor <service> <actor>\n"
         "    age-assurance    <service> <handle> <password> "
         "<begin|get-config|get-state>\n"
         "\n"
         "  Ozone moderation:\n"
         "    ozone moderation query-statuses <service> <handle> <password> "
         "[--limit N] [--cursor C]\n"
         "    ozone moderation get-suggestions <service> <handle> <password> "
         "[--ignore-subjects uri]... [--limit N]\n"
         "    ozone moderation get-label-definitions <service> <handle> "
         "<password> [--uri uri]...\n"
         "    ozone team list-members <service> <handle> <password> "
         "[--limit N] [--cursor C]\n"
         "    ozone server get-config <service> <handle> <password>\n"
         "    ozone communication list-templates <service> <handle> "
         "<password>\n"
         "    ozone setting list-options <service> <handle> <password> "
         "[--key key] [--scope scope] [--limit N]\n"
         "    ozone signature search-accounts <service> <handle> <password> "
         "<did> [--limit N]\n"
         "    ozone report query-reports <service> <handle> <password> "
         "[--limit N] [--cursor C]\n"
         "    ozone queue list-queues <service> <handle> <password> "
         "[--limit N] [--cursor C]\n"
         "\n"
         "  Admin:\n"
         "    admin search-accounts <service> <handle> <password> [email] "
         "[--limit N]\n"
         "    admin get-account-info <service> <handle> <password> <did>\n"
         "    admin get-subject-status <service> <handle> <password> <did>\n"
         "    admin get-invite-codes <service> <handle> <password> "
         "[--create N] [--use-count N] [--cursor C]\n"
         "    admin delete-account <service> <handle> <password> <did>\n"
         "    admin disable-account-invites <service> <handle> <password>\n"
         "    admin enable-account-invites <service> <handle> <password>\n"
         "    admin send-email <service> <handle> <password> <recipient-did> "
         "<subject> <body>\n"
         "\n"
         "  Global options:\n"
         "    --json           print raw JSON for list/get commands\n"
         "\n"
         "  <at-uri> is an at:// URI (e.g. "
         "at://did:plc:xxx/app.bsky.feed.post/rkey)\n"
         "  <actor> is a handle or DID\n",
         WOLFRAM_VERSION_STRING);
}

/* Per-command help text. */
static void cmd_help_stream(FILE *out, const char *cmd) {
    if (!cmd) return;

    struct {
        const char *name;
        const char *usage;
        const char *desc;
    } cmds[] = {
        {"login", "login <service> <handle> <password>",
         "Create a session via com.atproto.server.createSession."},
        {"whoami", "whoami <service> <handle> <password>",
         "Log in and print the current session DID and handle."},
        {"describe-server", "describe-server <service>",
         "Fetch server metadata via com.atproto.server.describeServer (no "
         "auth)."},
        {"post", "post <service> <handle> <password> <text...>",
         "Create a new post. Rich-text facets (mentions, links, tags) are "
         "auto-detected."},
        {"reply",
         "reply <service> <handle> <password> <parent-at-uri> <text...>",
         "Reply to a post. The parent CID and root ref are resolved via "
         "getPosts and a reply record is created via wf_agent_create_record."},
        {"delete", "delete <service> <handle> <password> <at-uri>",
         "Delete a post created by the authenticated user."},
        {"like", "like <service> <handle> <password> <at-uri>",
         "Like a post. The post's CID is resolved via getPosts, then "
         "wf_agent_like is called."},
        {"unlike", "unlike <service> <handle> <password> <like-at-uri>",
         "Delete a like record by its at:// URI via wf_agent_unlike."},
        {"repost", "repost <service> <handle> <password> <at-uri>",
         "Repost a post. The post's CID is resolved via getPosts, then "
         "wf_agent_repost is called."},
        {"delete-repost",
         "delete-repost <service> <handle> <password> <repost-at-uri>",
         "Delete a repost record by its at:// URI via wf_agent_delete_repost."},
        {"unrepost", "unrepost <service> <handle> <password> <repost-at-uri>",
         "Alias for delete-repost: remove a repost record by its at:// URI."},
        {"timeline", "timeline <service> <handle> <password> [pages]",
         "Fetch the authenticated user's home timeline. pages=0 (default) "
         "fetches until exhausted."},
        {"get-post", "get-post <service> <at-uri>",
         "Fetch a single record via com.atproto.repo.getRecord (no auth)."},
        {"profile", "profile <service> <actor>",
         "Fetch and display an actor's profile via wf_agent_get_profile."},
        {"search", "search <service> <handle> <password> <query> [limit]",
         "Search posts via app.bsky.feed.searchPosts. limit defaults to 25."},
        {"thread", "thread <service> <handle> <password> <at-uri> [depth]",
         "Fetch and display a post thread tree. depth defaults to 6."},
        {"notifications", "notifications <service> <handle> <password> [limit]",
         "List recent notifications. limit defaults to 50."},
        {"follow", "follow <service> <handle> <password> <actor>",
         "Follow an actor (handle or DID)."},
        {"unfollow", "unfollow <service> <handle> <password> <actor>",
         "Unfollow an actor by looking up and deleting the follow record."},
        {"mute", "mute <service> <handle> <password> <actor>",
         "Mute an actor (handle or DID)."},
        {"unmute", "unmute <service> <handle> <password> <actor>",
         "Unmute an actor (handle or DID)."},
        {"resolve", "resolve <service> <handle-or-did>",
         "Resolve a handle to a DID via wf_handle_resolve. A DID is echoed "
         "back unchanged."},
        {"labels", "labels subscribe <service> [--cursor N] [--seconds N]",
         "Subscribe to com.atproto.label.subscribeLabels via the label.h "
         "streaming API and print each arriving label. Bounded by --seconds "
         "(default 30) or SIGINT."},
        {"moderation", "moderation <service> <actor> [labeler-did]",
         "Run the moderation decision engine on an actor's profile."},
        {"oauth-login",
         "oauth-login <service> <handle> [client-id] [redirect-uri]",
         "Demonstrate the OAuth login path: discover the authorization server "
         "and begin a PAR flow, printing the authorization URL and state."},
        {"follows", "follows <service> <actor> [limit]",
         "List the accounts an actor follows via wf_agent_get_follows."},
        {"followers", "followers <service> <actor> [limit]",
         "List an actor's followers via wf_agent_get_followers."},
        {"blocks", "blocks <service> <handle> <password> [limit]",
         "List the accounts the authenticated user blocks via "
         "wf_agent_get_blocks."},
        {"mutes", "mutes <service> <handle> <password> [limit]",
         "List the accounts the authenticated user mutes via "
         "wf_agent_get_mutes."},
        {"list", "list <service> <list-uri> [limit]",
         "Fetch the items of a list via wf_agent_get_list."},
        {"lists", "lists <service> <actor> [limit]",
         "List the lists an actor owns via wf_agent_get_lists."},
        {"get-record",
         "get-record <service> <handle> <password> <collection> <rkey>",
         "Fetch a single record via wf_agent_get_record and print the JSON."},
        {"video", "video upload <service> <handle> <password> <file.mp4>",
         "Upload a video blob via wf_agent_upload_video and report the blob "
         "CID and size."},
        {"video", "video status <service> <handle> <password> <job-id>",
         "Poll a video processing job via wf_agent_get_video_job_status."},
        {"oauth-login",
         "oauth-login <service> <handle> [client-id] [redirect-uri] "
         "[--state-file <path>]",
         "Discover the authorization server, begin a PAR flow, and save the "
         "pending state. Prints the authorization URL and the opaque state "
         "token."},
        {"oauth-callback",
         "oauth-callback <service> --url <redirect> --state <state> "
         "[--state-file <path>] [--client-id <id>] [--redirect-uri <uri>] "
         "[--session <path>]",
         "Complete the OAuth flow: validate the callback, exchange the code "
         "for tokens, and persist the session (default "
         "~/.wolf_session.json)."},
        {"block", "block <service> <handle> <password> <actor>",
         "Block an actor (handle or DID) via wf_agent_block."},
        {"unblock", "unblock <service> <handle> <password> <actor>",
         "Unblock an actor by finding and deleting the block record via "
         "wf_agent_unblock."},
        {"notifications",
         "notifications update-seen [--seen-at <iso>] <service> <handle> "
         "<password>",
         "Mark notifications seen via wf_agent_update_seen_typed."},
        {"repo",
         "repo put-record <service> <handle> <password> --collection <nsid> "
         "--rkey <rkey> --json <record|file>",
         "Write a record via wf_agent_put_record_typed (read JSON from a "
         "string or file)."},
        {"repo",
         "repo delete-record <service> <handle> <password> --collection <nsid> "
         "--rkey <rkey>",
         "Delete a record via wf_agent_delete_record_typed."},
        {"repo",
         "repo list-records <service> <handle> <password> --collection <nsid> "
         "[--limit N] [--cursor C]",
         "List records via wf_agent_list_records_typed."},
        {"repo",
         "repo describe <service> <handle> <password> --repo <did-or-handle>",
         "Describe a repository via wf_agent_describe_repo_typed."},
        {"feed",
         "feed get <service> <handle> <password> --feed <generator-uri> "
         "[--limit N] [--cursor C]",
         "Fetch a custom feed via wf_agent_get_feed_typed."},
        {"feed",
         "feed author <service> <handle> <password> --actor <handle-or-did> "
         "[--limit N] [--cursor C]",
         "Fetch an actor's author feed via wf_agent_get_author_feed_typed."},
        {"moderation",
         "moderation report <service> <handle> <password> --subject <uri> "
         "--reason <reason> [--reason-type <type>] [--cid <cid>]",
         "Submit a moderation report via wf_agent_report_typed."},
        {"search-actors",
         "search-actors <service> <handle> <password> <query> [limit]",
         "Search actors via app.bsky.actor.searchActors. limit defaults to 25."},
        {"search-typeahead",
         "search-typeahead <service> <handle> <password> <query> [limit]",
         "Actor search typeahead via app.bsky.actor.searchActorsTypeahead. "
         "limit defaults to 10."},
        {"search-starter-packs",
         "search-starter-packs <service> <handle> <password> <query> [limit]",
         "Search starter packs via app.bsky.graph.searchStarterPacks. "
         "limit defaults to 25."},
        {"get-actor-feeds",
         "get-actor-feeds <service> <handle> <password> --actor "
         "<handle-or-did> [--limit N] [--cursor C]",
         "Fetch an actor's pinned and curated feeds via "
         "wf_agent_get_actor_feeds."},
        {"get-actor-likes",
         "get-actor-likes <service> <handle> <password> --actor "
         "<handle-or-did> [--limit N] [--cursor C]",
         "Fetch an actor's liked posts via wf_agent_get_actor_likes."},
        {"get-likes",
         "get-likes <service> <handle> <password> <at-uri> [--limit N] "
         "[--cursor C]",
         "Fetch likes for a post via app.bsky.feed.getLikes."},
        {"get-reposted-by",
         "get-reposted-by <service> <handle> <password> <at-uri> [--limit N] "
         "[--cursor C]",
         "Fetch reposters of a post via app.bsky.feed.getRepostedBy."},
        {"get-quotes",
         "get-quotes <service> <handle> <password> <at-uri> [--limit N] "
         "[--cursor C]",
         "Fetch quote posts of a post via app.bsky.feed.getQuotes."},
        {"describe-feed",
         "describe-feed <service> <handle> <password> --feed <generator-uri>",
         "Fetch feed generator metadata via wf_agent_describe_feed_generator."},
        {"get-suggested-feeds",
         "get-suggested-feeds <service> <handle> <password>",
         "Fetch suggested feeds via wf_agent_get_suggested_feeds."},
        {"get-suggestions",
         "get-suggestions <service> <handle> <password> [limit]",
         "Fetch suggested follows via app.bsky.actor.getSuggestions."},
        {"known-followers",
         "known-followers <service> <handle> <password> <actor>",
         "Fetch known followers of an actor via app.bsky.graph.getKnownFollowers."},
        {"relationships",
         "relationships <service> <handle> <password> <actor> <other>...",
         "Fetch relationships between an actor and others via "
         "wf_agent_get_relationships."},
        {"get-actor-status",
         "get-actor-status <service> <actor>",
         "Fetch an actor's live status via wf_agent_get_actor_status."},
        {"get-feed-generators",
         "get-feed-generators <service> <feed-uri>...",
         "Fetch feed generator metadata for URIs via "
         "wf_agent_get_feed_generators (no auth)."},
        {"get-suggested-follows-by-actor",
         "get-suggested-follows-by-actor <service> <actor>",
         "Fetch suggested follows by actor via "
         "wf_agent_get_suggested_follows_by_actor (no auth)."},
        {"age-assurance",
         "age-assurance <service> <handle> <password> "
         "<begin|get-config|get-state>",
         "Interact with app.bsky.ageassurance: begin, getConfig, getState."},
        {"ozone",
         "ozone moderation query-statuses <service> <handle> <password> "
         "[--limit N] [--cursor C]",
         "Query moderation subject statuses via tools.ozone.moderation."},
        {"ozone",
         "ozone moderation get-suggestions <service> <handle> <password> "
         "[--ignore-subjects uri]... [--limit N]",
         "Fetch moderation suggestions via tools.ozone.moderation."},
        {"ozone",
         "ozone moderation get-label-definitions <service> <handle> "
         "<password> [--uri uri]...",
         "Fetch moderation label definitions via tools.ozone.moderation."},
        {"ozone",
         "ozone team list-members <service> <handle> <password> "
         "[--limit N] [--cursor C]",
         "List Ozone team members via tools.ozone.team.listMembers."},
        {"ozone",
         "ozone server get-config <service> <handle> <password>",
         "Fetch Ozone server config via tools.ozone.server.getConfig."},
        {"ozone",
         "ozone communication list-templates <service> <handle> <password>",
         "List communication templates via tools.ozone.communication."},
        {"ozone",
         "ozone setting list-options <service> <handle> <password> "
         "[--key key] [--scope scope] [--limit N]",
         "List Ozone setting options via tools.ozone.setting.listOptions."},
        {"ozone",
         "ozone signature search-accounts <service> <handle> <password> "
         "<did> [--limit N]",
         "Search accounts by signature via tools.ozone.signature."},
        {"ozone",
         "ozone report query-reports <service> <handle> <password> "
         "[--limit N] [--cursor C]",
         "Query Ozone reports via tools.ozone.report.queryReports."},
        {"ozone",
         "ozone queue list-queues <service> <handle> <password> "
         "[--limit N] [--cursor C]",
         "List Ozone moderation queues via tools.ozone.queue.listQueues."},
        {"admin",
         "admin search-accounts <service> <handle> <password> [email] "
         "[--limit N]",
         "Search accounts as admin via com.atproto.admin.searchAccounts."},
        {"admin",
         "admin get-account-info <service> <handle> <password> <did>",
         "Fetch account info as admin via com.atproto.admin.getAccountInfo."},
        {"admin",
         "admin get-subject-status <service> <handle> <password> <did>",
         "Fetch subject status as admin via com.atproto.admin.getSubjectStatus."},
        {"admin",
         "admin get-invite-codes <service> <handle> <password> "
         "[--create N] [--use-count N] [--cursor C]",
         "Fetch invite codes as admin via com.atproto.admin.getInviteCodes."},
        {"admin",
         "admin delete-account <service> <handle> <password> <did>",
         "Delete an account as admin via com.atproto.admin.deleteAccount."},
        {"admin",
         "admin disable-account-invites <service> <handle> <password>",
         "Disable account invites as admin."},
        {"admin",
         "admin enable-account-invites <service> <handle> <password>",
         "Enable account invites as admin."},
        {"admin",
         "admin send-email <service> <handle> <password> <recipient-did> "
         "<subject> <body>",
         "Send email as admin via com.atproto.admin.sendEmail."},
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
        if (strcmp(cmd, cmds[i].name) == 0) {
            fprintf(out, "wolf %s\n\n  %s\n\n  %s\n", cmds[i].usage,
                    cmds[i].usage, cmds[i].desc);
            return;
        }
    }

    fprintf(out, "error: no help for unknown command '%s'\n\n", cmd);
    usage_stream(out);
}

/* Print usage and exit 0 (offline-safe: never touches the network). */
int usage_exit(void) {
    usage_stream(stdout);
    return 0;
}

/* ----------------------------------------------------------------- */
/* Small helpers                                                     */
/* ----------------------------------------------------------------- */

/* Join argv[first..argc-1] into a single heap string (caller frees). */
char *join_args(int argc, char **argv, int first) {
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
wf_status resolve_actor_to_did(wf_agent *agent, const char *actor,
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
wf_agent *agent_login_or_err(const char *service, const char *handle,
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

/* Print an agent-level failure to stderr, appending the server's XRPC error
 * message when one was captured. Returns the failure exit code. */
int cli_agent_error(const char *what, wf_status s, wf_agent *agent) {
    const char *msg = wf_agent_last_error(agent);
    if (msg && *msg) {
        fprintf(stderr, "error: %s failed (status %d): %s\n", what, (int)s,
                msg);
    } else {
        fprintf(stderr, "error: %s failed (status %d)\n", what, (int)s);
    }
    return 1;
}

/* Resolve a post at-uri to its CID via app.bsky.feed.getPosts.
 * Caller frees *out_cid. Returns WF_OK or an error status. */
wf_status resolve_post_cid(wf_agent *agent, const char *at_uri,
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
wf_status resolve_post_for_reply(wf_agent *agent, const char *at_uri,
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
        free(*out_cid);
        *out_cid = NULL;
        free(*out_root_uri);
        *out_root_uri = NULL;
        free(*out_root_cid);
        *out_root_cid = NULL;
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/* Format the current UTC time as an RFC 3339 timestamp (e.g. for record
 * createdAt fields). Writes at most `len` bytes into `buf`. */
void now_rfc3339(char *buf, size_t len) {
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

/* Print a raw JSON agent response, freeing resources, returning 0 on success.
 * On error prints to stderr and returns 1. */
int finish_agent_response(wf_agent *agent, wf_status s, wf_response *res) {
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

/* Read the entire text file `path` into a heap string (NUL terminated).
 * Returns NULL on failure. Caller frees. */
char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *data = malloc((size_t)len + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(data, 1, (size_t)len, f);
    fclose(f);
    data[n] = '\0';
    return data;
}

/* ----------------------------------------------------------------- */
/* Subcommands                                                       */
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
    if (strcmp(cmd, "whoami") == 0) {
        return cmd_whoami(rest, cargv);
    }
    if (strcmp(cmd, "describe-server") == 0) {
        return cmd_describe_server(rest, cargv);
    }
    if (strcmp(cmd, "post") == 0) {
        return cmd_post(rest, cargv);
    }
    if (strcmp(cmd, "reply") == 0) {
        return cmd_reply(rest, cargv);
    }
    if (strcmp(cmd, "delete") == 0) {
        return cmd_delete(rest, cargv);
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
    if (strcmp(cmd, "ozone") == 0) {
        return cmd_ozone(rest, cargv);
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
    if (strcmp(cmd, "revoke-account-credentials") == 0) {
        return cmd_revoke_account_credentials(rest, cargv);
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

    /* --- Feed & discovery --- */
    if (strcmp(cmd, "get-likes") == 0) {
        return cmd_get_likes(rest, cargv);
    }
    if (strcmp(cmd, "get-reposted-by") == 0) {
        return cmd_get_reposted_by(rest, cargv);
    }
    if (strcmp(cmd, "get-quotes") == 0) {
        return cmd_get_quotes(rest, cargv);
    }
    if (strcmp(cmd, "get-actor-likes") == 0) {
        return cmd_get_actor_likes(rest, cargv);
    }
    if (strcmp(cmd, "get-actor-feeds") == 0) {
        return cmd_get_actor_feeds(rest, cargv);
    }
    if (strcmp(cmd, "describe-feed") == 0) {
        return cmd_describe_feed(rest, cargv);
    }
    if (strcmp(cmd, "get-suggested-feeds") == 0) {
        return cmd_get_suggested_feeds(rest, cargv);
    }
    if (strcmp(cmd, "get-suggestions") == 0) {
        return cmd_get_suggestions(rest, cargv);
    }
    if (strcmp(cmd, "known-followers") == 0) {
        return cmd_known_followers(rest, cargv);
    }
    if (strcmp(cmd, "relationships") == 0) {
        return cmd_relationships(rest, cargv);
    }
    if (strcmp(cmd, "get-profiles") == 0) {
        return cmd_get_profiles(rest, cargv);
    }
    if (strcmp(cmd, "unread-count") == 0) {
        return cmd_unread_count(rest, cargv);
    }

    /* --- Graph & list operations --- */
    if (strcmp(cmd, "mute-list") == 0) {
        return cmd_mute_list(rest, cargv);
    }
    if (strcmp(cmd, "unmute-list") == 0) {
        return cmd_unmute_list(rest, cargv);
    }
    if (strcmp(cmd, "block-list") == 0) {
        return cmd_block_list(rest, cargv);
    }
    if (strcmp(cmd, "unblock-list") == 0) {
        return cmd_unblock_list(rest, cargv);
    }
    if (strcmp(cmd, "mute-thread") == 0) {
        return cmd_mute_thread(rest, cargv);
    }
    if (strcmp(cmd, "unmute-thread") == 0) {
        return cmd_unmute_thread(rest, cargv);
    }
    if (strcmp(cmd, "get-list-blocks") == 0) {
        return cmd_get_list_blocks(rest, cargv);
    }
    if (strcmp(cmd, "get-list-mutes") == 0) {
        return cmd_get_list_mutes(rest, cargv);
    }
    if (strcmp(cmd, "get-suggested-follows") == 0) {
        return cmd_get_suggested_follows(rest, cargv);
    }
    if (strcmp(cmd, "starter-pack") == 0) {
        return cmd_starter_pack(rest, cargv);
    }

    /* --- Server & account --- */
    if (strcmp(cmd, "create-account") == 0) {
        return cmd_create_account(rest, cargv);
    }
    if (strcmp(cmd, "app-password") == 0) {
        return cmd_app_password(rest, cargv);
    }
    if (strcmp(cmd, "invite-codes") == 0) {
        return cmd_invite_codes(rest, cargv);
    }
    if (strcmp(cmd, "activate") == 0) {
        return cmd_activate(rest, cargv);
    }
    if (strcmp(cmd, "deactivate") == 0) {
        return cmd_deactivate(rest, cargv);
    }
    if (strcmp(cmd, "check-status") == 0) {
        return cmd_check_status(rest, cargv);
    }
    if (strcmp(cmd, "email") == 0) {
        return cmd_email(rest, cargv);
    }
    if (strcmp(cmd, "password-reset") == 0) {
        return cmd_password_reset(rest, cargv);
    }
    if (strcmp(cmd, "reserve-signing-key") == 0) {
        return cmd_reserve_signing_key(rest, cargv);
    }
    if (strcmp(cmd, "get-service-auth") == 0) {
        return cmd_get_service_auth(rest, cargv);
    }
    if (strcmp(cmd, "request-account-delete") == 0) {
        return cmd_request_account_delete(rest, cargv);
    }
    if (strcmp(cmd, "request-email-update") == 0) {
        return cmd_request_email_update(rest, cargv);
    }
    if (strcmp(cmd, "request-email-confirmation") == 0) {
        return cmd_request_email_confirmation(rest, cargv);
    }

    /* --- Identity --- */
    if (strcmp(cmd, "resolve-did") == 0) {
        return cmd_resolve_did(rest, cargv);
    }
    if (strcmp(cmd, "check-handle") == 0) {
        return cmd_check_handle(rest, cargv);
    }
    if (strcmp(cmd, "get-recommended-did-credentials") == 0) {
        return cmd_get_recommended_did_credentials(rest, cargv);
    }
    if (strcmp(cmd, "rotate-handle") == 0) {
        return cmd_rotate_handle(rest, cargv);
    }
    if (strcmp(cmd, "plc") == 0) {
        return cmd_plc(rest, cargv);
    }

    /* --- Sync & blob --- */
    if (strcmp(cmd, "sync") == 0) {
        if (rest >= 2) {
            const char *sub = cargv[1];
            if (strcmp(sub, "get-blob") == 0)
                return cmd_sync_get_blob(rest - 1, cargv + 1);
            if (strcmp(sub, "get-blocks") == 0)
                return cmd_sync_get_blocks(rest - 1, cargv + 1);
            if (strcmp(sub, "get-record") == 0)
                return cmd_sync_get_record(rest - 1, cargv + 1);
            if (strcmp(sub, "list-blobs") == 0)
                return cmd_sync_list_blobs(rest - 1, cargv + 1);
            if (strcmp(sub, "subscribe") == 0)
                return cmd_sync_subscribe(rest - 1, cargv + 1);
        }
        fprintf(stderr, "error: unknown sync subcommand\n");
        usage_stream(stderr);
        return 0;
    }
    if (strcmp(cmd, "import-repo") == 0) {
        return cmd_import_repo(rest, cargv);
    }
    if (strcmp(cmd, "list-missing-blobs") == 0) {
        return cmd_list_missing_blobs(rest, cargv);
    }

    /* --- Preferences & push --- */
    if (strcmp(cmd, "preferences") == 0) {
        return cmd_preferences(rest, cargv);
    }
    if (strcmp(cmd, "register-push") == 0) {
        return cmd_register_push(rest, cargv);
    }
    if (strcmp(cmd, "unregister-push") == 0) {
        return cmd_unregister_push(rest, cargv);
    }
    if (strcmp(cmd, "update-profile") == 0) {
        return cmd_update_profile(rest, cargv);
    }
    if (strcmp(cmd, "put-actor-status") == 0) {
        return cmd_put_actor_status(rest, cargv);
    }
    if (strcmp(cmd, "upload-blob") == 0) {
        return cmd_upload_blob(rest, cargv);
    }
    if (strcmp(cmd, "apply-writes") == 0) {
        return cmd_apply_writes(rest, cargv);
    }
    if (strcmp(cmd, "send-interactions") == 0) {
        return cmd_send_interactions(rest, cargv);
    }

    /* --- Chat --- */
    if (strcmp(cmd, "convos") == 0) {
        return cmd_convos(rest, cargv);
    }
    if (strcmp(cmd, "messages") == 0) {
        return cmd_messages(rest, cargv);
    }
    if (strcmp(cmd, "chat") == 0) {
        return cmd_chat(rest, cargv);
    }
    if (strcmp(cmd, "groups") == 0) {
        return cmd_groups(rest, cargv);
    }
    if (strcmp(cmd, "reactions") == 0) {
        return cmd_reactions(rest, cargv);
    }

    /* --- Sync/Jetstream/Firehose --- */
    if (strcmp(cmd, "jetstream") == 0) {
        return cmd_jetstream(rest, cargv);
    }
    if (strcmp(cmd, "firehose") == 0) {
        return cmd_firehose(rest, cargv);
    }

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage_stream(stderr);
    return 0;
}
