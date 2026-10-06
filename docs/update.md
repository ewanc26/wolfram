# Client self-update: manifest, versions and checksums

The platform-neutral half of self-update, so Cobalt, Indigo and Platinum do not each carry their own copy. The header is [`wolfram/update.h`](../include/wolfram/update.h) and the vectors every client runs are in [`test/vectors/update/`](../test/vectors/update/). Wolfram itself does not update itself.

What is here: parsing the release manifest, comparing versions, and checking a download against the manifest's SHA-256. What is not: fetching the files (use the XRPC/HTTP transport), swapping the running binary, relaunching. Those differ per platform and stay in each client.

## Manifest

One `update.json` asset on each GitHub release, published by the client's own release script:

```json
{"schema":1,"app":"cobalt","version":"0.6.0","notes":"...",
 "asset":{"name":"cobalt-0.6.0.wuhb",
          "url":"https://github.com/ewanc26/cobalt/releases/download/v0.6.0/cobalt-0.6.0.wuhb",
          "size":1234567,"sha256":"<64 hex>"},
 "signature":null}
```

`wf_update_parse_manifest()` rejects, and zeroes its output on, any of these:

- bad JSON, or a `schema` other than 1;
- a missing field, or one too long for its buffer (nothing is truncated);
- a `version` that is not strict `MAJOR.MINOR.PATCH[-pre]`;
- an asset URL that is not `https://`, has userinfo, whitespace or a backslash, or has no host;
- a `size` of 0, fractional, or over 4 GiB;
- a `sha256` that is not exactly 64 hex characters.

A caller-supplied policy adds three more, reported as `WF_ERR_VALIDATION` rather than `WF_ERR_PARSE`: a size ceiling (default 64 MiB), a required `app`, and a required URL prefix (which must itself be `https://`). Give the policy the prefix of your own repository's release downloads, so a manifest cannot point the updater at another host.

`notes` is optional. The manifest's own `signature` member stays reserved: `null` or absent, and anything else sets `has_signature` and is **not verified**. The signature that counts is the detached one described under [Signature](#signature).

## Versions

`wf_update_compare_versions(a, b, &err)` follows [semver 2.0.0](https://semver.org/) precedence. A release outranks its prereleases (`1.0.0-rc.1 < 1.0.0`), numeric prerelease identifiers compare as numbers and sort below alphanumeric ones, build metadata is ignored. No leading `v`, no leading zeros, no missing parts. A malformed string sets `*err` and returns 0, so a bad version is never silently equal to another. Map your own build string (a `git describe` suffix, say) to `x.y.z` before calling.

## Checksums

`wf_update_verify_init/feed/final` hash the download as it arrives, in any chunking, and fail as soon as more than `size` bytes come in, so a hostile server cannot make you write an unbounded file. `final` succeeds only when exactly `size` bytes arrived and the digest matches, compared in constant time. `wf_sha256_buffer()` hashes a buffer already in memory.

## Signature

A SHA-256 taken from a manifest fetched over TLS from the same release is integrity against corruption, not authenticity against a compromised release: whoever can publish a release can publish a matching manifest. So each release also carries `update.json.sig`, a detached Ed25519 ([RFC 8032](https://www.rfc-editor.org/rfc/rfc8032)) signature over the exact bytes of `update.json`, written as 128 hex characters and an optional newline. The client has the 32-byte public key compiled in.

```c
wf_status st = wf_update_verify_signature(manifest_bytes, manifest_len,
                                          sig_text, sig_len, public_key);
if (st != WF_OK) /* refuse: unsigned, tampered or signed by another key */;
```

Verify the bytes **before** parsing them and refuse to update on anything but `WF_OK`. A missing `update.json.sig` is a refusal, not a warning: an unsigned release is not a valid one. The signature covers the manifest, and the manifest carries the asset's SHA-256, so the signature covers the download too, once `wf_update_verify_final` has checked it against that hash.

Signing is not done here. Each client's release workflow signs with a private key held only as a GitHub Actions secret, and the public half is committed beside the verification code. `wf_update_verify_signature` returns `WF_ERR_PARSE` for signature text of the wrong shape and `WF_ERR_VALIDATION` for a signature that does not verify, is not canonical (S not below the group order) or is under a key that is not a curve point.

`ed25519.c` is a compact port of TweetNaCl's algorithms with its own SHA-512, so it builds on every console with nothing behind it. It is C99, so it is not in the Mac OS 9 C89 target; Platinum's bridge verifies the manifest and the Mac client trusts the bridge it paired with. Vectors are in `test/vectors/ed25519.json` (RFC 8032 section 7.1 tests 1 and 2, SHA-512 padding edges, tampering, a non-canonical S, bad keys). I also ran 1500 random signatures with random one-bit mutations against pyca/cryptography: the two agreed on every one.

## Building

`update_core.c` (versions, SHA-256, verification) is strict C89 with no `stdint.h`, and CI compiles it with `-std=c89 -pedantic-errors` as part of the Mac OS 9 target. `manifest.c` needs cJSON, so it is part of libwolfram but not of the Mac OS 9 target; a Classic Mac client gets manifests through its bridge for now. If the Mac OS 9 client needs to parse manifests itself, that needs a bounded scanner like the one in `xrpc_mac9.c`.

## Vectors

`ed25519.json` (signatures, see above), `versions.json` (comparisons and invalid strings), `sha256.json` (FIPS 180-4 examples plus padding edges and a million-byte message), `manifest.json` (valid, rejected and policy cases). All plain JSON, so Platinum's TypeScript port can run the same files.
