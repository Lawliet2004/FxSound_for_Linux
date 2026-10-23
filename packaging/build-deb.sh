#!/usr/bin/env bash
# Build and test before producing an installable native package.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_dir/build-release}"
output_dir="${2:-$repo_dir/dist}"
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DFXSOUND_STUB_DSP=OFF -DFXSOUND_STUB_AUDIO=OFF -DFXSOUND_SANITIZER=none
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$build_dir" -L unit --output-on-failure
mkdir -p -- "$output_dir"
cpack --config "$build_dir/CPackConfig.cmake" -G DEB -B "$output_dir"
