#!/usr/bin/env bash
set -euo pipefail

JOBS=${JOBS:-8}
BUILD_TYPE=${BUILD_TYPE:-Release}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR=${BUILD_DIR:-"$PROJECT_ROOT/build_chunky"}

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --target VoxPlacePregen --config "$BUILD_TYPE" -j "$JOBS"

echo "Built $BUILD_DIR/VoxPlacePregen"
