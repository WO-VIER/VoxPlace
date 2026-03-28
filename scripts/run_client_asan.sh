#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build_asan}"
SUPPRESSIONS_FILE="${LSAN_SUPPRESSIONS_FILE:-$PROJECT_ROOT/scripts/lsan_suppressions.txt}"

DEFAULT_ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1"
DEFAULT_LSAN_OPTIONS="suppressions=${SUPPRESSIONS_FILE}:print_suppressions=0"

export ASAN_OPTIONS="${ASAN_OPTIONS:-$DEFAULT_ASAN_OPTIONS}"
export LSAN_OPTIONS="${LSAN_OPTIONS:-$DEFAULT_LSAN_OPTIONS}"

exec "${BUILD_DIR}/VoxPlace" "$@"
