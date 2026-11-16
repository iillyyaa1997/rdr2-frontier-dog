#!/bin/bash
# Unit tests for the pure logic (no game): host clang++ in the rdr2-msvc-build image.
set -euo pipefail
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/w -w /w rdr2-msvc-build sh -c '
set -e
srcs="$(ls src/core/kvfile.cpp src/core/fs.cpp src/core/lang.cpp 2>/dev/null) $(ls src/logic/*.cpp 2>/dev/null || true) $(ls tests/*.cpp)"
clang++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -I src -I tests $srcs -o /tmp/fdtests
/tmp/fdtests'
