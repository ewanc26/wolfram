#!/usr/bin/env bash
#
# release.sh -- cut a Wolfram release in two steps, with a pull request between.
#
#   tools/release.sh prepare [--dry-run] [--full] [--consumers-verified] <major|minor|patch|x.y.z>
#   tools/release.sh publish [--dry-run] <x.y.z>
#
# prepare  On a clean main equal to origin/main: runs the default build and
#          ctest (and the full-features one with --full), bumps VERSION in
#          project(), moves CHANGELOG.md's Unreleased section into a dated
#          version section, commits that on release/vX.Y.Z and pushes the
#          branch. Then open a PR from it and rebase-merge it once CI is green.
#          --consumers-verified is required: it asserts metalbear, cobalt,
#          indigo and platinum build against this change, which the script
#          cannot check for you.
# publish  After the release PR is merged: finds the bump commit on
#          origin/main, requires a green `CI gate` check on that exact commit,
#          creates the GitHub release for vX.Y.Z at that commit (GitHub makes
#          the tag; no `git push` of a tag), with the changelog section as
#          notes, a source tarball and its SHA-256 attached.
#
# Requires: git, cmake, a C/C++ toolchain, python3, gh (REST access).
#
# Why it is shaped like this:
#
#   * Nothing goes straight to main. The bump travels as a PR like any change.
#     prepare and publish are separate because the PR is created and merged in
#     between (by hand or with the GitHub tools); the sandbox the agents use can
#     reach GitHub's REST API but not GraphQL, which `gh pr` needs.
#   * The version lives in exactly one place: VERSION in project() in
#     CMakeLists.txt. cmake_minimum_required() also says VERSION, so this
#     reads the project() block, not the first match.
#   * Release notes are CHANGELOG.md's section for the version, written by hand
#     as the PRs land, not generated from commit subjects.
#   * A tag is never moved and a version is never re-tagged. A wrong release is
#     fixed by a new patch release.
#   * Wolfram does not update itself, so there is no update manifest; the
#     assets are the source tarball and its SHA-256 for anyone who wants to
#     verify a download.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_DIR}"

BUILD_DIR="${BUILD_DIR:-build-release}"
FULL_BUILD_DIR="${FULL_BUILD_DIR:-build-release-full}"

usage() {
	sed -n '4,6p' "${BASH_SOURCE[0]}" | sed 's/^# *//' >&2
	exit 2
}
fail() {
	echo "release: $*" >&2
	exit 1
}

cmd="${1:-}"
[ -n "$cmd" ] || usage
shift
dry_run=0 run_full=0 consumers_verified=0 arg=""
for a in "$@"; do
	case "$a" in
		--dry-run) dry_run=1 ;;
		--full) run_full=1 ;;
		--consumers-verified) consumers_verified=1 ;;
		-h | --help) usage ;;
		-*) fail "unknown option $a" ;;
		*) arg="$a" ;;
	esac
done
[ -n "$arg" ] || usage

command -v cmake >/dev/null || fail "cmake not found"
command -v gh >/dev/null || fail "gh not found"
command -v python3 >/dev/null || fail "python3 not found"

read_version() { # [ref]  VERSION from project() in CMakeLists.txt
	local src
	if [ -n "${1:-}" ]; then src="$(git show "$1:CMakeLists.txt")"; else src="$(cat CMakeLists.txt)"; fi
	printf '%s\n' "$src" | awk '/^project\(/,/\)[[:space:]]*$/' |
		sed -n 's/.*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' | head -1
}

repo_slug() {
	local u p
	u="$(git remote get-url origin)"
	case "$u" in *://* | *@*:*) ;; *) fail "origin ($u) is not a network remote" ;; esac
	p="${u%.git}"; p="${p#*://}"; p="${p#*@}"
	case "$p" in *:*/*) p="${p#*:}" ;; */*/*) p="${p#*/}" ;; esac
	[[ "$p" == */* && "$p" != */*/* ]] || fail "could not read owner/repo from origin ($u)"
	echo "$p"
}

# Print the body of CHANGELOG.md's section for version $1 (from file $2).
changelog_section() {
	python3 - "$1" "$2" <<-'PY'
	import re, sys
	v, path = sys.argv[1], sys.argv[2]
	t = open(path, encoding="utf-8").read()
	m = re.search(r"^## \[" + re.escape(v) + r"\][^\n]*\n(.*?)(?=^## \[|^\[[^\]]+\]: |\Z)", t, re.M | re.S)
	if not m: sys.exit(f"no ## [{v}] section in CHANGELOG.md")
	print(m.group(1).strip())
	PY
}

# Move Unreleased into "## [new] - date", leave an empty Unreleased, fix links.
changelog_cut() {
	python3 - "$1" "$2" "$3" <<-'PY'
	import re, sys, datetime
	new, prev, slug = sys.argv[1:4]
	path = "CHANGELOG.md"
	t = open(path, encoding="utf-8").read()
	m = re.search(r"^## \[Unreleased\][^\n]*\n(.*?)(?=^## \[|^\[[^\]]+\]: |\Z)", t, re.M | re.S)
	if not m: sys.exit("CHANGELOG.md has no ## [Unreleased] section")
	body = m.group(1).strip()
	if not re.search(r"^- ", body, re.M): sys.exit("Unreleased has no entries; nothing to release")
	today = datetime.date.today().isoformat()
	t = t[:m.start()] + "## [Unreleased]\n\n## [%s] - %s\n\n%s\n\n" % (new, today, body) + t[m.end():]
	url = "https://github.com/" + slug
	t = re.sub(r"^\[Unreleased\]: .*$", "[Unreleased]: %s/compare/v%s...HEAD\n[%s]: %s/releases/tag/v%s" % (url, new, new, url, new), t, count=1, flags=re.M)
	open(path, "w", encoding="utf-8").write(t)
	PY
}

jobs() { sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4; }
run_config() {
	local dir="$1" label="$2"; shift 2
	echo ">> ${label}: configure, build, ctest in ${dir}"
	cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE=Debug "$@" >/dev/null || fail "${label}: configure failed"
	cmake --build "$dir" -j"$(jobs)" >/dev/null || fail "${label}: build failed"
	ctest --test-dir "$dir" --output-on-failure -j"$(jobs)" >/dev/null || fail "${label}: ctest failed"
}

# ---------------------------------------------------------------------------
prepare() {
	local bump="$1" current new ma mi pa prev slug branch_name
	((dry_run)) || ((consumers_verified)) ||
		fail "refusing to prepare: pass --consumers-verified once metalbear, cobalt, indigo and platinum build against this change (or use --dry-run)"
	[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || fail "must be on main"
	[ -z "$(git status --porcelain)" ] || fail "working tree is not clean"
	git fetch origin --tags --prune --quiet
	[ "$(git rev-parse main)" = "$(git rev-parse origin/main)" ] || fail "local main differs from origin/main; sync first"
	current="$(read_version)"
	[ -n "$current" ] || fail "could not read the project VERSION"
	case "$bump" in
		major | minor | patch)
			IFS=. read -r ma mi pa <<<"$current"
			case "$bump" in
				major) new="$((ma + 1)).0.0" ;;
				minor) new="$ma.$((mi + 1)).0" ;;
				patch) new="$ma.$mi.$((pa + 1))" ;;
			esac ;;
		*) new="$bump" ;;
	esac
	[[ "$new" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "bad version: $new"
	[ "$new" != "$current" ] || fail "version is already $current"
	git rev-parse -q --verify "refs/tags/v$new" >/dev/null && fail "tag v$new already exists"
	slug="$(repo_slug)"
	echo ">> Preparing ${current} -> ${new} for ${slug}"

	run_config "$BUILD_DIR" "default build"
	if ((run_full)); then
		run_config "$FULL_BUILD_DIR" "full-features build" -DWOLFRAM_BUILD_SERVER=ON \
			-DWOLFRAM_BUILD_STORE=ON -DWOLFRAM_BUILD_STORE_CRYPTO=ON -DWOLFRAM_BUILD_CPP=ON
	fi

	branch_name="release/v${new}"
	git switch -q -c "$branch_name"
	# From here a failure must not leave a half-made release branch behind.
	trap 'git checkout -q -- CMakeLists.txt CHANGELOG.md 2>/dev/null; git switch -q main; git branch -q -D "'"$branch_name"'" 2>/dev/null || true' ERR
	python3 - "$new" <<-'PY'
	import re, sys
	want = sys.argv[1]
	lines = open("CMakeLists.txt").read().split("\n")
	start = next(i for i, l in enumerate(lines) if l.startswith("project("))
	end = next(i for i in range(start, len(lines)) if lines[i].rstrip().endswith(")"))
	for i in range(start, end + 1):
	    new, n = re.subn(r"(VERSION\s+)[0-9][0-9.]*", r"\g<1>" + want, lines[i])
	    if n: lines[i] = new; break
	else: sys.exit("no VERSION line inside project()")
	open("CMakeLists.txt", "w").write("\n".join(lines))
	PY
	[ "$(read_version)" = "$new" ] || fail "bump did not take effect"
	changelog_cut "$new" "$current" "$slug" || fail "could not cut the changelog"
	python3 tools/changelog-check.py drift . --allow-untagged "$new" || fail "changelog drift after the cut"

	if ((dry_run)); then
		echo ">> Dry run. Release notes would be:"; echo
		changelog_section "$new" CHANGELOG.md
		git checkout -q -- CMakeLists.txt CHANGELOG.md; git switch -q main; git branch -q -D "$branch_name"
		trap - ERR
		echo; echo ">> Dry run complete; nothing committed or pushed."
		return
	fi
	git add CMakeLists.txt CHANGELOG.md
	git commit -q -m "chore(release): v${new}" -m "Version bump and changelog cut for v${new}." \
		-m "${RELEASE_TRAILERS:-Co-Authored-By: release.sh <noreply@github.com>}"
	trap - ERR
	git push -u origin "$branch_name"
	git switch -q main
	cat <<-EOF2

	>> Pushed ${branch_name}. Next:
	   1. Open a PR from ${branch_name} to main titled "chore(release): v${new}",
	      saying which consumer builds were verified and where.
	   2. Rebase-merge it once CI is green.
	   3. tools/release.sh publish ${new}
	EOF2
}

# ---------------------------------------------------------------------------
publish() {
	local new="$1" slug sha tmp notes gate rel_id asset
	[[ "$new" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "bad version: $new"
	git fetch origin --tags --prune --quiet
	git rev-parse -q --verify "refs/tags/v$new" >/dev/null && fail "tag v$new already exists; never re-tag, cut a new patch instead"
	slug="$(repo_slug)"
	# The release commit is the first commit on main that carries the version.
	sha=""
	for c in $(git rev-list --reverse origin/main -- CMakeLists.txt); do
		if [ "$(read_version "$c")" = "$new" ]; then sha="$c"; break; fi
	done
	[ -n "$sha" ] || fail "no commit on origin/main sets VERSION ${new}; merge the release PR first"
	gate="$(gh api "repos/${slug}/commits/${sha}/check-runs?per_page=100" \
		--jq '[.check_runs[] | select(.name == "CI gate")] | sort_by(.completed_at) | last | (.status + " " + (.conclusion // ""))')"
	[ "$gate" = "completed success" ] || fail "CI gate on ${sha} is '${gate:-absent}', not green; not tagging"
	tmp="$(mktemp -d)"
	git show "${sha}:CHANGELOG.md" >"$tmp/CHANGELOG.md"
	notes="$(changelog_section "$new" "$tmp/CHANGELOG.md")"
	echo ">> Releasing v${new} at ${sha} (CI gate green)"
	if ((dry_run)); then
		echo ">> Dry run. Notes:"; echo; echo "$notes"; rm -rf "$tmp"; return
	fi
	# The release call creates the tag at $sha server-side. Some environments
	# (the agents' sandbox) refuse `git push` of a tag but allow the REST API, and
	# one call cannot leave a tag without a release or the reverse.
	rel_id="$(gh api -X POST "repos/${slug}/releases" -f tag_name="v${new}" \
		-f target_commitish="$sha" -f name="v${new}" -f body="$notes" --jq .id)" ||
		fail "could not create the release (and so no tag): nothing was published"
	git fetch -q origin "refs/tags/v${new}:refs/tags/v${new}" ||
		echo "release: created v${new}; fetch the tag with 'git fetch --tags'" >&2
	asset="wolfram-${new}.tar.gz"
	git archive --format=tar.gz --prefix="wolfram-${new}/" -o "$tmp/$asset" "v${new}"
	(cd "$tmp" && { sha256sum "$asset" 2>/dev/null || shasum -a 256 "$asset"; } >"$asset.sha256")
	for f in "$asset" "$asset.sha256"; do
		gh api -X POST "https://uploads.github.com/repos/${slug}/releases/${rel_id}/assets?name=${f}" \
			-H "Content-Type: application/octet-stream" --input "$tmp/$f" --jq .name >/dev/null ||
			echo "release: could not upload ${f}; the release exists without it" >&2
	done
	rm -rf "$tmp"
	echo ">> Released https://github.com/${slug}/releases/tag/v${new}"
}

case "$cmd" in
	prepare) prepare "$arg" ;;
	publish) publish "$arg" ;;
	*) usage ;;
esac
