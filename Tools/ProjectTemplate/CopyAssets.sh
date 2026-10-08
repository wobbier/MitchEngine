#!/usr/bin/env bash
# Copies the engine + project assets into a build folder (the Linux counterpart
# of CopyAssets.bat).
#
# Usage: CopyAssets.sh <BuildFolder>   e.g. CopyAssets.sh Editor_Release
set -e
cd "$(dirname "$0")"

dest=".build/${1:?Usage: CopyAssets.sh <BuildFolder> (e.g. Editor_Release)}/Assets"

mkdir -p "$dest"
cp -a Engine/Assets/. "$dest"/
cp -a Assets/. "$dest"/
