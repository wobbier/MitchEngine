#!/usr/bin/env bash
# VS Code's F5 build entry point. Generate before building, including fresh clones.
# Usage: BuildProject.sh [--project DIR] [TARGET [CONFIG [MAKE_ARGS...]]]
# --project names the checkout to build (an engine checkout builds its own solution); without
# it, the game around the working directory, else the engine (DevShell.sh).
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_args=()
if [[ "${1:-}" == --project ]]; then
    [[ $# -ge 2 ]] || { echo "BuildProject.sh: --project requires a directory" >&2; exit 2; }
    project_args=(--project "$2")
    shift 2
fi
if [[ "${1:-}" != --in-dev-shell ]]; then
    exec "$here/NixEnv.sh" "${project_args[@]}" bash "$here/BuildProject.sh" --in-dev-shell "$@"
fi
shift
target="${1:-Havana}"
config="${2:-editor_debug}"
if [[ $# -gt 0 ]]; then shift; fi
if [[ $# -gt 0 ]]; then shift; fi
cd -- "${MITCH_PROJECT_ROOT:?Run through the engine environment launcher}"
bash "$here/GenerateProject.sh" --in-dev-shell
project_name="$(bash "$here/SolutionName.sh")"
exec make -f "$project_name.make" "$target" "config=$config" -j"${MITCH_BUILD_JOBS:-$(nproc)}" "$@"
