#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
if [[ "$(uname -s)" != Linux ]]; then
    cd -- "$project_root/Project"
    exec dotnet "../Engine/Tools/Sharpmake/Sharpmake.Application.dll" "/sources('../Game.sharpmake.cs')"
fi
cd -- "$project_root"
exec "$project_root/Engine/Tools/GenerateProject.sh" "$@"
