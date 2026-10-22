#!/usr/bin/env bash
# Command-only entry point for IDE tasks and scripts; preserve their cwd.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_args=()
if [[ "${1:-}" == --project ]]; then
    [[ $# -ge 2 ]] || { echo "NixEnv.sh: --project requires a directory" >&2; exit 2; }
    project_args=(--project "$2")
    shift 2
fi
[[ $# -gt 0 ]] || { echo "Usage: NixEnv.sh [--project DIR] COMMAND [ARG...]" >&2; exit 2; }
exec "$here/DevShell.sh" "${project_args[@]}" --command "$@"
