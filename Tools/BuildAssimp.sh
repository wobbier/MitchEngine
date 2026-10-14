#!/usr/bin/env bash
# Build the static model importer with optimization, including when the engine is in Debug.
set -euo pipefail
engine_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${ASSIMP_BUILD_DIR:-$engine_dir/.tmp/assimp-release}"
output_dir="$engine_dir/ThirdParty/Lib/Assimp/linux/Release"

cmake -S "$engine_dir/ThirdParty/Assimp" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$build_dir/lib" \
    -DASSIMP_ARCHIVE_OUTPUT_DIRECTORY="$build_dir/lib" \
    -DBUILD_SHARED_LIBS=OFF \
    -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF \
    -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF \
    -DASSIMP_BUILD_FBX_IMPORTER=ON \
    -DASSIMP_BUILD_OBJ_IMPORTER=ON \
    -DASSIMP_BUILD_ASSBIN_IMPORTER=ON \
    -DASSIMP_BUILD_ASSBIN_EXPORTER=ON \
    -DASSIMP_BUILD_TESTS=OFF \
    -DASSIMP_BUILD_ASSIMP_TOOLS=OFF \
    -DASSIMP_BUILD_SAMPLES=OFF \
    -DASSIMP_BUILD_ZLIB=ON
cmake --build "$build_dir" --parallel "${ASSIMP_BUILD_JOBS:-8}"
mkdir -p "$output_dir"
install -m 644 "$build_dir/lib/libassimp.a" "$output_dir/libassimp.a"
install -m 644 "$build_dir/lib/libzlibstatic.a" "$output_dir/libzlibstatic.a"
