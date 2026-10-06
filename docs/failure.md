# Failure kinds

A client has to tell the person something when a call fails: the password was refused, there is no network, the secure connection failed, slow down. `wf_status` alone cannot say. A rejected login arrives as `WF_ERR_AUTH` from the session code but as `WF_ERR_HTTP` with an XRPC error name (`ExpiredToken`) or a bare 401 from a plain request, and a rate limit as `WF_ERR_RATE_LIMIT` or a 429. Cobalt and Indigo each worked that out for themselves, and not identically. [`wolfram/failure.h`](../include/wolfram/failure.h) does it once.

```c
wf_failure_kind k = wf_failure_classify(st, http_status, xrpc_error);
const char *tag = wf_failure_tag(k);   /* "rate-limit", for logs and tests */
```

`http_status` is the response's HTTP status, or 0 if there was none. `xrpc_error` is the error name from the response body (`wf_xrpc_error`), or `NULL`. The function is total: every input gives a kind, and `WF_OK` gives `WF_FAIL_NONE` whatever the other two say. Message text stays in each client, because the wording and the language differ per platform. It is strict C89 with no allocation, so the Mac OS 9 target compiles it.

## The mapping

When `st` is `WF_ERR_HTTP`, the XRPC error name decides first, then the HTTP status. For every other `st` the status decides alone, because a failure below HTTP has no response to read.

| Kind | Tag | From |
| --- | --- | --- |
| `WF_FAIL_NONE` | `none` | `WF_OK` |
| `WF_FAIL_BAD_CREDENTIALS` | `bad-credentials` | `WF_ERR_AUTH`; `AuthenticationRequired`, `ExpiredToken`, `InvalidToken`, `AuthMissing`, `AuthFactorTokenRequired`; HTTP 401 |
| `WF_FAIL_RATE_LIMIT` | `rate-limit` | `WF_ERR_RATE_LIMIT`; `RateLimitExceeded`; HTTP 429 |
| `WF_FAIL_TIMEOUT` | `timeout` | `WF_ERR_TIMEOUT`; `UpstreamTimeout`; HTTP 408, 504 |
| `WF_FAIL_TLS` | `tls` | `WF_ERR_TLS`, `WF_ERR_CRYPTO`, `WF_ERR_CONFIG` |
| `WF_FAIL_NETWORK` | `network` | `WF_ERR_NETWORK`, and the four DID and handle resolution failures |
| `WF_FAIL_SERVER` | `server` | `WF_ERR_INTERNAL`; `InternalServerError`, `UpstreamFailure`, `NotEnoughResources`; HTTP 5xx |
| `WF_FAIL_BAD_RESPONSE` | `bad-response` | `WF_ERR_PARSE` |
| `WF_FAIL_NOT_READY` | `not-ready` | `WF_ERR_UNSUPPORTED`, `WF_ERR_NOT_IMPLEMENTED` |
| `WF_FAIL_OTHER` | `other` | everything else, including an `WF_ERR_HTTP` the table does not name (400, 403, 404) |

`WF_ERR_CRYPTO` and `WF_ERR_CONFIG` count as TLS because the console transports report a failed handshake or a missing certificate store that way until `WF_ERR_TLS` is verified on them; Indigo already treated them as TLS. Only the libcurl transport reports `WF_ERR_TLS` today (see the status comment in `xrpc.h`), and I have not checked the console or macTLS transports on hardware.

A 400, 403 or 404 is `other` on purpose: the service answered and refused the request, which is not a failure of the service, and what to say depends on the call. Look at the XRPC error name yourself for those.

## Vectors

[`test/vectors/failure.json`](../test/vectors/failure.json) holds every case as `{status, http, xrpc_error, kind}` with the status given by name and a `statuses` table of values, so another language's port can run the same file. `test/test_failure.c` runs it and also checks the function is total and the tags are distinct.
