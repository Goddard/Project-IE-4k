#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
INSTALL_DIR="$ROOT/install"


# --- Build and install ncnn first -------------------------------------------------
NCNN_SRC_DIR="$BUILD_DIR/_deps/ncnn-src"
NCNN_BUILD_DIR="$BUILD_DIR/_deps/ncnn-build"
NCNN_INSTALL_DIR="$INSTALL_DIR/ncnn"

if [ ! -d "$NCNN_SRC_DIR" ]; then
	echo "Cloning ncnn into $NCNN_SRC_DIR"
	git clone --depth 1 --branch 20250503 https://github.com/Tencent/ncnn.git "$NCNN_SRC_DIR"
fi

# Ensure submodules are initialized (required by ncnn)
echo "Updating ncnn submodules"
git -C "$NCNN_SRC_DIR" submodule update --init --recursive

echo "Configuring ncnn (install -> $NCNN_INSTALL_DIR)"
cmake -S "$NCNN_SRC_DIR" -B "$NCNN_BUILD_DIR" -G Ninja \
	-DCMAKE_CXX_FLAGS="-Wno-unused-variable" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$NCNN_INSTALL_DIR" \
	-DNCNN_VULKAN=ON

echo "Building and installing ncnn"
cmake --build "$NCNN_BUILD_DIR" -j"$(nproc)" --target install

# --- Get and update gemrb we at least need demo data -------------------------------------------------
GEMRB_INSTALL_DIR="$INSTALL_DIR/gemrb"
if [ ! -d "$GEMRB_INSTALL_DIR" ]; then
    echo "Cloning gemrb into $GEMRB_INSTALL_DIR"
    git clone --depth 1 --branch pie4k https://github.com/Goddard/gemrb.git "$GEMRB_INSTALL_DIR"
fi

if [ -d "$GEMRB_INSTALL_DIR" ]; then
    echo "pulling gemrb"
	cd $GEMRB_INSTALL_DIR
    git pull
	cd $ROOT
fi

# --- Configure main project (Ninja + Debug preset) --------------------------------
export CMAKE_PREFIX_PATH="$NCNN_INSTALL_DIR${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
cmake --preset debug -S "$ROOT"

# Build
cmake --build --preset debug -j"$(nproc)"

# Install to CMAKE_INSTALL_PREFIX (now ${sourceDir}/install)
# TODO : will make a better installation later
cmake --install "$BUILD_DIR"