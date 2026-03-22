#!/usr/bin/env bash
set -euo pipefail
JOBS=${JOBS:-8}
BUILD_TYPE=${BUILD_TYPE:-Debug}

# Resolve project root regardless of current working directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_TYPE_LOWER="$(printf '%s' "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
case "$BUILD_TYPE" in
    Debug) DEFAULT_BUILD_DIR="$PROJECT_ROOT/build" ;;
    Release) DEFAULT_BUILD_DIR="$PROJECT_ROOT/build_release" ;;
    RelWithDebInfo) DEFAULT_BUILD_DIR="$PROJECT_ROOT/build_relwithdebinfo" ;;
    MinSizeRel) DEFAULT_BUILD_DIR="$PROJECT_ROOT/build_minsizerel" ;;
    *) DEFAULT_BUILD_DIR="$PROJECT_ROOT/build_$BUILD_TYPE_LOWER" ;;
esac
BUILD_DIR=${BUILD_DIR:-"$DEFAULT_BUILD_DIR"}

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$JOBS"
