#!/bin/bash
# Build BandGate without Moonbase auth and install to system dirs.
# Usage: ./build_and_install.sh [Release|Debug] [jobs]

set -euo pipefail

BUILD_TYPE="${1:-Release}"
JOBS="${2:-$(nproc)}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

VST3_DIR="$HOME/.vst3"
CLAP_DIR="$HOME/.clap"
STANDALONE_DIR="$HOME/.local/bin"

echo "=== BandGate Build & Install ==="
echo "Build type: ${BUILD_TYPE}"
echo "Jobs:       ${JOBS}"
echo ""

# --- Configure ---
echo "--- Configuring CMake (Moonbase disabled) ---"
cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DBANDGATE_DISABLE_MOONBASE=ON

# --- Build ---
TARGETS=(BandGate_Standalone BandGate_VST3 BandGate_CLAP)

echo ""
echo "--- Building BandGate (${#TARGETS[@]} targets) ---"
cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" -j"$JOBS"
