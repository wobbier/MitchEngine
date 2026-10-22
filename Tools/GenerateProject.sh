#!/usr/bin/env bash
# Shared Linux generation. No game-specific names or Nix files are required.
# --compile-commands retains the existing bear-based editor build when requested.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ "${1:-}" != --in-dev-shell ]]; then
    exec "$here/NixEnv.sh" bash "$here/GenerateProject.sh" --in-dev-shell "$@"
fi
shift
compile_commands=0
if [[ "${1:-}" == --compile-commands ]]; then
    compile_commands=1
    shift
fi
[[ $# -eq 0 ]] || { echo "Usage: GenerateProject.sh [--compile-commands]" >&2; exit 2; }
cd -- "${MITCH_PROJECT_ROOT:?Run through the engine environment launcher}"

if [[ ! -f "$MITCH_ENGINE_ROOT/ThirdParty/Assimp/include/assimp/config.h" ]]; then
    bash "$MITCH_ENGINE_ROOT/ThirdParty/GenerateSolutions.sh"
fi
(
    cd Project
    dotnet "$MITCH_ENGINE_ROOT/Tools/Sharpmake/Sharpmake.Application.dll" "/sources('../Game.sharpmake.cs')"
)

if [[ "$compile_commands" == 1 ]]; then
    project_name="$(python3 -c 'import json; print(json.load(open("Project/Game.meproj"))["ProjectName"])')"
    bear --output compile_commands.json -- make -f "$project_name.make" Havana config=editor_debug -j"${MITCH_BUILD_JOBS:-$(nproc)}"
fi
