#!/usr/bin/env bash
#
# style-check.sh -- check a repository's README header and logo against
# docs/house-style.md.
#
# Usage: tools/style-check.sh [repo-dir]
# Exit 0 when it conforms, 1 with one line per violation otherwise.
set -u
repo="${1:-.}"
exec python3 - "$repo" <<'PY'
import re, sys, os

repo = sys.argv[1]
bad = []
def fail(msg): bad.append(msg)

readme_path = os.path.join(repo, "README.md")
logo_path = os.path.join(repo, "docs", "logo.svg")

# ---- README -------------------------------------------------------------
try:
    readme = open(readme_path, encoding="utf-8").read()
except OSError:
    fail("README.md is missing"); readme = ""
lines = readme.split("\n")

m = re.match(r'\s*<p align="center">\s*<img src="docs/logo\.svg" alt="([^"]+)" width="420">\s*</p>', readme)
if not m:
    fail('README must open with <p align="center"><img src="docs/logo.svg" alt="<Name>" width="420"></p>')
    name = None
else:
    name = m.group(1)

# Badge row: the second centred block.
blocks = re.findall(r'<p align="center">(.*?)</p>', readme, re.S)
badge_row = blocks[1] if len(blocks) > 1 else ""
if not badge_row:
    fail("README needs a centred badge row after the logo")
else:
    want = [
        ("CI workflow badge", r'actions/workflows/ci\.yml/badge\.svg'),
        ("latest release badge with ?sort=semver", r'img\.shields\.io/github/v/release/[^"?]+\?sort=semver'),
        ("licence badge", r'img\.shields\.io/github/license/'),
        ("GitHub Sponsors badge", r'img\.shields\.io/github/sponsors/'),
    ]
    last = -1
    for label, pat in want:
        mm = re.search(pat, badge_row)
        if not mm:
            fail(f"badge row is missing the {label}")
        elif mm.start() < last:
            fail(f"badge row has the {label} out of order (CI, release, licence, sponsors, then extras)")
        else:
            last = mm.start()
    if re.search(r'img\.shields\.io/github/license/[^"]*"', badge_row) and "label=licence" not in badge_row:
        fail("licence badge must carry ?label=licence (British spelling)")

# Heading follows the badge row.
h1 = re.search(r'^# (.+)$', readme, re.M)
if not h1:
    fail("README needs a '# <Name>' heading")
elif name and h1.group(1).strip() != name:
    fail(f"heading '# {h1.group(1).strip()}' must equal the logo alt text '{name}'")
elif h1 and blocks:
    if readme.find(blocks[1] if len(blocks) > 1 else "") > h1.start():
        fail("the badge row must come before the '# <Name>' heading")

if re.search(r'!\[version\]', readme, re.I):
    fail("remove the duplicate ![version] badge; the header release badge is the version badge")

# Section order among recognised headings.
cats = [
    ("install", r'install|getting started|requirements'),
    ("use", r'quick start|usage|^use\b|examples'),
    ("build", r'build'),
    ("contributing", r'contribut'),
    ("licence", r'licen[cs]e'),
]
found = {}
for i, ln in enumerate(lines):
    mm = re.match(r'^## (.+)$', ln)
    if not mm: continue
    t = mm.group(1).lower()
    for key, pat in cats:
        if key not in found and re.search(pat, t):
            found[key] = i
            break
for key in ("contributing", "licence"):
    if key not in found:
        fail(f"README is missing a {key} section")
order = [k for k, _ in cats if k in found]
for a, b in zip(order, order[1:]):
    if found[a] > found[b]:
        fail(f"section '{a}' must come before '{b}'")
if "contributing" in found and "CONTRIBUTING.md" not in readme:
    fail("the contributing section must link CONTRIBUTING.md")

# ---- logo ---------------------------------------------------------------
try:
    svg = open(logo_path, encoding="utf-8").read()
except OSError:
    fail("docs/logo.svg is missing"); svg = ""
if svg:
    root = re.search(r'<svg\b[^>]*>', svg)
    root = root.group(0) if root else ""
    if not re.search(r'viewBox="0 0 294 \d+"', root): fail('logo viewBox must be "0 0 294 <height>"')
    if 'role="img"' not in root: fail('logo root needs role="img"')
    if not re.search(r'aria-label="[^"]+"', root): fail("logo root needs an aria-label")
    if 'shape-rendering="crispEdges"' not in svg: fail('logo needs shape-rendering="crispEdges"')
    if 'class="logo"' not in svg: fail('logo shapes need class="logo"')
    if "#15803d" not in svg.lower(): fail("logo light colour must be #15803d")
    if "#4ade80" not in svg.lower(): fail("logo dark colour must be #4ade80")
    if "prefers-color-scheme: dark" not in svg: fail("logo needs a prefers-color-scheme: dark rule")
    body = re.sub(r'<style>.*?</style>', '', svg, flags=re.S)
    tags = set(re.findall(r'<([a-zA-Z]+)\b', body))
    extra = tags - {"svg", "g", "rect", "title", "desc"}
    if extra: fail("logo may contain only <rect> shapes, found: " + ", ".join(sorted(extra)))
    if re.findall(r'\bfill="', body): fail("logo must not set fill attributes; the .logo class owns the colour")
    off = 0
    for r in re.findall(r'<rect\b[^>]*>', body):
        vals = {k: re.search(k + r'="(-?\d+)"', r) for k in ("x", "y", "width", "height")}
        if not all(vals.values()):
            off += 1; continue
        v = {k: int(m_.group(1)) for k, m_ in vals.items()}
        if v["x"] % 3 or v["width"] % 3 or v["y"] % 5 or v["height"] % 5:
            off += 1
    if off: fail(f"{off} <rect> element(s) are off the 3x5 cell grid")

if bad:
    for b in bad: print("style: " + b, file=sys.stderr)
    sys.exit(1)
print("style: conforms to docs/house-style.md")
PY
