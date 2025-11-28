#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$PROJECT_ROOT/build_wasm"
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory $BUILD_DIR doesn't exist. Run ./scripts/build_wasm.sh first." >&2
    exit 1
fi

PORT=${PORT:-8000}
python -m http.server --directory "$BUILD_DIR" "$PORT"
