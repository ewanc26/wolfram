#!/usr/bin/env bash
# apt-install.sh -- apt-get install with per-package skip and retries.
#
# The GitHub ubuntu-24.04 runner image preinstalls the toolchain (gcc, g++,
# make, cmake, ninja, pkg-config, clang-18, clang-format-18, libssl-dev,
# libsqlite3-dev, ...), so only genuinely missing packages should hit the
# network: runner DNS to the Ubuntu archives is intermittently broken, and
# fewer apt fetches means fewer chances to hit it. Packages already
# installed are skipped; the rest are installed with retries.
#
# Run as root:  sudo bash tools/apt-install.sh pkg1 pkg2 ...
set -uo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "apt-install: run as root (sudo bash $0 ...)" >&2
  exit 2
fi

[ "$#" -gt 0 ] || { echo "apt-install: no packages given" >&2; exit 2; }

# GitHub runner images point apt at the Azure mirror via
# /etc/apt/apt-mirrors.txt (mirror+file: URIs). When that host does not
# resolve from the runner (seen 2026-11-11), repoint apt at a mirror that
# does, so the remaining packages can still be fetched.
mirror_host() { echo "$1" | sed -E 's#^[a-z]+://([^/]+).*#\1#'; }
fix_mirror() {
  local def defhost m h
  def=$(cat /etc/apt/apt-mirrors.txt 2>/dev/null || true)
  defhost=$(mirror_host "$def")
  if [ -n "$defhost" ] && getent hosts "$defhost" >/dev/null 2>&1; then
    return 0
  fi
  echo "apt-install: default apt mirror '$def' does not resolve; trying fallbacks" >&2
  for m in http://archive.ubuntu.com/ubuntu http://us.archive.ubuntu.com/ubuntu \
           http://mirrors.edge.kernel.org/ubuntu https://ftp.acc.umu.se/mirror/ubuntu; do
    h=$(mirror_host "$m")
    if getent hosts "$h" >/dev/null 2>&1; then
      echo "$m" > /etc/apt/apt-mirrors.txt
      echo "apt-install: repointed apt at $m" >&2
      return 0
    fi
  done
  echo "apt-install: no fallback mirror resolved; keeping default" >&2
  return 1
}

missing=()
for p in "$@"; do
  # A package is satisfied when dpkg has it or when it provides a binary of
  # the same name (covers tool metapackages like clang/gcc/cmake that the
  # runner image ships under versioned names, e.g. clang-18).
  if dpkg-query -W -f='${db:Status-Status}' "$p" 2>/dev/null | grep -qx installed ||
     command -v "$p" >/dev/null 2>&1; then
    echo "apt-install: $p already present, skipping"
  else
    missing+=("$p")
  fi
done
if [ "${#missing[@]}" -eq 0 ]; then
  exit 0
fi

fix_mirror || true
ok=0
for attempt in 1 2 3 4 5; do
  fix_mirror || true
  if timeout 300 apt-get update &&
     timeout 900 apt-get install -y --no-install-recommends "${missing[@]}"; then
    ok=1
    break
  fi
  echo "apt-install: attempt $attempt failed; retrying" >&2
  sleep 15
done
if [ "$ok" -ne 1 ]; then
  echo "apt-install: could not install: ${missing[*]}" >&2
  exit 1
fi
