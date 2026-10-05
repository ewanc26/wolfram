#!/usr/bin/env bash
#
# flow-check.sh -- enforce the unified flow on one pull request.
#
# Inputs (environment, so PR text never reaches a shell unquoted):
#   PR_TITLE   pull request title
#   PR_BRANCH  head branch name
#   PR_BODY    pull request description
#   BASE_SHA / HEAD_SHA (optional)  when both are set and resolve in the
#              current repository, commits in the range must not be empty.
#
# The rules live in docs/flow.md. Exit 0 when all hold, 1 otherwise; every
# violation is printed, not just the first.
set -u

types='feat|fix|docs|ci|chore|refactor|test|perf|build|release'
fail=0
bad() {
	echo "flow: $*" >&2
	fail=1
}

title="${PR_TITLE-}"
branch="${PR_BRANCH-}"
body="${PR_BODY-}"

# Title: Conventional Commits. A squash merge uses it as the commit subject.
if ! [[ "$title" =~ ^(${types}|revert)(\([a-z0-9._/-]+\))?!?:\ [^[:space:]].*$ ]] || ((${#title} > 100)); then
	bad "PR title must be 'type(scope): summary' (type: ${types}|revert; at most 100 characters), got: ${title}"
fi

# Branch: type/slug, so the title and the branch say the same kind of thing.
if ! [[ "$branch" =~ ^(${types})/[a-z0-9][a-z0-9._-]*$ ]]; then
	bad "branch must be 'type/slug' (type: ${types}; slug lowercase a-z 0-9 . _ -), got: ${branch}"
fi

# Body: the three template sections exist and are not just the template's
# HTML-comment placeholders.
section_text() {
	# Print the text under "## $1", minus HTML comments and blank lines.
	printf '%s\n' "$body" | tr -d '\r' | awk -v h="## $1" '
		$0 == h { on = 1; next }
		/^## / { on = 0 }
		!on { next }
		{
			line = $0
			out = ""
			while (length(line) > 0) {
				if (inc) {
					i = index(line, "-->")
					if (i == 0) { line = ""; break }
					line = substr(line, i + 3); inc = 0
				} else {
					i = index(line, "<!--")
					if (i == 0) { out = out line; line = ""; break }
					out = out substr(line, 1, i - 1); line = substr(line, i + 4); inc = 1
				}
			}
			if (out ~ /[^[:space:]]/) print out
		}
	'
}
for s in "What this changes" "Verification" "Docs"; do
	if ! printf '%s\n' "$body" | tr -d '\r' | grep -qx "## $s"; then
		bad "PR body is missing the '## $s' section (use .github/PULL_REQUEST_TEMPLATE.md)"
	elif [ -z "$(section_text "$s")" ]; then
		bad "PR body section '## $s' is empty"
	fi
done

# Commits: an empty commit is never a fix.
if [ -n "${BASE_SHA-}" ] && [ -n "${HEAD_SHA-}" ] &&
	git cat-file -e "${BASE_SHA}^{commit}" 2>/dev/null && git cat-file -e "${HEAD_SHA}^{commit}" 2>/dev/null; then
	while IFS= read -r c; do
		[ -n "$c" ] || continue
		if [ -z "$(git diff-tree --root --no-commit-id --name-only -r "$c")" ]; then
			bad "commit $(git rev-parse --short "$c") is empty"
		fi
	done < <(git rev-list --no-merges "${BASE_SHA}..${HEAD_SHA}")
fi

((fail == 0)) && echo "flow: conventions ok"
exit "$fail"
