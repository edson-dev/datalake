#!/usr/bin/env bash
# Runs inside a debian:bookworm-slim container to produce the linux_amd64 build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/linux}"
JOBS="${JOBS:-$(nproc)}"

export DEBIAN_FRONTEND=noninteractive
if ! command -v cmake >/dev/null 2>&1; then
  apt-get update
  apt-get install -y --no-install-recommends ca-certificates git cmake ninja-build g++
  rm -rf /var/lib/apt/lists/*
fi

# the workspace is bind-mounted from the host, so git refuses it as "dubious"
git config --global --add safe.directory '*' >/dev/null 2>&1 || true

cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DEXTENSION_STATIC_BUILD=1 \
  -DBUILD_UNITTESTS=FALSE \
  -DDUCKDB_EXTENSION_CONFIGS="$ROOT/extension_config.cmake" \
  -DUNITTEST_ROOT_DIRECTORY="$ROOT/" \
  -DENABLE_UNITTEST_CPP_TESTS=FALSE \
  -DENABLE_EXTENSION_AUTOLOADING=1 \
  -DENABLE_EXTENSION_AUTOINSTALL=1 \
  -S "$ROOT/duckdb" \
  -B "$BUILD_DIR"

cmake --build "$BUILD_DIR" -j "$JOBS"

echo
echo "Built: $BUILD_DIR/extension/datalake/datalake.duckdb_extension"
