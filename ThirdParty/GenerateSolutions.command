#!/usr/bin/env bash
set -euo pipefail

cd Assimp
cmake . \
  -G "Xcode" \
  -DBUILD_SHARED_LIBS=false \
  -DASSIMP_BUILD_ZLIB=true \
  -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=false \
  -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=false \
  -DASSIMP_BUILD_FBX_IMPORTER=true \
  -DASSIMP_BUILD_OBJ_IMPORTER=true \
  -DASSIMP_BUILD_ASSBIN_IMPORTER=true \
  -DASSIMP_BUILD_ASSBIN_EXPORTER=true

cd ../Bullet
cmake . \
  -G "Xcode" \
  -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cd ..