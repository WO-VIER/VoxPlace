#!/usr/bin/env bash
set -euo pipefail
JOBS=${JOBS:-8}
BUILD_TYPE=${BUILD_TYPE:-Debug}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR=${BUILD_DIR:-"$PROJECT_ROOT/build_asan"}

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DENABLE_ASAN=ON
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$JOBS"

echo "ASAN build ready in: $BUILD_DIR"
echo "Client: $BUILD_DIR/VoxPlace"
echo "Server: $BUILD_DIR/VoxPlaceServer"
