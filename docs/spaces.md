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

## Not here yet

Record and sync requests, STAR archives, OAuth `space:` scopes, the space host
methods and the vendored lexicons' generated wrappers are not implemented.
Spaces are not exposed through the C++, .NET or Node bindings, and nothing here
is part of the Mac OS 9 target.
