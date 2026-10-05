#!/usr/bin/env python3
"""repo_sync.py -- labels and repository metadata as code, for all five repos.

  repo_sync.py labels   check|apply [--repo O/R] [--file F] [--canon F] [--live-json F] [--dry-run]
  repo_sync.py metadata check|apply [--repo O/R] [--file F] [--canon F] [--live-json F] [--dry-run]

`check` exits 1 on any difference and prints one line each. It is two checks:
  file   the repo's `core` block must equal the canonical (Wolfram's) one, and
         `local`/`repo` must follow the rules in the file header;
  live   what GitHub reports must equal the file.
`--live-json` supplies the live data from a file (labels: the array from
GET /labels; metadata: the object from GET /repos/{o}/{r}) so the checks can be
tested offline. `apply` writes with `gh api` and is run by a person.

Label descriptions are at most 100 characters (a GitHub limit). Renames PATCH
`new_name`, which keeps the label on existing issues.
"""
import argparse, json, subprocess, sys
import yaml

FAMILY_PREFIXES = ("area: ", "platform: ")
problems = []
def bad(msg): problems.append(msg)

def gh(args, data=None):
    cmd = ["gh", "api"] + args
    r = subprocess.run(cmd, input=data, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"gh api {' '.join(args)} failed: {r.stderr.strip() or r.stdout.strip()}")
    return r.stdout

def load(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)

# ---- labels -------------------------------------------------------------
def labels_file_check(doc, canon):
    core = doc.get("core") or []
    if core != canon.get("core"):
        bad("labels.yml `core` differs from Wolfram's canonical copy; copy it over, do not edit it")
    if doc.get("renames") != canon.get("renames"):
        bad("labels.yml `renames` differs from Wolfram's canonical copy")
    names = [l["name"] for l in core] + [l["name"] for l in (doc.get("local") or [])]
    if len(set(n.lower() for n in names)) != len(names):
        bad("labels.yml defines a label name twice")
    for l in (doc.get("local") or []):
        if not l["name"].startswith(FAMILY_PREFIXES):
            bad(f"local label '{l['name']}' must be 'area: <x>' or 'platform: <x>'; other labels go through Wolfram's core")
    for l in core + (doc.get("local") or []):
        if len(l.get("description") or "") == 0:
            bad(f"label '{l['name']}' needs a description")
        if len(l.get("description") or "") > 100:
            bad(f"label '{l['name']}' description is over 100 characters")
        c = str(l.get("color", ""))
        if len(c) != 6 or any(ch not in "0123456789abcdefABCDEF" for ch in c):
            bad(f"label '{l['name']}' colour must be six hex digits")
    fam = {}
    for l in core + (doc.get("local") or []):
        key = l["name"].split(": ")[0] if ": " in l["name"] else None
        if key in ("area", "impact", "platform"):
            fam.setdefault(key, set()).add(str(l["color"]).lower())
    for k, v in fam.items():
        if len(v) > 1:
            bad(f"the '{k}:' family must use one colour, found {sorted(v)}")

def desired_labels(doc):
    return list(doc.get("core") or []) + list(doc.get("local") or [])

def labels_live_check(doc, live):
    want = {l["name"].lower(): l for l in desired_labels(doc)}
    have = {l["name"].lower(): l for l in live}
    for n, l in want.items():
        if n not in have:
            bad(f"label '{l['name']}' is missing from the repository")
            continue
        h = have[n]
        if h["name"] != l["name"]:
            bad(f"label '{h['name']}' is spelt differently from '{l['name']}'")
        if str(h.get("color", "")).lower() != str(l["color"]).lower():
            bad(f"label '{l['name']}' has colour {h.get('color')}, want {l['color']}")
        if (h.get("description") or "") != l["description"]:
            bad(f"label '{l['name']}' has a different description")
    for n, h in have.items():
        if n not in want:
            old = (doc.get("renames") or {})
            hint = f" (rename it: `apply` renames to '{old[h['name']]}')" if h["name"] in old else ""
            bad(f"label '{h['name']}' is not defined in labels.yml{hint}")

def labels_apply(doc, repo, live, dry):
    have = {l["name"].lower(): l for l in live}
    renames = {k.lower(): v for k, v in (doc.get("renames") or {}).items()}
    inv = {v.lower(): k for k, v in renames.items()}
    for l in desired_labels(doc):
        n = l["name"].lower()
        fields = ["-f", f"color={l['color']}", "-f", f"description={l['description']}"]
        if n in have:
            h = have[n]
            if h["name"] == l["name"] and str(h["color"]).lower() == str(l["color"]).lower() and (h.get("description") or "") == l["description"]:
                continue
            print(f"update  {l['name']}")
            if not dry: gh(["-X", "PATCH", f"repos/{repo}/labels/{urlq(h['name'])}", "-f", f"new_name={l['name']}"] + fields)
        elif n in inv and inv[n].lower() in have:
            old = have[inv[n].lower()]["name"]
            print(f"rename  {old} -> {l['name']}")
            if not dry: gh(["-X", "PATCH", f"repos/{repo}/labels/{urlq(old)}", "-f", f"new_name={l['name']}"] + fields)
        else:
            print(f"create  {l['name']}")
            if not dry: gh(["-X", "POST", f"repos/{repo}/labels", "-f", f"name={l['name']}"] + fields)

def urlq(s):
    from urllib.parse import quote
    return quote(s, safe="")

# ---- metadata -----------------------------------------------------------
def meta_effective(doc):
    core, repo = doc.get("core") or {}, doc.get("repo") or {}
    topics = sorted(set(core.get("topics") or []) | set(repo.get("topics") or []))
    return {
        "description": repo.get("description", ""),
        "homepage": repo.get("homepage") or "",
        "topics": topics,
        "has_wiki": core.get("has_wiki"),
        "has_discussions": core.get("has_discussions"),
        "has_projects": core.get("has_projects"),
    }

def meta_file_check(doc, canon):
    if doc.get("core") != canon.get("core"):
        bad("repo-metadata.yml `core` differs from Wolfram's canonical copy; copy it over, do not edit it")
    repo = doc.get("repo") or {}
    d = repo.get("description") or ""
    if not d or "\n" in d or len(d) > 350:
        bad("description must be one sentence on one line, at most 350 characters")
    elif not d.endswith("."):
        bad("description is a sentence and ends with a full stop")
    if "emoji" in d.lower():
        pass
    topics = meta_effective(doc)["topics"]
    if len(topics) > 20:
        bad(f"{len(topics)} topics; GitHub allows 20")
    for t in topics:
        import re
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,49}", t):
            bad(f"topic '{t}' must be lowercase letters, digits and hyphens, at most 50 characters")
    extra = set(repo) - {"description", "homepage", "topics", "enforce_live"}
    if extra:
        bad(f"unknown keys under `repo`: {sorted(extra)}")

def meta_live_check(doc, live):
    want = meta_effective(doc)
    have = {
        "description": live.get("description") or "",
        "homepage": live.get("homepage") or "",
        "topics": sorted(live.get("topics") or []),
        "has_wiki": live.get("has_wiki"),
        "has_discussions": live.get("has_discussions"),
        "has_projects": live.get("has_projects"),
    }
    for k in want:
        if want[k] != have[k]:
            bad(f"live {k} is {have[k]!r}, repo-metadata.yml says {want[k]!r}")

def meta_apply(doc, repo, dry):
    w = meta_effective(doc)
    print(f"PATCH repos/{repo}: description, homepage, has_wiki/has_discussions/has_projects")
    print(f"PUT   repos/{repo}/topics: {' '.join(w['topics'])}")
    if dry: return
    body = {k: w[k] for k in ("description", "homepage", "has_wiki", "has_discussions", "has_projects")}
    gh(["-X", "PATCH", f"repos/{repo}", "--input", "-"], data=json.dumps(body))
    gh(["-X", "PUT", f"repos/{repo}/topics", "--input", "-"], data=json.dumps({"names": w["topics"]}))

# ---- main ---------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["labels", "metadata"])
    ap.add_argument("mode", choices=["check", "apply"])
    ap.add_argument("--repo")
    ap.add_argument("--file")
    ap.add_argument("--canon")
    ap.add_argument("--live-json")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    default = ".github/labels.yml" if a.what == "labels" else ".github/repo-metadata.yml"
    doc = load(a.file or default)
    canon = load(a.canon or a.file or default)
    repo = a.repo
    if a.mode == "check":
        (labels_file_check if a.what == "labels" else meta_file_check)(doc, canon)
        if a.live_json or repo:
            before = len(problems)
            if a.live_json:
                live = json.load(open(a.live_json))
            elif a.what == "labels":
                live = json.loads(gh(["--paginate", f"repos/{repo}/labels?per_page=100"]).replace("][", ","))
            else:
                live = json.loads(gh([f"repos/{repo}"]))
            (labels_live_check if a.what == "labels" else meta_live_check)(doc, live)
            live_problems = problems[before:]
            enforce = a.what == "labels" or (doc.get("repo") or {}).get("enforce_live")
            if live_problems and not enforce:
                del problems[before:]
                for p in live_problems:
                    print(f"::warning::repo-sync: {p} (live metadata is not enforced yet: set enforce_live once the owner has applied it)", file=sys.stderr)
        if problems:
            for p in problems: print(f"repo-sync: {p}", file=sys.stderr)
            sys.exit(1)
        print(f"repo-sync: {a.what} ok")
        return
    if not repo:
        sys.exit("apply needs --repo OWNER/REPO")
    if a.what == "labels":
        live = json.loads(gh(["--paginate", f"repos/{repo}/labels?per_page=100"]).replace("][", ","))
        labels_apply(doc, repo, live, a.dry_run)
    else:
        meta_apply(doc, repo, a.dry_run)

main()
