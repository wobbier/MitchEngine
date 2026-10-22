#!/usr/bin/env bash
# Generates the third-party build files needed to compile the engine on Linux.
#
# The engine links the prebuilt static libraries committed under ThirdParty/Lib
# (BGFX, Bullet, Assimp — see Tools/BaseProject.sharpmake.cs ConfigureLinux), so
# a normal project setup only needs the cmake configure step below to produce
# the generated headers (assimp/config.h and friends) in the submodule trees.
#
# Pass --build-bgfx to also rebuild the bgfx toolchain and libraries from
# source; use that when refreshing the archives in ThirdParty/Lib/BGFX/linux
# after a bgfx submodule bump (copy the .a files out of bgfx/.build/linux64_gcc/bin).
set -e
cd "$(dirname "$0")"

# CMAKE_POLICY_VERSION_MINIMUM: Bullet's cmake_minimum_required(2.4.3) predates
# what cmake 4.x accepts; the flag is ignored (with a warning) on older cmake.
(
    cd Assimp
    cmake . -G "Unix Makefiles" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED_LIBS=false \
        -DASSIMP_BUILD_ZLIB=true \
        -DASSIMP_BUILD_TESTS=false \
        -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=false \
        -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=false \
        -DASSIMP_BUILD_FBX_IMPORTER=true \
        -DASSIMP_BUILD_OBJ_IMPORTER=true \
        -DASSIMP_BUILD_ASSBIN_IMPORTER=true \
        -DASSIMP_BUILD_ASSBIN_EXPORTER=true
)

(
    cd Bullet
    cmake . -G "Unix Makefiles" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
)

if [ "$1" = "--build-bgfx" ]; then
    if [ ! -d genie ]; then
        git clone https://github.com/bkaradzic/genie
    fi
    make -C genie

    cd bgfx
    ../genie/bin/linux/genie \
      --with-tools \
      --gcc=linux-gcc \
      gmake

    CFLAGS="-march=x86-64-v2" CXXFLAGS="-march=x86-64-v2" \
      make linux-gcc-debug64 -j"$(nproc)"
    CFLAGS="-march=x86-64-v2" CXXFLAGS="-march=x86-64-v2" \
      make linux-gcc-release64 -j"$(nproc)"
fi
