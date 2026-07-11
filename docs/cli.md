# Command-line client (`wolfram`)

The `wolfram` executable (built by default as `build/wolfram`) is a thin
demonstration over the SDK that exercises the high-level agent API end to end.
Every subcommand is network-gated: with missing arguments it prints usage and
exits 0 without performing any network I/O.

## Getting help

`wolfram help` (or `wolfram --help` / `wolfram -h`) prints the full command
list. `wolfram help <command>` prints that command's usage line and a short
description:

```sh
wolfram help
wolfram help post
wolfram help labels
```

## Commands

```sh
wolfram login          <service> <handle> <password>
wolfram whoami         <service> <handle> <password>
wolfram describe-server <service>

wolfram post           <service> <handle> <password> <text...>
wolfram reply          <service> <handle> <password> <parent-at-uri> <text...>
wolfram delete         <service> <handle> <password> <at-uri>
wolfram like           <service> <handle> <password> <at-uri>
wolfram unlike         <service> <handle> <password> <like-at-uri>
wolfram repost         <service> <handle> <password> <at-uri>
wolfram delete-repost  <service> <handle> <password> <repost-at-uri>
wolfram unrepost       <service> <handle> <password> <repost-at-uri>

wolfram timeline       <service> <handle> <password> [pages]
wolfram get-post       <service> <at-uri>
wolfram get-record     <service> <handle> <password> <collection> <rkey>
wolfram repo put-record    <service> <handle> <password> --collection <nsid> --rkey <rkey> --json <record|file>
wolfram repo delete-record <service> <handle> <password> --collection <nsid> --rkey <rkey>
wolfram repo list-records  <service> <handle> <password> --collection <nsid> [--limit N] [--cursor C]
wolfram repo describe      <service> <handle> <password> --repo <did-or-handle>
wolfram profile        <service> <actor>
wolfram search         <service> <handle> <password> <query> [limit]
wolfram thread         <service> <handle> <password> <at-uri> [depth]
wolfram feed get    <service> <handle> <password> --feed <generator-uri> [--limit N] [--cursor C]
wolfram feed author <service> <handle> <password> --actor <handle-or-did> [--limit N] [--cursor C]
wolfram notifications  <service> <handle> <password> [limit]
wolfram notifications update-seen [--seen-at <iso>] <service> <handle> <password>

wolfram follow         <service> <handle> <password> <actor>
wolfram unfollow       <service> <handle> <password> <actor>
wolfram mute           <service> <handle> <password> <actor>
wolfram unmute         <service> <handle> <password> <actor>
wolfram block          <service> <handle> <password> <actor>
wolfram unblock        <service> <handle> <password> <actor>

wolfram follows        <service> <actor> [limit]
wolfram followers      <service> <actor> [limit]
wolfram blocks         <service> <handle> <password> [limit]
wolfram mutes          <service> <handle> <password> [limit]
wolfram list           <service> <list-uri> [limit]
wolfram lists          <service> <actor> [limit]

wolfram resolve        <service> <handle-or-did>
wolfram labels subscribe <service> [--cursor N] [--seconds N]
wolfram moderation     <service> <actor> [labeler-did]
wolfram oauth-login    <service> <handle> [client-id] [redirect-uri] [--state-file <path>]
wolfram oauth-callback <service> --url <redirect> --state <state> [--state-file <path>] [--client-id <id>] [--redirect-uri <uri>] [--session <path>]

wolfram video upload   <service> <handle> <password> <file.mp4>
wolfram video status   <service> <handle> <password> <job-id>
```

## What each command does

- `login` / `whoami` / `describe-server` — session and account metadata.
  `login` and `whoami` create a session via `wf_agent_login`; `describe-server`
  fetches `com.atproto.server.describeServer` with no auth.
- `post` / `reply` — create posts. `post` uses `wf_agent_post` (rich-text
  facets are auto-detected). `reply` resolves the parent's CID and root ref via
  `getPosts` and builds a reply record with `wf_agent_create_record`.
- `delete` / `like` / `unlike` / `repost` / `delete-repost` (alias `unrepost`) —
  record-write helpers (`wf_agent_delete_record`, `wf_agent_like`,
  `wf_agent_unlike`, `wf_agent_repost`, `wf_agent_delete_repost`). `like`,
  `repost`, and `reply` resolve the target post's CID via `getPosts` first.
- `timeline` — the authenticated user's home timeline via the cursor-based
  `wf_agent_get_timeline_paged` helper (`pages=0`, the default, fetches until
  exhausted).
- `get-post` — resolves an `at://` URI and issues `com.atproto.repo.getRecord`
  over raw XRPC (no login required).
- `get-record` — `wf_agent_get_record` for an explicit `<collection> <rkey>`
  pair; prints the raw record JSON.
- `profile` — `wf_agent_get_profile` for an actor (handle or DID).
- `search` — `app.bsky.feed.searchPosts` (`limit` defaults to 25).
- `thread` — `wf_agent_get_post_thread_typed` (`depth` defaults to 6).
- `notifications` — `wf_agent_list_notifications_typed` (`limit` defaults to 50).
- `follow` / `unfollow` — resolve the actor to a DID and issue graph writes
  through the agent. `unfollow` looks up the existing follow URI via
  `getRelationships` and deletes it.
- `mute` / `unmute` — `wf_agent_mute` / `wf_agent_unmute` for a single actor.
- `follows` / `followers` — `wf_agent_get_follows` / `wf_agent_get_followers`
  for an actor (no login required); prints raw JSON. Optional `limit` defaults
  to 50.
- `blocks` / `mutes` — the authenticated user's `app.bsky.graph.getBlocks` /
  `getMutes` lists (`wf_agent_get_blocks` / `wf_agent_get_mutes`); require login.
- `list` / `lists` — `wf_agent_get_list` (items of a list URI) and
  `wf_agent_get_lists` (lists owned by an actor); print raw JSON. Optional
  `limit` defaults to 50.
- `resolve` — turns a handle into a DID via `wf_handle_resolve` (a DID is echoed
  back unchanged).
- `labels subscribe` — **label streaming**. Subscribes to
  `com.atproto.label.subscribeLabels` using the `label.h` streaming API and
  prints each arriving label (`uri`, `cid`, `val`, `src`, `exp`, `neg`). The
  stream runs for a bounded duration (`--seconds N`, default 30), an internal
  event cap, or until interrupted with Ctrl-C (SIGINT), then exits cleanly.
  `--cursor N` resumes from a sequence number.
- `moderation` — runs the moderation decision engine on an actor's profile
  (`wf_agent_moderate_profile` + `wf_mod_decision_ui`).
- `oauth-login` — drives the OAuth/PAR/DPoP discovery and authorization-begin
  flow, printing the authorization URL and flow state. The pending state is
  written to `--state-file` (default `~/.wolfram_oauth_state.json`) so it can be
  completed by `oauth-callback`.
- `oauth-callback` — completes the OAuth flow. Reads the pending state file,
  re-discovers the authorization server, parses the redirect URL (`--url`,
  carrying `?code=…&state=…`), performs the token exchange via
  `wf_oauth_authorization_complete`, and writes the resulting session to
  `--session` (default `~/.wolfram_session.json`). Run this after visiting the
  URL printed by `oauth-login`. No local HTTP server is started.
- `block` / `unblock` — block or unblock an actor (`wf_agent_block` /
  `wf_agent_unblock`); `unblock` resolves the existing block URI and deletes
  it. Require login.
- `notifications update-seen` — mark notifications as seen via
  `wf_agent_update_seen_typed`; pass `--seen-at <iso>` to set a specific
  timestamp, otherwise the current time is used.
- `repo put-record` / `delete-record` / `list-records` / `describe` — raw repo
  operations through the agent: write/delete a record by `<collection> <rkey>`
  (`--json` accepts an inline JSON object or a file path), list records of a
  collection (with `--limit`/`--cursor`), and fetch `describeRepo` for an
  account (`--repo <did-or-handle>`). Require login.
- `feed get` / `feed author` — fetch a custom feed by generator URI
  (`wf_agent_get_feed_typed`) or an actor's author feed (`--actor`, via
  `wf_agent_get_author_feed_typed`), both with `--limit`/`--cursor`. Require
  login.
- `moderation report` — submit a moderation report via `wf_agent_report_typed`
  for `--subject <uri>` (an `at://` record URI, or pass `--cid` for a blob),
  with `--reason` (or `--reason-type`, an NSID such as
  `com.atproto.moderation.defs#reason-spam`). Require login.
- `video upload` — reads `<file.mp4>` and uploads it via `wf_agent_upload_video`,
  printing the resulting blob CID, MIME type, and size.
- `video status` — polls a video processing job by `job-id` via
  `wf_agent_get_video_job_status`, printing the raw status JSON.

All commands that hit the authenticated user's data (`post`, `timeline`,
`follow`, `blocks`, `mutes`, `get-record`, `video upload`, `video status`,
etc.) take `<service> <handle> <password>` first. Read-only queries of other
actors (`get-post`, `profile`, `follows`, `followers`, `list`, `lists`,
`resolve`) take only `<service>` plus the actor/URI argument.

## Machine-readable output

Pass the global `--json` flag (before the subcommand) to print the raw JSON
response body instead of the human-readable text for list/get commands
(`timeline`, `notifications`, `thread`, `profile`, `feed get`/`author`, `repo
list-records`/`describe`, and others). This is useful for piping into
`jq` or other tooling.
