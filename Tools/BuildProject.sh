#!/usr/bin/env bash
# VS Code's F5 build entry point. Generate before building, including fresh clones.
# Usage: BuildProject.sh [TARGET [CONFIG [MAKE_ARGS...]]]
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ "${1:-}" != --in-dev-shell ]]; then
    exec "$here/NixEnv.sh" bash "$here/BuildProject.sh" --in-dev-shell "$@"
fi
shift
target="${1:-Havana}"
config="${2:-editor_debug}"
if [[ $# -gt 0 ]]; then shift; fi
if [[ $# -gt 0 ]]; then shift; fi
cd -- "${MITCH_PROJECT_ROOT:?Run through the engine environment launcher}"
bash "$here/GenerateProject.sh" --in-dev-shell
project_name="$(python3 -c 'import json; print(json.load(open("Project/Game.meproj"))["ProjectName"])')"
exec make -f "$project_name.make" "$target" "config=$config" -j"${MITCH_BUILD_JOBS:-$(nproc)}" "$@"
