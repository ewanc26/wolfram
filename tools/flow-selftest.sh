#!/usr/bin/env bash
#
# flow-selftest.sh -- prove flow-check.sh and flow-drift.sh reject violations.
# A check that cannot fail is not a check, so every rule has a case here that
# must be refused as well as one that must pass.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
check="$here/tools/flow-check.sh"
drift="$here/tools/flow-drift.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fails=0

good_body=$'## What this changes\nFixes the thing.\n\n## Verification\nctest on Linux host.\n\n## Docs\nREADME updated.\n'

expect() { # expect pass|fail <name> -- command...
	local want="$1" name="$2"
	shift 3
	"$@" >"$tmp/out" 2>&1
	local rc=$?
	if { [ "$want" = pass ] && [ $rc -eq 0 ]; } || { [ "$want" = fail ] && [ $rc -ne 0 ]; }; then
		echo "ok   $want  $name"
	else
		echo "FAIL expected $want, rc=$rc: $name"
		sed 's/^/       /' "$tmp/out"
		fails=$((fails + 1))
	fi
}
chk() { PR_TITLE="$1" PR_BRANCH="$2" PR_BODY="$3" bash "$check"; }

expect pass "valid PR" -- chk "fix(sync): close the socket" "fix/relay-macos" "$good_body"
expect pass "no scope, breaking marker" -- chk "feat!: drop v1" "feat/drop-v1" "$good_body"
expect fail "title without a type" -- chk "Add stuff" "feat/x" "$good_body"
expect fail "title with unknown type" -- chk "wip: thing" "feat/x" "$good_body"
expect fail "title missing space after colon" -- chk "fix:thing" "fix/x" "$good_body"
expect fail "title over 100 chars" -- chk "fix: $(printf 'a%.0s' $(seq 1 100))" "fix/x" "$good_body"
expect fail "branch without a type" -- chk "fix: a thing" "my-branch" "$good_body"
expect fail "branch with uppercase" -- chk "fix: a thing" "fix/Thing" "$good_body"
expect fail "body missing Verification" -- chk "fix: a thing" "fix/x" $'## What this changes\nx\n\n## Docs\ny\n'
expect fail "empty template body" -- chk "fix: a thing" "fix/x" "$(cat "$here/.github/PULL_REQUEST_TEMPLATE.md")"
expect fail "section holding only a comment" -- chk "fix: a thing" "fix/x" $'## What this changes\nx\n\n## Verification\n<!-- todo -->\n\n## Docs\ny\n'

# Empty commits, in a throwaway repository.
r="$tmp/repo"
git init -q "$r"
(
	cd "$r" || exit 1
	git config user.email t@example.invalid
	git config user.name t
	echo a >a && git add a && git commit -q -m base
	git rev-parse HEAD >"$tmp/base"
	echo b >b && git add b && git commit -q -m real
	git rev-parse HEAD >"$tmp/real"
	git commit -q --allow-empty -m empty
	git rev-parse HEAD >"$tmp/empty"
)
chk_commits() { (cd "$r" && PR_TITLE="fix: a thing" PR_BRANCH="fix/x" PR_BODY="$good_body" BASE_SHA="$(cat "$tmp/base")" HEAD_SHA="$1" bash "$check"); }
expect pass "non-empty commits" -- chk_commits "$(cat "$tmp/real")"
expect fail "empty commit in range" -- chk_commits "$(cat "$tmp/empty")"

# Drift, against this checkout as the canonical copy.
copy_flow() {
	mkdir -p "$1/.github/workflows"
	cp "$here/.github/PULL_REQUEST_TEMPLATE.md" "$1/.github/"
	cp "$here/AGENTS.md" "$1/"
	cp "$here/.github/workflows/flow.yml" "$1/.github/workflows/"
}
copy_flow "$tmp/ok"
expect pass "identical copy" -- bash "$drift" "$here" "$tmp/ok"
copy_flow "$tmp/t"
echo "- [ ] extra" >>"$tmp/t/.github/PULL_REQUEST_TEMPLATE.md"
expect fail "edited PR template" -- bash "$drift" "$here" "$tmp/t"
copy_flow "$tmp/a"
sed -i 's/Never merge red\./Merge red if in a hurry./' "$tmp/a/AGENTS.md"
expect fail "edited AGENTS flow block" -- bash "$drift" "$here" "$tmp/a"
copy_flow "$tmp/n"
printf '# AGENTS.md\n' >"$tmp/n/AGENTS.md"
expect fail "AGENTS.md without flow block" -- bash "$drift" "$here" "$tmp/n"
copy_flow "$tmp/w"
rm "$tmp/w/.github/workflows/flow.yml"
expect fail "missing caller workflow" -- bash "$drift" "$here" "$tmp/w"

echo
if [ $fails -ne 0 ]; then
	echo "flow-selftest: $fails case(s) behaved wrongly"
	exit 1
fi
echo "flow-selftest: all cases behaved"
