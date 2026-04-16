#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-speak-to-computer"

echo "Cleaning ${BUILD_DIR}"
rm -rf -- "${BUILD_DIR}"

echo "Configuring speak-to-computer"
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"

echo "Building speak-to-computer"
cmake --build "${BUILD_DIR}"

echo "Running tests"
ctest --test-dir "${BUILD_DIR}" --output-on-failure
