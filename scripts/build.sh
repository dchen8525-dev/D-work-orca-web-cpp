#!/usr/bin/env bash
# Build orca-cpp on Linux (also used by CI). No vcpkg: distro packages.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
VERSION="${ORCA_VERSION:-0.0.1-dev}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DORCA_BUILD_VERSION="$VERSION" \
    -DORCA_REQUIRE_NETWORK_DEPS=ON

cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure

CMAKE_BIN="$(command -v cmake)"
"$CMAKE_BIN" -DORCA_TARGET_FILE="$BUILD/server/orca-server" \
    -DORCA_MAX_BYTES=8388608 -P "$ROOT/cmake/SizeReport.cmake"
"$CMAKE_BIN" -DORCA_TARGET_FILE="$BUILD/gateway/orca-gateway" \
    -DORCA_MAX_BYTES=8388608 -P "$ROOT/cmake/SizeReport.cmake"
