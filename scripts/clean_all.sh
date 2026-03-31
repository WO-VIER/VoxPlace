#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Remove build directories
rm -rf "$PROJECT_ROOT/build" "$PROJECT_ROOT/build_asan" "$PROJECT_ROOT/build_chunky" \
       "$PROJECT_ROOT/build_release" "$PROJECT_ROOT/build_relwithdebinfo" \
       "$PROJECT_ROOT/build_minsizerel"

# Remove CMake artifacts if present at repo root
rm -rf "$PROJECT_ROOT/CMakeFiles" "$PROJECT_ROOT/CMakeCache.txt" "$PROJECT_ROOT/compile_commands.json"

echo "Full clean completed."
