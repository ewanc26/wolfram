#!/usr/bin/env bash
# labels-sync.sh -- check or apply .github/labels.yml.
# Usage: tools/labels-sync.sh check|apply [OWNER/REPO] [--dry-run]
# Defaults to the repo of the current checkout. Needs gh and python3 with PyYAML.
set -eu
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:?usage: labels-sync.sh check|apply [OWNER/REPO] [--dry-run]}"; shift
repo="${1:-}"; [ "${repo#--}" = "$repo" ] && [ -n "$repo" ] && shift || repo=""
[ -n "$repo" ] || repo="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
canon="${WOLFRAM_CANON:-$here}/.github/labels.yml"
exec python3 "$here/tools/repo_sync.py" labels "$mode" --repo "$repo" --canon "$canon" "$@"
