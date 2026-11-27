# AT Protocol Spaces (experimental)

Spaces are the AT Protocol extension for permissioned data: records kept under
access control instead of broadcast to the whole network. They are access
control, not encryption. They are in beta upstream from 2026-12-01 and still
changing, so **everything under `wolfram/spaces/` is experimental**: it can
change or be removed in any release, with no deprecation period.

## What this follows

| Source | Revision |
| --- | --- |
| [Spaces field guide](https://atproto.how/atproto-spaces-field-guide) | revised 2026-11-26 |
| [Proposal 0016](https://github.com/bluesky-social/proposals/blob/b54ec6853bab77edd4fd373804a480633094b324/0016-permissioned-data/README.md) | `b54ec68` |
| [Lexicons and `@atproto/space`](https://github.com/bluesky-social/atproto/tree/355b73380411a24c751b9896ee9e17f5b593bca7) (branch `permissioned-data`) | `355b733` |

Where they disagree, the lexicons win. Progress is tracked in
[#35](https://github.com/ewanc26/wolfram/issues/35).

## URIs

`wolfram/spaces/uri.h` builds on every target, consoles included.

```text
at:space/<authority>/<type>/<skey>
at:space/<authority>/<type>/<skey>/<author>/<collection>/<rkey>
```

The first DID of a space URI is the space **authority**, not the author. A
client that takes "the DID" out of a URI has to check which kind it holds:
`wf_space_uri_has_prefix` tells them apart, and `wf_syntax_aturi_parse` stays
for public `at://` URIs only.

```c
wf_space_uri uri;
if (wf_space_uri_parse(text, &uri) == WF_OK) {
    const char *author = uri.author; /* NULL for a space reference */
    wf_space_uri_free(&uri);
}
```

## Building the rest

Only the URI module is always built. Everything else needs a hosted build with
`-DWOLFRAM_BUILD_SPACES=ON`, which also fetches BLAKE3.

## Set hash

`wolfram/spaces/lthash.h` keeps the 2048-byte LtHash state of a space repo.
A syncer applies each record operation to its own state and compares the digest
with the commit's `data`; a mismatch means the copy has drifted.

```c
wf_space_lthash hash;
unsigned char digest[WF_SPACE_DIGEST_BYTES];

wf_space_lthash_init(&hash);
wf_space_lthash_apply(&hash, collection, rkey, prev_cid, cid);
wf_space_lthash_digest(&hash, digest);
```

The digest alone proves nothing: check the commit it is compared with first.

## Shared secret

`wolfram/spaces/kem.h` derives the secret a response is MACed with. The syncer
encapsulates against the author's `#atproto_kem` key and sends the returned key
share as the `keyid` of its MAC request; the repo host decapsulates it. Derive
a new one for every request, including each retry and each page, and wipe the
secret when the response has been checked.

## Not here yet

Record and sync requests, STAR archives, OAuth `space:` scopes, the space host
methods and the vendored lexicons' generated wrappers are not implemented.
Spaces are not exposed through the C++, .NET or Node bindings, and nothing here
is part of the Mac OS 9 target.
