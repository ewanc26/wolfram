#!/usr/bin/env bash
#
# repo-sync-selftest.sh -- prove tools/repo_sync.py rejects each kind of
# violation in labels.yml, repo-metadata.yml and the live data they describe.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tool="$here/tools/repo_sync.py"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
fails=0

expect() { # expect pass|fail <name> <command...>
	local want="$1" name="$2"; shift 2
	"$@" >"$tmp/out" 2>&1; local rc=$?
	if { [ "$want" = pass ] && [ $rc -eq 0 ]; } || { [ "$want" = fail ] && [ $rc -ne 0 ]; }; then
		echo "ok   $want  $name"
	else
		echo "FAIL expected $want, rc=$rc: $name"; sed 's/^/       /' "$tmp/out"; fails=$((fails + 1))
	fi
}
py() { python3 -c "$1"; }

# Live data that matches the files exactly.
py "
import yaml, json
d = yaml.safe_load(open('$here/.github/labels.yml'))
live = [dict(l) for l in d['core'] + d['local']]
json.dump(live, open('$tmp/live-labels.json', 'w'))
m = yaml.safe_load(open('$here/.github/repo-metadata.yml'))
json.dump({'description': m['repo']['description'], 'homepage': '', 'topics': sorted(set(m['core']['topics']) | set(m['repo']['topics'])), 'has_wiki': False, 'has_discussions': False, 'has_projects': False}, open('$tmp/live-meta.json', 'w'))
"
L="$here/.github/labels.yml"; M="$here/.github/repo-metadata.yml"
labels() { python3 "$tool" labels check --file "$1" --canon "$L" ${2:+--live-json "$2"}; }
meta() { python3 "$tool" metadata check --file "$1" --canon "$M" ${2:+--live-json "$2"}; }
mut() { # mut <src> <dst> <python edit on d>
	python3 - "$1" "$2" "$3" <<'PY'
import sys, yaml
src, dst, edit = sys.argv[1:4]
d = yaml.safe_load(open(src)); exec(edit)
yaml.safe_dump(d, open(dst, 'w'), sort_keys=False)
PY
}
mutjson() { python3 - "$1" "$2" "$3" <<'PY'
import sys, json
src, dst, edit = sys.argv[1:4]
d = json.load(open(src)); exec(edit)
json.dump(d, open(dst, 'w'))
PY
}

echo "-- labels"
expect pass "labels file and live data agree" labels "$L" "$tmp/live-labels.json"
mut "$L" "$tmp/a.yml" "d['core'][0]['color']='000000'"
expect fail "core edited in a repo's copy" labels "$tmp/a.yml"
mut "$L" "$tmp/b.yml" "d['local'].append({'name':'bugfix','color':'ffffff','description':'x'})"
expect fail "local label outside area:/platform:" labels "$tmp/b.yml"
mut "$L" "$tmp/c.yml" "d['local'].append({'name':'area: ui','color':'1d76db','description':'dup'})"
expect fail "duplicate label name" labels "$tmp/c.yml"
mut "$L" "$tmp/d.yml" "d['local'].append({'name':'area: games','color':'ff0000','description':'x'})"
expect fail "area label in a second colour" labels "$tmp/d.yml"
mut "$L" "$tmp/e.yml" "d['local'].append({'name':'area: games','color':'1d76db','description':'x'*101})"
expect fail "description over 100 characters" labels "$tmp/e.yml"
mut "$L" "$tmp/f.yml" "d['local'].append({'name':'area: games','color':'1d76db','description':'Game logic'})"
expect pass "a repo may add an area through local" labels "$tmp/f.yml"
mutjson "$tmp/live-labels.json" "$tmp/l1.json" "d.pop(3)"
expect fail "live repo is missing a label" labels "$L" "$tmp/l1.json"
mutjson "$tmp/live-labels.json" "$tmp/l2.json" "d[0]['color']='ffffff'"
expect fail "live label has the wrong colour" labels "$L" "$tmp/l2.json"
mutjson "$tmp/live-labels.json" "$tmp/l3.json" "d[0]['description']='changed'"
expect fail "live label has the wrong description" labels "$L" "$tmp/l3.json"
mutjson "$tmp/live-labels.json" "$tmp/l4.json" "d.append({'name':'accessibility','color':'ededed','description':''})"
expect fail "live repo has an undefined label (old name)" labels "$L" "$tmp/l4.json"

echo "-- metadata"
expect pass "metadata file and live data agree" meta "$M" "$tmp/live-meta.json"
mut "$M" "$tmp/m1.yml" "d['core']['topics'].append('extra')"
expect fail "core topics edited in a repo's copy" meta "$tmp/m1.yml"
mut "$M" "$tmp/m2.yml" "d['repo']['description']='two\nlines.'"
expect fail "multi-line description" meta "$tmp/m2.yml"
mut "$M" "$tmp/m3.yml" "d['repo']['description']='no full stop'"
expect fail "description without a full stop" meta "$tmp/m3.yml"
mut "$M" "$tmp/m4.yml" "d['repo']['topics']=['t%d'%i for i in range(30)]"
expect fail "more than 20 topics" meta "$tmp/m4.yml"
mut "$M" "$tmp/m5.yml" "d['repo']['topics'].append('Bad Topic')"
expect fail "invalid topic" meta "$tmp/m5.yml"
mutjson "$tmp/live-meta.json" "$tmp/v1.json" "d['has_wiki']=True"
mut "$M" "$tmp/enf.yml" "d['repo']['enforce_live']=True"
expect pass "live drift only warns until enforce_live" meta "$M" "$tmp/v1.json"
expect fail "live drift fails once enforce_live is set" meta "$tmp/enf.yml" "$tmp/v1.json"
mutjson "$tmp/live-meta.json" "$tmp/v2.json" "d['topics'].pop()"
expect fail "live topics differ (enforced)" meta "$tmp/enf.yml" "$tmp/v2.json"

echo "-- issue forms"
F="$here/.github/ISSUE_TEMPLATE"
forms() { python3 "$tool" forms check --repo "$1" --canon "$F" --file "$2" --labels "${3:-$L}"; }
rm -rf "$tmp/f1"; cp -r "$F" "$tmp/f1"
expect pass "forms equal the canonical copy" forms ewanc26/wolfram "$tmp/f1"
sed -i 's/What I found/What I noticed/' "$tmp/f1/bug.yml"
expect fail "a form's field edited in a repo" forms ewanc26/wolfram "$tmp/f1"
rm -rf "$tmp/f2"; cp -r "$F" "$tmp/f2"; rm "$tmp/f2/chore.yml"
expect fail "a canonical form is missing" forms ewanc26/wolfram "$tmp/f2"
rm -rf "$tmp/f3"; cp -r "$F" "$tmp/f3"; echo "name: old" >"$tmp/f3/bug_report.md"
expect fail "an old non-canonical template is left behind" forms ewanc26/wolfram "$tmp/f3"
rm -rf "$tmp/f4"; cp -r "$F" "$tmp/f4"; sed -i 's/blank_issues_enabled: false/blank_issues_enabled: true/' "$tmp/f4/config.yml"
expect fail "blank issues enabled" forms ewanc26/wolfram "$tmp/f4"
mut "$L" "$tmp/lg.yml" "d['local'].append({'name':'area: games','color':'1d76db','description':'Game logic'})"
rm -rf "$tmp/f5"; python3 "$tool" forms apply --repo ewanc26/wolfram --canon "$F" --file "$tmp/f5" --labels "$tmp/lg.yml" >/dev/null
expect pass "a repo's extra area appears in its dropdown only" forms ewanc26/wolfram "$tmp/f5" "$tmp/lg.yml"
expect fail "the extra area is missing from the dropdown" forms ewanc26/wolfram "$tmp/f1" "$tmp/lg.yml"
rm -rf "$tmp/f6"; python3 "$tool" forms apply --repo ewanc26/cobalt --canon "$F" --file "$tmp/f6" >/dev/null
expect pass "another repo's config.yml names that repo" forms ewanc26/cobalt "$tmp/f6"
grep -q "ewanc26/cobalt/security/advisories/new" "$tmp/f6/config.yml" && echo "ok   pass  advisory link points at the repo itself" || { echo "FAIL advisory link"; fails=$((fails + 1)); }

echo
if [ $fails -ne 0 ]; then echo "repo-sync-selftest: $fails case(s) behaved wrongly"; exit 1; fi
echo "repo-sync-selftest: all cases behaved"
