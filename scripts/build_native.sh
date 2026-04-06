#!/usr/bin/env bash
set -euo pipefail
JOBS=${JOBS:-8}
BUILD_TYPE=${BUILD_TYPE:-Debug}

# Resolve project root regardless of current working directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

case "$BUILD_TYPE" in
	Release)    BUILD_DIR="$PROJECT_ROOT/build_release" ;;
	Debug)      BUILD_DIR="$PROJECT_ROOT/build_debug" ;;
	*)          BUILD_DIR="$PROJECT_ROOT/build_${BUILD_TYPE,,}" ;;
esac

BUILD_DIR=${BUILD_DIR:-"$BUILD_DIR"}
CMAKE_ARGS=${CMAKE_ARGS:-""}

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" $CMAKE_ARGS
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$JOBS"
