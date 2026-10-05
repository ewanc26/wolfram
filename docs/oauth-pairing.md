# OAuth pairing contract

This is the one specification of how a console client signs in through the [hosted OAuth node](oauth-node.md). The server half is `src/node/oauth_node.c`; the client half is [`wolfram/oauth_pairing.h`](../include/wolfram/oauth_pairing.h), which builds on every target, consoles included. Cobalt and Indigo should call it rather than parse these replies by hand.

The shared test vectors are in [`test/vectors/oauth_pairing.json`](../test/vectors/oauth_pairing.json). They are plain JSON so a client in another language can read them. The suite that runs them is `test/test_oauth_pairing.c`.

Platinum's Node bridge speaks a different, older protocol (`POST /v1/pair` with a code the bridge displays after a browser login). That is not this contract and is not covered here.

## Flow

1. The client sends `POST /xrpc/uk.ewancroft.oauth.begin` with `{"handle":"<handle>"}`.
2. The node replies with a pair code, a URL and an expiry. The client shows the URL (and code) to the user, who opens it on another device and signs in at their PDS.
3. The client sends `GET /xrpc/uk.ewancroft.oauth.poll?code=<pair_code>` every 1500 ms.
4. When the user finishes, a poll returns `complete` with a bearer token. The client points its agent at `service` and uses the token with `wf_agent_set_bearer`.

## Replies

`begin`:

| Field | Type | Rule |
| --- | --- | --- |
| `pair_code` | string | Required, non-empty, under 64 bytes. |
| `pair_url` | string | Required, `http://` or `https://`, under 512 bytes. |
| `expires_at` | number | Optional, Unix seconds. Absent means 0. |

`poll`, discriminated by `status`:

| `status` | Other fields | Meaning |
| --- | --- | --- |
| `pending` | none | Keep polling. |
| `complete` | `token`, `handle`, `did`, `service`, all required non-empty strings; `did` starts `did:`; `service` is `http(s)://` | Signed in. |
| `error` | `message` (optional string) | Terminal. |

Anything else is a contract violation and parses as `WF_ERR_PARSE`: a value that does not fit its buffer, a wrong type, an unknown `status`, a `complete` missing a field. A violation never yields a half-filled result; the output is zeroed. Over-long values are rejected, not truncated. The one exception is `message`, which is only display text and is truncated to 255 bytes.

## What the client does with failures

| Poll outcome | Client behaviour |
| --- | --- |
| Network failure, HTTP 5xx | Transient. Poll again. |
| HTTP 404 (`NotFound`, "Unknown pairing code.") | Terminal. Stop. Do not poll a purged code for nine minutes. |
| 2xx with a reply that breaks the table above | Terminal (`WF_ERR_PARSE`). The node is not behaving. |
| `error` reply | Terminal. Show `message` if there is one. |
| `expires_at` passed | Give up (`WF_ERR_TIMEOUT`). With no `expires_at`, give up after 360 polls. |

`wf_oauth_pair_run()` implements exactly this, with hooks for the code display, cancellation and sleeping, so the loop is written once.

## Security

The `token` is a bearer credential. Nothing in the library logs it, the pair code, a poll URL or a raw reply; `test_oauth_pairing` runs the whole driver at debug log level with stderr captured and fails if the token or code appears. Call `wf_oauth_pair_poll_wipe()` when you are done with a result; it zeroes the struct in a way the compiler may not drop.

`service` is the node's public base URL. Send the token to that host only, and do not follow a different one.

## Vectors

The file has five groups: `build_begin` (request body), `begin` and `poll` (reply parsing), and `driver` (whole sessions: scripted HTTP replies in, final status and number of polls made out). Numbers in `n` match the table in [issue 101](https://github.com/ewanc26/wolfram/issues/101).
