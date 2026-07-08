# Command-line client (`wolfram`)

The `wolfram` executable (built by default as `build/wolfram`) is a thin
demonstration over the SDK that exercises the high-level agent API end to end.
Every subcommand is network-gated: with missing arguments it prints usage and
exits 0 without performing any network I/O.

```sh
wolfram login        <service> <handle> <password>
wolfram post         <service> <handle> <password> <text...>
wolfram timeline     <service> <handle> <password> [pages]
wolfram get-post     <service> <at-uri>
wolfram profile      <service> <actor>
wolfram follow       <service> <handle> <password> <actor>
wolfram unfollow     <service> <handle> <password> <actor>
wolfram resolve      <handle-or-did>
wolfram like         <service> <handle> <password> <at-uri>
wolfram unlike       <service> <handle> <password> <uri/record>
wolfram repost       <service> <handle> <password> <at-uri>
wolfram reply        <service> <handle> <password> <parent-at-uri> <text...>
wolfram labels       <service> <handle> <password> <at-uri> [sources...]
wolfram oauth-login  <service> [--client-id ID] [--redirect-uri URI] [--scope S] [--port P]
wolfram help         [command]
```

`login` and `post` create a session via `wf_agent_login` / `wf_agent_post`.
`timeline` fetches the authenticated user's timeline with the cursor-based
`wf_agent_get_timeline_paged` helper. `get-post` resolves an `at://` URI and
issues `com.atproto.repo.getRecord` over raw XRPC (no login required).
`profile` calls `wf_agent_get_profile`. `follow`/`unfollow` resolve the actor
to a DID and issue graph writes through the agent. `resolve` turns a handle
into a DID via `wf_handle_resolve` (a DID is echoed back unchanged). `like` /
`unlike` / `repost` / `reply` exercise the record-write helpers (`wf_agent_like`,
`wf_agent_repost`, `wf_agent_reply`); `labels` reads subject labels; `oauth-login`
drives the OAuth/PAR/DPoP flow; `help [command]` prints usage for a subcommand.
