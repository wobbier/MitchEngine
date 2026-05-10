#!/usr/bin/env bash
cd Assimp;
cmake . -G "Unix Makefiles" -DBUILD_SHARED_LIBS=false -DASSIMP_BUILD_ZLIB=true -DASSIMP_BUILD_TESTS=false -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=false -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=false -DASSIMP_BUILD_FBX_IMPORTER=true -DASSIMP_BUILD_OBJ_IMPORTER=true -DASSIMP_BUILD_ASSBIN_IMPORTER=true -DASSIMP_BUILD_ASSBIN_EXPORTER=true
cd ../Bullet;
cmake . -G "Unix Makefiles" -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
cd ../

git clone https://github.com/bkaradzic/genie
cd genie/
make
cd ../

cd bgfx

# TODO: Add genie to repo to build it.
../genie/bin/linux/genie \
  --with-tools \
  --gcc=linux-gcc \
  gmake

CFLAGS="-march=x86-64-v2" CXXFLAGS="-march=x86-64-v2" \
  make linux-gcc-debug64 -j$(nproc)