#!/usr/bin/env bash
#
# macos9-c89-check.sh -- compiles the Classic Mac OS 9 transport sources that
# need only Wolfram's own headers under the C90 flags CMake applies to
# wolfram-macos9-transport (CMakeLists.txt), with both clang and gcc.
#
# The list is every source of that target except src/transport/macos9_tls.c,
# which includes the macTLS SDK headers (ostls_async.h, Mac OS toolbox headers)
# and cannot compile on a Linux runner. CMake builds it where macTLS is
# present; this check keeps the rest of the target honest on every change.
#
# Usage: tools/macos9-c89-check.sh [repo-root]   (exit 1 on any failure)
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${1:-$here}"
cd "$root" || exit 1

SOURCES=(
  src/transport/xrpc_mac9.c
  src/update/update_core.c
  src/failure.c
  src/cdn.c
  src/drag.c
)
# Matches CMakeLists.txt: C90 with extensions off, plus the three warnings
# CodeWarrior's default mode rejects most often. WOLFRAM_VERSION_STRING is
# defined the way CMake defines it; the value only affects the agent string.
FLAGS=(
  -std=c90 -pedantic-errors -Wall -Wextra -Werror
  -Wdeclaration-after-statement -Wstrict-prototypes -Wvla
  "-DWOLFRAM_VERSION_STRING=\"0.0.0-ci\""
  -Iinclude
)

status=0
for cc in clang gcc; do
  if ! command -v "$cc" >/dev/null 2>&1; then
    echo "skip: $cc not installed"
    continue
  fi
  for src in "${SOURCES[@]}"; do
    if "$cc" "${FLAGS[@]}" -c "$src" -o /dev/null; then
      echo "ok   $cc $src"
    else
      echo "FAIL $cc $src"
      status=1
    fi
  done
done
exit $status
