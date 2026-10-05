#!/usr/bin/env bash
#
# flow-drift.sh -- check that a repository's copy of the unified flow matches
# the canonical one in wolfram.
#
# Usage: tools/flow-drift.sh <canonical-wolfram-dir> [repo-dir]
#
# Compared byte for byte:
#   .github/PULL_REQUEST_TEMPLATE.md
#   the <!-- flow:begin --> ... <!-- flow:end --> block of AGENTS.md
# Also required: .github/workflows/flow.yml calls the reusable workflow.
#
# Fix drift by copying the canonical file or block over yours, never by
# editing the copy. Change the flow itself with a PR to wolfram.
set -u
canon="${1:?usage: flow-drift.sh <canonical-wolfram-dir> [repo-dir]}"
repo="${2:-.}"
fail=0
bad() {
	echo "flow-drift: $*" >&2
	fail=1
}

block() {
	awk '/<!-- flow:begin -->/{on=1} on{print} /<!-- flow:end -->/{on=0}' "$1" 2>/dev/null
}

f=.github/PULL_REQUEST_TEMPLATE.md
if [ ! -f "$repo/$f" ]; then
	bad "$f is missing"
elif ! cmp -s "$canon/$f" "$repo/$f"; then
	bad "$f differs from wolfram's canonical copy"
fi

want="$(block "$canon/AGENTS.md")"
have="$(block "$repo/AGENTS.md")"
[ -n "$want" ] || bad "canonical AGENTS.md has no flow block (is $canon a wolfram checkout?)"
if [ -z "$have" ]; then
	bad "AGENTS.md has no <!-- flow:begin --> ... <!-- flow:end --> block"
elif [ "$want" != "$have" ]; then
	bad "AGENTS.md flow block differs from wolfram's canonical copy"
fi

if ! grep -q 'flow-reusable.yml' "$repo/.github/workflows/flow.yml" 2>/dev/null; then
	bad ".github/workflows/flow.yml is missing or does not call flow-reusable.yml"
fi

((fail == 0)) && echo "flow-drift: in sync with wolfram"
exit "$fail"
