#!/usr/bin/env python3
"""release-check.py -- gate a release tag before it is published.

Checks, in order:
  * TAG is vMAJOR.MINOR.PATCH and VERSION (when supplied) is the same version.
  * CHANGELOG has a non-empty `## [VERSION]` section.
  * COMMIT is on origin/main.
  * every comma-separated REQUIRED_CHECKS check run on COMMIT succeeded; when
    --wait-seconds is non-zero, a pending run is waited for.

The version is taken from --version when supplied, otherwise from TAG. The
version file is not read here because each repository keeps it somewhere else;
callers that want that check pass the value they read.

Usage:
  release-check.py --tag v1.2.3 [--version 1.2.3] [--changelog CHANGELOG.md]
                   [--required-checks "CI gate,host-tests"] [--wait-seconds 0]
                   [--repo OWNER/REPO] [--sha REV] [--api-url URL]
                   [--skip-ancestry] [--skip-checks]

Exit 0 with a one-line summary, or 1 with every problem on stderr.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+)$")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
DEFAULT_API = "https://api.github.com"


def split_checks(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def resolve_version(tag: str, version: str) -> tuple[str, list[str]]:
    problems: list[str] = []
    match = TAG_RE.fullmatch(tag)
    if match is None:
        problems.append(f"tag {tag!r} is not vMAJOR.MINOR.PATCH")
        derived = ""
    else:
        derived = match.group(1)
    if version:
        if not VERSION_RE.fullmatch(version):
            problems.append(f"version {version!r} is not MAJOR.MINOR.PATCH")
        elif derived and version != derived:
            problems.append(f"tag {tag!r} names version {derived}, not {version}")
    return (version or derived), problems


def changelog_problems(path: str, version: str) -> list[str]:
    if not version:
        return ["no version to look up in the changelog"]
    try:
        with open(path, encoding="utf-8") as changelog:
            text = changelog.read()
    except OSError as error:
        return [f"cannot read {path}: {error.strerror}"]

    pattern = re.compile(
        r"^## \[" + re.escape(version) + r"\][^\n]*\n(.*?)(?=^## \[|^\[[^\]]+\]: |\Z)",
        re.M | re.S,
    )
    match = pattern.search(text)
    if match is None:
        return [f"{path} has no `## [{version}]` section"]
    body = match.group(1).strip()
    if not body:
        return [f"{path} has an empty `## [{version}]` section"]
    if not any(line.strip() and not line.strip().startswith("#") for line in body.splitlines()):
        return [f"{path}'s `## [{version}]` section has no content outside its headings"]
    return []


def resolve_commit(sha: str, cwd: str) -> str:
    return subprocess.run(
        ["git", "rev-parse", f"{sha}^{{commit}}"],
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def on_main(sha: str, cwd: str) -> tuple[bool, str]:
    commit = resolve_commit(sha, cwd)
    subprocess.run(["git", "fetch", "--quiet", "origin", "main"], cwd=cwd, check=True)
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "origin/main"], cwd=cwd
    )
    return (result.returncode == 0, commit)


def fetch_check_runs(repo: str, sha: str, token: str, api_url: str) -> list[dict]:
    url = f"{api_url.rstrip('/')}/repos/{repo}/commits/{sha}/check-runs?per_page=100"
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": "wolfram-release-check",
        },
    )
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = json.load(response)
    except urllib.error.HTTPError as error:
        raise RuntimeError(f"GitHub API answered HTTP {error.code} for {repo}") from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"cannot reach the GitHub API: {error.reason}") from error
    runs = payload.get("check_runs", [])
    if not isinstance(runs, list):
        raise RuntimeError("the GitHub API answer has no check_runs list")
    return runs


def evaluate_checks(runs: list[dict], required: list[str]) -> tuple[str, str]:
    """Return (state, detail), where state is success, pending or failure."""
    if not required:
        return "success", "no required checks"
    by_name: dict[str, list[dict]] = {}
    for run in runs:
        if isinstance(run, dict) and isinstance(run.get("name"), str):
            by_name.setdefault(run["name"], []).append(run)

    pending: list[str] = []
    failed: list[str] = []
    for name in required:
        matches = by_name.get(name, [])
        if any(
            run.get("status") == "completed" and run.get("conclusion") == "success"
            for run in matches
        ):
            continue
        incomplete = [
            run for run in matches if run.get("status") != "completed"
        ]
        if incomplete:
            pending.append(f"{name} (running)")
        elif matches:
            conclusions = ", ".join(str(run.get("conclusion")) for run in matches)
            failed.append(f"{name} ({conclusions})")
        else:
            pending.append(f"{name} (not created yet)")
    if failed:
        return "failure", "; ".join(failed)
    if pending:
        return "pending", "; ".join(pending)
    return "success", f"{len(required)} required check(s) passed"


def wait_for_checks(
    repo: str,
    sha: str,
    required: list[str],
    token: str,
    api_url: str,
    wait_seconds: int,
) -> None:
    deadline = time.monotonic() + max(0, wait_seconds)
    while True:
        runs = fetch_check_runs(repo, sha, token, api_url)
        state, detail = evaluate_checks(runs, required)
        if state == "success":
            return
        if state == "failure":
            raise RuntimeError(f"required checks did not pass: {detail}")
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError(
                f"required checks were not successful after {max(0, wait_seconds)}s: {detail}"
            )
        time.sleep(min(10.0, max(1.0, remaining)))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", required=True)
    parser.add_argument("--version", default="")
    parser.add_argument("--changelog", default="CHANGELOG.md")
    parser.add_argument("--required-checks", default="")
    parser.add_argument("--wait-seconds", type=int, default=0)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--sha", default=os.environ.get("GITHUB_SHA", "HEAD"))
    parser.add_argument("--api-url", default=DEFAULT_API)
    parser.add_argument("--skip-ancestry", action="store_true")
    parser.add_argument("--skip-checks", action="store_true")
    args = parser.parse_args(argv)

    version, problems = resolve_version(args.tag, args.version)
    problems += changelog_problems(args.changelog, version)

    sha = args.sha
    if not args.skip_ancestry:
        try:
            ok, sha = on_main(sha, ".")
        except (subprocess.CalledProcessError, FileNotFoundError) as error:
            problems.append(f"cannot check that {args.sha} is on origin/main: {error}")
        else:
            if not ok:
                problems.append(f"{sha} is not on origin/main")

    required = split_checks(args.required_checks)
    if required and not args.skip_checks:
        if not args.repo:
            problems.append("--repo (or GITHUB_REPOSITORY) is required to check check-runs")
        elif args.wait_seconds < 0:
            problems.append("--wait-seconds cannot be negative")
        else:
            token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN", "")
            try:
                wait_for_checks(
                    args.repo,
                    sha,
                    required,
                    token,
                    args.api_url,
                    args.wait_seconds,
                )
            except RuntimeError as error:
                problems.append(str(error))

    if problems:
        for problem in problems:
            print(f"release-check: {problem}", file=sys.stderr)
        return 1
    print(
        f"release-check: {args.tag} ({version}) is on main, has a changelog section"
        + (f" and {len(required)} green required check(s)" if required else "")
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
