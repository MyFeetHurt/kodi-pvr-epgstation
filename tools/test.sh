#!/usr/bin/env bash
# Unit tests of the pure EPGStation layer (no Kodi needed).
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S tests -B build/tests -G Ninja >/dev/null
cmake --build build/tests
./build/tests/unit_tests
