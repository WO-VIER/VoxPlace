#!/usr/bin/env bash
set -euo pipefail
JOBS=${JOBS:-8}

# Resolve project root regardless of current working directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/build_asan" -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build "$PROJECT_ROOT/build_asan" --config Debug -j "$JOBS"
