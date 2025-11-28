#!/usr/bin/env bash
set -euo pipefail
JOBS=${JOBS:-8}

# Resolve project root regardless of current working directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# If emcmake is available use it; otherwise try to source system emscripten profile
if command -v emcmake >/dev/null 2>&1; then
    emcmake cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/build_wasm" -DCMAKE_BUILD_TYPE=Release
    emmake make -C "$PROJECT_ROOT/build_wasm" -j "$JOBS"
else
    if [ -f /etc/profile.d/emscripten.sh ]; then
        CMD="source /etc/profile.d/emscripten.sh && emcmake cmake -S \"$PROJECT_ROOT\" -B \"$PROJECT_ROOT/build_wasm\" -DCMAKE_BUILD_TYPE=Release && emmake make -C \"$PROJECT_ROOT/build_wasm\" -j $JOBS"
        bash -lc "$CMD"
    else
        echo "emcmake not found. Install emsdk or source /etc/profile.d/emscripten.sh" >&2
        exit 1
    fi
fi
