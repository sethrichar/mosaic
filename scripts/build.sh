#!/usr/bin/env bash
# Configure, build (universal arm64 + x86_64), run unit tests, and install the plugins
# into the user plugin folders (JUCE's COPY_PLUGIN_AFTER_BUILD does the copy).
#
# Usage: scripts/build.sh [Release|Debug] [extra cmake args...]
#   BUILD_DIR   override the build directory (default: build)
#   MOSAIC_JOBS override parallel jobs (default: all cores)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-Release}"
shift || true
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
JOBS="${MOSAIC_JOBS:-$(sysctl -n hw.ncpu)}"

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR=(-G Ninja)
fi

if [[ ! -f "$ROOT/libs/JUCE/CMakeLists.txt" || ! -f "$ROOT/libs/Catch2/CMakeLists.txt" ]]; then
    echo "==> Fetching submodules"
    git -C "$ROOT" submodule update --init --recursive
fi

echo "==> Configure ($CONFIG) in $BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" "${GENERATOR[@]}" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    "$@"

echo "==> Build"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$JOBS"

echo "==> Unit tests"
ctest --test-dir "$BUILD_DIR" --build-config "$CONFIG" --output-on-failure

echo "==> Installed plugins:"
ls -d "$HOME/Library/Audio/Plug-Ins/Components/Mosaic.component" \
      "$HOME/Library/Audio/Plug-Ins/VST3/Mosaic.vst3" 2>/dev/null || true
echo "==> Done."
