#!/usr/bin/env bash
# lexicon-drift.sh -- compare the vendored lexicons/ corpus against upstream
# bluesky-social/atproto at a pinned ref.
#
# Prints added / removed / changed NSIDs and exits 0 when the trees match,
# 1 when they drift, 2 on tooling failure. CI runs it as an advisory job
# (continue-on-error), so drift is visible without blocking merges.
#
#   tools/lexicon-drift.sh [--ref <sha-or-branch>] [--keep]
#
# The ref defaults to UPSTREAM_REF below; bump it deliberately, never
# silently, and record the bump in the PR that bumps it.
set -euo pipefail

cd "$(dirname "$0")/.."

UPSTREAM_REPO="bluesky-social/atproto"
UPSTREAM_REF="ec49bcb0d88202e92674fe910a0201f2a44db961"
KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --ref) UPSTREAM_REF="${2:?--ref needs a value}"; shift 2 ;;
        --keep) KEEP=1; shift ;;
        *) echo "lexicon-drift: unknown argument: $1" >&2; exit 2 ;;
    esac
done

work="$(mktemp -d)"
cleanup() { [ "$KEEP" = 1 ] && echo "kept $work" || rm -rf "$work"; }
trap cleanup EXIT

echo "lexicon-drift: upstream $UPSTREAM_REPO@$UPSTREAM_REF vs vendored lexicons/"
curl -fsSL --max-time 120 \
    "https://codeload.github.com/$UPSTREAM_REPO/tar.gz/$UPSTREAM_REF" \
    -o "$work/upstream.tar.gz" || { echo "lexicon-drift: fetch failed" >&2; exit 2; }
mkdir -p "$work/upstream"
tar -xzf "$work/upstream.tar.gz" -C "$work/upstream" \
    --include '*/lexicons/*' 2>/dev/null || \
    tar -xzf "$work/upstream.tar.gz" -C "$work/upstream" || { echo "lexicon-drift: extract failed" >&2; exit 2; }
# Upstream keeps the canonical corpus in the top-level lexicons/ and spreads
# the rest over packages/*/lexicons (api carries app/ and chat/, ozone its
# own tools/ozone). Index them all; the top-level dir wins on duplicate NSIDs
# because it is the canonical copy.
index() { # <dir> <outfile> <first-wins: 1/0>
    ( cd "$1" && find . -name '*.json' -type f | LC_ALL=C sort | while read -r f; do
          nsid="${f#./}"; nsid="${nsid%.json}"; nsid="${nsid//\//.}"
          printf '%s  %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" "$nsid"
      done ) >> "$2"
}
: > "$work/upstream.idx"
top="$(echo "$work/upstream"/*/lexicons)"
[ -d "$top" ] || { echo "lexicon-drift: no lexicons/ in upstream tarball" >&2; exit 2; }
index "$top" "$work/upstream.idx"
# Corpus packages only: packages/lex and *-example carry test fixtures
# (com.example.*), not the shipped corpus.
for pkg in api bsky ozone pds; do
    d="$(echo "$work/upstream"/*/packages/"$pkg"/lexicons)"
    [ -d "$d" ] && index "$d" "$work/upstream.idx"
done
# first occurrence of an NSID wins
awk '!seen[$2]++' "$work/upstream.idx" > "$work/upstream.idx.uniq"
mv "$work/upstream.idx.uniq" "$work/upstream.idx"
: > "$work/vendored.idx"
index "lexicons" "$work/vendored.idx"

cut -d' ' -f3- "$work/upstream.idx" | LC_ALL=C sort > "$work/up.nsid"
cut -d' ' -f3- "$work/vendored.idx" | LC_ALL=C sort > "$work/ven.nsid"
added="$work/added"; removed="$work/removed"; changed="$work/changed"

# added: in upstream, not vendored
comm -13 "$work/ven.nsid" "$work/up.nsid" > "$added"
# removed: vendored, not upstream
comm -23 "$work/ven.nsid" "$work/up.nsid" > "$removed"
# changed: same NSID, different hash (idx lines are "<sha256>  <nsid>")
awk 'NR==FNR { up[$2]=$1; next } ($2 in up) && up[$2] != $1 { print $2 }' \
    "$work/upstream.idx" "$work/vendored.idx" | LC_ALL=C sort > "$changed" 

rc=0
if [ -s "$added" ]; then
    rc=1
    echo "ADDED upstream (missing from vendored corpus):"
    sed 's/^/  + /' "$added"
fi
if [ -s "$removed" ]; then
    rc=1
    echo "REMOVED upstream (still vendored):"
    sed 's/^/  - /' "$removed"
fi
if [ -s "$changed" ]; then
    rc=1
    echo "CHANGED upstream (vendored copy differs):"
    sed 's/^/  ~ /' "$changed"
fi
if [ "$rc" = 0 ]; then
    echo "lexicon-drift: clean — vendored corpus matches $UPSTREAM_REF"
else
    echo "lexicon-drift: DRIFT against $UPSTREAM_REPO@$UPSTREAM_REF — vendor the differences or bump the pinned ref deliberately"
fi
exit "$rc"
