#!/usr/bin/env bash
# Generates the engine's own Xcode workspace (Havana, the Player and the tests; Docs/Projects.md).
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ ! -f "$root/ThirdParty/Assimp/include/assimp/config.h" ]]; then
    (cd -- "$root/ThirdParty" && bash GenerateSolutions.command)
fi
cd -- "$root"
# The portable Sharpmake bundle, as game checkouts use (the macOS one lacks the UWP extensions).
exec dotnet "Tools/Sharpmake/Sharpmake.Application.dll" "/sources('Engine.sharpmake.cs')"
