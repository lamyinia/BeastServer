#!/usr/bin/env bash
# 恢复 Conan 依赖与 CMake preset（CLion / 命令行共用）。
set -euo pipefail

cd "$(dirname "$0")"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
OUTPUT="${OUTPUT:-build}"

echo "==> conan install (build_type=${BUILD_TYPE}, output=${OUTPUT})"
conan install . --output-folder="${OUTPUT}" --build=missing -s "build_type=${BUILD_TYPE}"

echo "==> cmake --preset conan-relwithdebinfo"
cmake --preset conan-relwithdebinfo

echo "Done. CLion: Reload CMake Project，选择 profile conan-relwithdebinfo。"
