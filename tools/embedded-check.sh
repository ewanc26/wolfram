#!/usr/bin/env bash
#
# embedded-check.sh -- sources that console (embedded) targets must link must
# not sit inside a $<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>> block of
# CMakeLists.txt. A header that claims "builds on every target" while its
# source is excluded compiles and then fails to link in the client
# (wolfram oauth_pairing.c, found by Indigo's 3DS build).
#
# The list lives in tools/embedded-sources.txt. Exit 1 on a violation.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${1:-$here}"
exec python3 - "$root" <<'PY'
import re, sys
root = sys.argv[1]
cm = open(root + "/CMakeLists.txt").read()
want = [l.split("#")[0].strip() for l in open(root + "/tools/embedded-sources.txt")]
want = [w for w in want if w]
marker = "$<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:"
excluded = set()
i = 0
while True:
    i = cm.find(marker, i)
    if i < 0: break
    j = i + len(marker)
    depth = 1; k = j
    while k < len(cm) and depth:
        if cm.startswith("$<", k): depth += 1; k += 2; continue
        if cm[k] == ">": depth -= 1
        k += 1
    for f in re.findall(r"[A-Za-z0-9_./${}-]+\.c(?:pp)?\b", cm[j:k]):
        excluded.add(f)
    i = k
bad = 0
for w in want:
    if w not in cm:
        print(f"embedded-check: {w} is listed but not in CMakeLists.txt", file=sys.stderr); bad = 1
    elif w in excluded:
        print(f"embedded-check: {w} must build for consoles but sits in a NOT-EMBEDDED block", file=sys.stderr); bad = 1
if bad: sys.exit(1)
print("embedded-check: console sources are all outside NOT-EMBEDDED blocks")
PY
