#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Remove build directories
rm -rf "$PROJECT_ROOT/build" "$PROJECT_ROOT/build_wasm"

# Remove generated output files that may be at repo root
rm -f "$PROJECT_ROOT/VoxPlace" "$PROJECT_ROOT/VoxPlace.html" "$PROJECT_ROOT/VoxPlace.wasm" "$PROJECT_ROOT/VoxPlace.js"

# Remove CMake artifacts if present
rm -rf "$PROJECT_ROOT/CMakeFiles" "$PROJECT_ROOT/CMakeCache.txt" "$PROJECT_ROOT/compile_commands.json"

# Remove any temporary emscripten generated files
rm -f "$PROJECT_ROOT"/*.mem
rm -f "$PROJECT_ROOT"/*.js
rm -f "$PROJECT_ROOT"/*.wasm

echo "Full clean completed (build/ build_wasm/ artifacts removed)."
