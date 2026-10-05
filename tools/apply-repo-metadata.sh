#!/usr/bin/env bash
# apply-repo-metadata.sh -- apply .github/repo-metadata.yml (from each repo's
# main) to all five repos: PATCH repos/{o}/{r} and PUT .../topics. The agents'
# sandbox cannot write repository metadata, so the owner runs this locally:
#
#   tools/apply-repo-metadata.sh            # apply to all five
#   tools/apply-repo-metadata.sh --dry-run  # show what would change
#
# Needs gh (authenticated with a token that can administer the repos) and
# python3 with PyYAML. It also applies the labels from each repo's labels.yml.
set -eu
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
owner="${OWNER:-ewanc26}"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
fetch() { gh api "repos/$owner/$1/contents/$2?ref=main" --jq .content | base64 -d >"$tmp/$1.$3"; }
for r in wolfram metalbear cobalt indigo platinum; do
	echo ">> $owner/$r"
	fetch "$r" .github/repo-metadata.yml meta.yml
	fetch "$r" .github/labels.yml labels.yml
	python3 "$here/tools/repo_sync.py" metadata check --file "$tmp/$r.meta.yml" --canon "$tmp/wolfram.meta.yml"
	python3 "$here/tools/repo_sync.py" metadata apply --repo "$owner/$r" --file "$tmp/$r.meta.yml" "$@"
	python3 "$here/tools/repo_sync.py" labels apply --repo "$owner/$r" --file "$tmp/$r.labels.yml" "$@"
done
