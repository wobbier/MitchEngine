#!/usr/bin/env bash
# Enter the SDK environment, or run a command with a project's private additions.
# Usage: DevShell.sh [--project DIR] [--command COMMAND [ARG...]]
set -euo pipefail

engine_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
project_root=""
if [[ "${1:-}" == --project ]]; then
    [[ $# -ge 2 ]] || { echo "DevShell.sh: --project requires a directory" >&2; exit 2; }
    project_root="$(cd -- "$2" && pwd -P)"
    shift 2
fi

if [[ -z "$project_root" ]]; then
    candidate="$PWD"
    while [[ "$candidate" != / ]]; do
        if [[ -f "$candidate/Game.sharpmake.cs" ]]; then
            project_root="$candidate"
            break
        fi
        candidate="$(dirname -- "$candidate")"
    done
    if [[ -z "$project_root" && -f "$engine_root/../Game.sharpmake.cs" ]]; then
        project_root="$(cd -- "$engine_root/.." && pwd -P)"
    fi
fi
# No game around the engine: the engine checkout is its own root (its own solution, its own
# .build/ and .tmp/; Docs/Projects.md).
if [[ -z "$project_root" ]]; then
    project_root="$engine_root"
fi

command -v nix >/dev/null || {
    echo "DevShell.sh: install Nix to use the engine's Linux development environment" >&2
    exit 127
}
export MITCH_ENGINE_ROOT="$engine_root"
export MITCH_PROJECT_ROOT="$project_root"

# --impure permits importing the local project's extension. Package versions
# still come exclusively from the SDK's checked-in flake.lock.
nix_args=(--impure --file "$engine_root/nix/project-shell.nix"
    --argstr engineRoot "$engine_root" --argstr projectRoot "$project_root")
if [[ $# -eq 0 ]]; then
    cd -- "$project_root"
    exec nix --extra-experimental-features "nix-command flakes" develop "${nix_args[@]}"
fi
if [[ "$1" != --command || $# -lt 2 ]]; then
    echo "Usage: DevShell.sh [--project DIR] [--command COMMAND [ARG...]]" >&2
    exit 2
fi
shift

# Capture separately so a failed Nix evaluation cannot fall through to running
# a build or debugger with the host environment. Keep stdout clean for GDB/MI.
dev_environment="$(nix --extra-experimental-features "nix-command flakes" print-dev-env "${nix_args[@]}")"
eval "$dev_environment" >/dev/null
# print-dev-env defines shellHook; it does not execute it.
eval "${shellHook:-}" >/dev/null
exec "$@"
