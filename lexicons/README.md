# Vendored lexicon corpus

This tree is the atproto lexicon corpus vendored verbatim from
[bluesky-social/atproto](https://github.com/bluesky-social/atproto) (MIT /
Apache-2.0): the top-level `lexicons/` plus the corpus packages
(`packages/api`, `packages/bsky`, `packages/ozone`, `packages/pds`).
Test-fixture packages (`packages/lex`, `*-example`) are not vendored.

## Staying current

`tools/lexicon-drift.sh` diffs this tree against upstream at the pinned ref
recorded at the top of that script and lists added / removed / changed NSIDs.
The `lexicon drift (advisory)` CI job runs it on every push and is
`continue-on-error`, so drift shows up without blocking merges.

When you vendor new upstream lexicons or bump the pinned ref, do it in a
deliberate PR and say so in the commit message. Do not hand-edit vendored
files; re-vendor from upstream instead.

At runtime, look a document up by NSID with `wf_lexicon_resolve` /
`wf_lexicon_registry_document` (see `docs/validate.md`).
