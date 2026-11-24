#!/usr/bin/env bash
# Local "CI": format check, then the ci preset (asan-ubsan + clang-tidy + -Werror), then tests, then coverage.
# The replays are not part of it: ./scripts/goldens.sh plays them against their goldens.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== clang-format"
# tools/dosbox/ is compiled inside DOSBox-X and follows its style (tabs)
git ls-files -co --exclude-standard -- '*.cpp' '*.hpp' ':!tools/dosbox/*' | xargs -r clang-format --dry-run --Werror

echo "== constants defined once"
./scripts/duplicate_constants.py

echo "== configure + build (ci)"
cmake --preset ci
cmake --build --preset ci

echo "== tests (ci)"
# The replays are not part of this (./scripts/goldens.sh plays them against their goldens):
ctest --preset ci --parallel "$(nproc)"

echo "== coverage (at least 80% of the lines in src/)"
./scripts/coverage.sh
