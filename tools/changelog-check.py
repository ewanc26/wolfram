#!/usr/bin/env python3
"""changelog-check.py -- keep CHANGELOG.md current (Keep a Changelog style).

  changelog-check.py pr     BASE_SHA HEAD_SHA   (PR body in $PR_BODY)
  changelog-check.py drift  [repo-dir] [--allow-untagged X.Y.Z] [--grace-minutes N] [--now EPOCH]

pr:    a PR that changes anything outside docs/, .github/, test/, tests/ or
       *.md files must add or change a line under `## [Unreleased]`. A PR body
       line `Changelog: none — <reason>` (or `--`, `-`) excuses a change no user
       can see; the reason is printed.
drift: `## [Unreleased]` is the first section; its subsections are only Added,
       Changed, Fixed, Removed, Security; every Unreleased entry links a PR or
       issue (#123 or a URL); every version tag vX.Y.Z has a `## [X.Y.Z]`
       section, and every version section has its tag, except the version in
       flight on a release/ branch (--allow-untagged) and, as a warning, a section
       added less than --grace-minutes (default 30) ago: the window between a
       release PR merging and the release workflow tagging it. After that it
       fails, so a release workflow that failed turns things red.
Exit 1 with one line per problem.
"""
import os, re, subprocess, sys

SUBSECTIONS = {"Added", "Changed", "Fixed", "Removed", "Security"}
EXEMPT_DIRS = ("docs/", ".github/", "test/", "tests/")
problems = []
def bad(m): problems.append(m)

def git(*a, cwd=None):
    return subprocess.run(["git", *a], capture_output=True, text=True, cwd=cwd, check=True).stdout

def unreleased_block(text):
    m = re.search(r"^## \[Unreleased\][^\n]*\n(.*?)(?=^## \[|\Z)", text, re.M | re.S)
    return m.group(1) if m else None

def pr(base, head):
    body = os.environ.get("PR_BODY", "")
    files = [f for f in git("diff", "--name-only", f"{base}...{head}").split("\n") if f]
    code = [f for f in files if not f.startswith(EXEMPT_DIRS) and not f.endswith(".md")]
    m = re.search(r"^Changelog:\s*none\s*(?:—|--|-)\s*(\S.*)$", body.replace("\r", ""), re.M)
    if not code:
        print("changelog: only docs, CI or tests changed; no entry needed")
        return
    if m:
        print(f"changelog: excused by the PR body: {m.group(1).strip()}")
        return
    if "CHANGELOG.md" not in files:
        bad(f"this PR changes {len(code)} file(s) outside docs/CI/tests (e.g. {code[0]}) but not CHANGELOG.md; "
            "add a line under ## [Unreleased], or put 'Changelog: none — <reason>' in the PR body")
        return
    try:
        old = git("show", f"{base}:CHANGELOG.md")
    except subprocess.CalledProcessError:
        old = ""
    new = git("show", f"{head}:CHANGELOG.md")
    ou, nu = unreleased_block(old) or "", unreleased_block(new)
    if nu is None:
        bad("CHANGELOG.md has no ## [Unreleased] section")
    elif sorted(l for l in nu.split("\n") if l.strip()) == sorted(l for l in ou.split("\n") if l.strip()):
        bad("CHANGELOG.md changed, but not under ## [Unreleased]; the entry belongs there (releasing moves it)")
    else:
        print("changelog: Unreleased updated")

def drift(root, allow_untagged, grace_minutes=30, now=None):
    path = os.path.join(root, "CHANGELOG.md")
    if not os.path.exists(path):
        bad("CHANGELOG.md is missing"); return
    text = open(path, encoding="utf-8").read()
    heads = re.findall(r"^## \[([^\]]+)\]", text, re.M)
    if not heads or heads[0] != "Unreleased":
        bad("the first section must be ## [Unreleased]")
    block = unreleased_block(text) or ""
    for sub in re.findall(r"^### (.+)$", block, re.M):
        if sub.strip() not in SUBSECTIONS:
            bad(f"Unreleased has a '### {sub}' subsection; use Added, Changed, Fixed, Removed or Security")
    for line in block.split("\n"):
        if line.startswith("- ") and not re.search(r"#\d+|https?://", line):
            bad(f"Unreleased entry does not link its PR or issue: {line[:80]}")
    versions = [h for h in heads if h != "Unreleased"]
    for v in versions:
        if not re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?", v):
            bad(f"section '## [{v}]' is not a version")
    tags = [t[1:] for t in git("tag", "--list", "v[0-9]*", cwd=root).split("\n") if t]
    for t in tags:
        if t not in versions:
            bad(f"tag v{t} exists but CHANGELOG.md has no ## [{t}] section")
    import time
    now = int(now if now is not None else time.time())
    # A PR can excuse one untagged version, visibly, in its body: the fix for a
    # broken release workflow cannot itself wait for the release it repairs.
    ex = re.search(r"^Changelog-untagged:\s*(\d+\.\d+\.\d+)\s*(?:—|--|-)\s*(\S.*)$",
                   os.environ.get("PR_BODY", "").replace("\r", ""), re.M)
    excused = ex.group(1) if ex else None
    if ex:
        print(f"changelog: v{excused} excused from needing its tag by the PR body: {ex.group(2).strip()}")
    for v in versions:
        if v in tags or v == allow_untagged or v == excused:
            continue
        # Between a release PR merging and the release workflow creating the
        # tag there is a short gap. Allow it, but only for a bounded time: a
        # release workflow that failed must still turn this red.
        added = git("log", "--format=%ct", "-S", f"## [{v}]", "--", "CHANGELOG.md", cwd=root).split()
        age_min = (now - int(added[-1])) / 60 if added else None
        if age_min is not None and age_min < grace_minutes:
            print(f"::warning::CHANGELOG.md has ## [{v}] but no tag v{v} yet; its release commit is {age_min:.0f} minutes old, "
                  f"so the release workflow has until {grace_minutes} minutes to tag it")
            continue
        when = f"; its section was added {age_min:.0f} minutes ago" if age_min is not None else ""
        bad(f"CHANGELOG.md has ## [{v}] but there is no tag v{v}{when}. The release workflow should have created it: "
            f"re-run it (workflow_dispatch with version {v}) or look at why it failed")

def main():
    a = sys.argv[1:]
    if a[:1] == ["pr"] and len(a) == 3:
        pr(a[1], a[2])
    elif a[:1] == ["drift"]:
        allow = None
        rest = a[1:]
        if "--allow-untagged" in rest:
            i = rest.index("--allow-untagged"); allow = rest[i + 1]; del rest[i:i + 2]
        grace, now = 30, None
        for flag in ("--grace-minutes", "--now"):
            if flag in rest:
                i = rest.index(flag); val = int(rest[i + 1]); del rest[i:i + 2]
                if flag == "--now": now = val
                else: grace = val
        drift(rest[0] if rest else ".", allow, grace, now)
    else:
        sys.exit(__doc__)
    if problems:
        for p in problems: print(f"changelog: {p}", file=sys.stderr)
        sys.exit(1)
    if a[0] == "drift": print("changelog: in step with the tags")

main()
