#!/usr/bin/env bash
# Line coverage of our own code, from clang's source-based coverage (the `coverage` preset instruments only our
# targets). Counts src/ without the tests, the fuzzers and the programs' main.cpp files; fails below the minimum.
#   ./scripts/coverage.sh          build, run the tests, report, fail below 80%
#   ./scripts/coverage.sh 85       another minimum
#   COVERAGE_HTML=1 ./scripts/coverage.sh   also write an annotated source report to build/coverage/html/
set -euo pipefail
cd "$(dirname "$0")/.."

minimum="${1:-80}"
build=build/coverage
profiles="$build/profiles"

# llvm-profdata and llvm-cov must match the compiler's version.
major="$(clang++ --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -n 1)"
tool() { command -v "$1-$major" || command -v "$1" || { echo "coverage: $1 not found" >&2; exit 1; }; }
profdata="$(tool llvm-profdata)"
cov="$(tool llvm-cov)"

cmake --preset coverage >/dev/null
cmake --build --preset coverage

rm -rf "$profiles"
mkdir -p "$profiles"
# The tests in shards side by side, one per core: each process writes its own profile. The shards take the tests
# in the order they are declared: with Catch2's random order each process would shuffle them its own way, and the
# shards would overlap and leave tests out.
shards="${COVERAGE_SHARDS:-$(nproc)}"
pids=()
for ((shard = 0; shard < shards; ++shard)); do
    LLVM_PROFILE_FILE="$PWD/$profiles/shard-$shard.profraw" "$build/bin/lvr_tests" \
        --order decl --shard-count "$shards" --shard-index "$shard" >/dev/null &
    pids+=("$!")
done
for pid in "${pids[@]}"; do
    wait "$pid"
done
"$profdata" merge -sparse "$profiles"/*.profraw -o "$build/tests.profdata"

options=(-instr-profile="$build/tests.profdata" -ignore-filename-regex='/(tests|fuzz)/|/main\.cpp$')
"$cov" report "$build/bin/lvr_tests" "${options[@]}" -show-region-summary=false -show-branch-summary=false \
    "$PWD/src" | tee "$build/report.txt"
if [[ -n "${COVERAGE_HTML:-}" ]]; then
    "$cov" show "$build/bin/lvr_tests" "${options[@]}" -format=html -output-dir="$build/html" "$PWD/src"
    echo "html report: $build/html/index.html"
fi

total="$(awk '/^TOTAL/ { sub(/%/, "", $NF); print $NF }' "$build/report.txt")"
if awk -v t="$total" -v m="$minimum" 'BEGIN { exit !(t + 0 < m + 0) }'; then
    echo "coverage: ${total}% of lines, below the minimum of ${minimum}%" >&2
    exit 1
fi
echo "coverage: ${total}% of lines (minimum ${minimum}%)"
