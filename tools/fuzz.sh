#!/usr/bin/env bash
#
# fuzz.sh -- run every libFuzzer harness in test/fuzz/ for a bounded time.
#
#   tools/fuzz.sh BUILD_DIR [SECONDS] [HARNESS...]
#
# BUILD_DIR is a tree configured with -DWOLFRAM_BUILD_FUZZ=ON and Clang (see
# docs/fuzzing.md). Each harness starts from a scratch copy of its committed
# corpus, so a run never writes into test/fuzz/corpus/. A harness that stops
# with a finding has its input replayed three more times; the script fails
# only when that replay fails again, so a crash it reports is reproducible.
# Reproducers are left in BUILD_DIR/fuzz-findings/.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:?usage: tools/fuzz.sh BUILD_DIR [SECONDS] [HARNESS...]}"
seconds="${2:-60}"
shift $(($# >= 2 ? 2 : 1))
harnesses=("$@")
if [ ${#harnesses[@]} -eq 0 ]; then
    for c in "$here"/test/fuzz/fuzz_*.c; do
        harnesses+=("$(basename "$c" .c)")
    done
fi

findings="$build/fuzz-findings"
mkdir -p "$findings"
status=0
for h in "${harnesses[@]}"; do
    bin="$build/test/fuzz/$h"
    if [ ! -x "$bin" ]; then
        echo "::error::$h: $bin is not built"
        status=1
        continue
    fi
    work="$(mktemp -d)"
    cp "$here/test/fuzz/corpus/$h"/* "$work/" 2>/dev/null
    echo "== $h: ${seconds}s"
    "$bin" -max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 \
        -max_len=65536 -print_final_stats=1 \
        -artifact_prefix="$findings/$h-" "$work" >"$findings/$h.log" 2>&1
    rc=$?
    tail -n 12 "$findings/$h.log" | grep -E '^(stat::|#[0-9]+)' | tail -n 3
    if [ $rc -ne 0 ]; then
        for art in "$findings/$h-"*; do
            [ -f "$art" ] || continue
            case "$art" in *.log) continue ;; esac
            reproduced=0
            for _ in 1 2 3; do
                "$bin" -runs=1 -timeout=10 -rss_limit_mb=2048 "$art" \
                    >/dev/null 2>&1 || reproduced=$((reproduced + 1))
            done
            if [ $reproduced -gt 0 ]; then
                echo "::error::$h: reproducible finding $(basename "$art")"
                grep -m1 -E 'runtime error|ERROR: ' "$findings/$h.log"
                status=1
            else
                echo "::warning::$h: $(basename "$art") did not reproduce"
            fi
        done
    fi
    rm -r "$work"
done
exit $status
