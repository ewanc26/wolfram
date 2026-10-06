#!/usr/bin/env bash
#
# changelog-selftest.sh -- prove tools/changelog-check.py fails on each
# violation, in a throwaway repository.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
chk="$here/tools/changelog-check.py"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
fails=0
expect() { # expect pass|fail <name> <cmd...>
	local want="$1" name="$2"; shift 2
	"$@" >"$tmp/out" 2>&1; local rc=$?
	if { [ "$want" = pass ] && [ $rc -eq 0 ]; } || { [ "$want" = fail ] && [ $rc -ne 0 ]; }; then
		echo "ok   $want  $name"
	else
		echo "FAIL expected $want, rc=$rc: $name"; sed 's/^/       /' "$tmp/out"; fails=$((fails + 1))
	fi
}
r="$tmp/r"; mkdir -p "$r/src" "$r/docs"; cd "$r" || exit 1
git init -q; git config user.email t@example.invalid; git config user.name t
good='# Changelog

## [Unreleased]

### Added

- Something new. (#1)

## [0.1.0] - 2026-01-01

First.

[Unreleased]: https://example.invalid/compare/v0.1.0...HEAD
'
printf '%s' "$good" >CHANGELOG.md; echo a >src/a.c; echo d >docs/d.md
git add -A; git commit -qm base; git tag v0.1.0; base=$(git rev-parse HEAD)

commit() { git add -A; git commit -qm "$1"; git rev-parse HEAD; }
br() { git switch -q -C "$1" "$base"; }

br code-only; echo b >>src/a.c; h=$(commit "fix: code")
expect fail "code change without a changelog entry" python3 "$chk" pr "$base" "$h"
expect pass "escape hatch with a reason" env PR_BODY=$'Changelog: none — internal rename, no behaviour change' python3 "$chk" pr "$base" "$h"
expect fail "escape hatch without a reason" env PR_BODY='Changelog: none' python3 "$chk" pr "$base" "$h"

br with-entry; echo b >>src/a.c; sed -i 's/^- Something new. (#1)$/- Something new. (#1)\n- A fix. (#2)/' CHANGELOG.md; h=$(commit "fix: code and entry")
expect pass "code change with an Unreleased entry" python3 "$chk" pr "$base" "$h"

br wrong-section; echo b >>src/a.c; sed -i 's/^First\.$/First, edited./' CHANGELOG.md; h=$(commit "fix: edit old section")
expect fail "changelog edited only in a released section" python3 "$chk" pr "$base" "$h"

br docs-only; echo e >>docs/d.md; mkdir -p test .github; echo t >test/t.c; echo w >.github/w.yml; h=$(commit "docs: only")
expect pass "docs, tests and CI need no entry" python3 "$chk" pr "$base" "$h"

br drift; expect pass "changelog in step with tags" python3 "$chk" drift .
git tag v0.2.0
expect fail "tag without a section" python3 "$chk" drift .
git tag -d v0.2.0 >/dev/null
printf '%s' "$good" | sed 's/^## \[0.1.0\]/## [0.3.0] - 2026-02-01\n\nNext.\n\n## [0.1.0]/' >CHANGELOG.md
expect fail "section without a tag" python3 "$chk" drift .
expect pass "newest section awaiting its tag, in a PR (--pending-ok)" python3 "$chk" drift . --pending-ok
printf '%s' "$good" | sed 's/^## \[0.1.0\]/## [0.3.0] - 2026-02-01\n\nNext.\n\n## [0.2.0] - 2026-01-15\n\nMiddle.\n\n## [0.1.0]/' >CHANGELOG.md
expect fail "an older section without a tag is still an error (--pending-ok)" python3 "$chk" drift . --pending-ok
printf '%s' "$good" | sed 's/^## \[0.1.0\]/## [0.3.0] - 2026-02-01\n\nNext.\n\n## [0.1.0]/' >CHANGELOG.md
expect pass "section without a tag on its own release branch" python3 "$chk" drift . --allow-untagged 0.3.0
printf '%s' "$good" | sed 's/### Added/### New stuff/' >CHANGELOG.md
expect fail "unknown subsection" python3 "$chk" drift .
printf '%s' "$good" | sed 's/ (#1)//' >CHANGELOG.md
expect fail "entry without a PR or issue link" python3 "$chk" drift .
printf '%s' "$good" | python3 -c "import sys;t=sys.stdin.read();print(t.replace('## [Unreleased]\n','',1))" >CHANGELOG.md
expect fail "no Unreleased section first" python3 "$chk" drift .
rm CHANGELOG.md
expect fail "no CHANGELOG.md" python3 "$chk" drift .

echo
if [ $fails -ne 0 ]; then echo "changelog-selftest: $fails case(s) behaved wrongly"; exit 1; fi
echo "changelog-selftest: all cases behaved"
