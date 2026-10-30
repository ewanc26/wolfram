# Hosted OAuth node deployment

This runs [`wolfram-oauth-node`](../../tools/oauth_node.c) in Docker and
publishes it at `https://oauth.croft.click` through a Cloudflare Tunnel. The
node is reached on loopback at `127.0.0.1:8788`; no inbound port is opened on
the host.

The node is **memory-only**. It has no state volume, no state file and no
database. Completed OAuth sessions — including refresh tokens and DPoP private
keys — exist only in the running process. Stopping or restarting the container
signs every console out. Pending pairing links also live only in memory.

## Start the node

From the wolfram repository root:

```sh
docker compose -f deploy/oauth-node/compose.yaml up -d --build
curl -fsS http://127.0.0.1:8788/oauth-client-metadata.json
```

The metadata document must say
`"client_id":"https://oauth.croft.click/oauth-client-metadata.json"`.

## Tunnel

The tunnel is a one-time setup, then it runs as a service or a foreground
process:

```sh
cloudflared tunnel create oauth-node
cloudflared tunnel route dns oauth-node oauth.croft.click
cloudflared tunnel run --url http://127.0.0.1:8788 oauth-node
```

Run the last command under a supervisor (systemd, launchd, or a second
container with the tunnel credentials) so it comes back after a restart.
`cloudflared tunnel list` should show the tunnel as `HEALTHY`.

## Check it

```sh
curl -fsS https://oauth.croft.click/oauth-client-metadata.json
curl -fsS -X POST https://oauth.croft.click/xrpc/uk.ewancroft.oauth.begin \
  -H 'content-type: application/json' \
  -d '{"handle":"bsky.app"}'
```

`begin` resolves the handle through Slingshot and contacts the account's PDS,
so a successful reply means DNS, the tunnel, the container and the node's
upstream HTTP are all working. The returned pairing URL is single-use and
expires with the node's default ten-minute lifetime.

## Updating

```sh
git pull
docker compose -f deploy/oauth-node/compose.yaml up -d --build
```

An update restarts the node and signs every console out. That is the intended
behaviour: no credential survives a restart. If an older deployment still has
an `oauth-node-state` volume, remove it (see below); the current container never
mounts or reads it.

## Sign every console out

Restarting the container is enough, because sessions are not persisted. To
destroy credentials left behind by an older build, stop the container and remove
the old volume:

```sh
docker compose -f deploy/oauth-node/compose.yaml stop oauth-node
docker compose -f deploy/oauth-node/compose.yaml rm -f oauth-node

docker volume ls --filter name=oauth-node-state
docker volume rm oauth-node_oauth-node-state

docker compose -f deploy/oauth-node/compose.yaml up -d --build
```

This also deletes any old sealed state file. OAuth grants already issued at the
PDS remain valid until they expire or are revoked there; the node cannot revoke
them because it no longer holds the sessions.

## Data protection

The public notice is at <https://oauth.croft.click/privacy> and at the root of
the hostname. It covers the controller, lawful bases, recipients, in-memory
retention periods and UK GDPR rights.

The node stores no personal data at rest. It does not log handles, DIDs,
tokens, pairing codes, request URLs or request bodies, and the executable
switches the library logger off so an upstream error cannot put a handle or DID
into Docker logs. In memory it holds the account handle, DID, PDS URL and OAuth
session only for the life of a console session, and drops them as pairing slots
are reused or when the process exits. There is nothing to export, back up or
erase from disk. Keep `docker logs` retention short anyway, and make sure any
proxy in front of the node does not log request paths or query strings.
