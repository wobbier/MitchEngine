#!/usr/bin/env bash
# No shell banner on stdout: VS Code communicates with GDB using MI.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_args=()
gdb_args=()
# Debug adapters can put their MI flags before miDebuggerArgs. Remove our
# option wherever it occurs, while leaving debuggee arguments after --args.
while [[ $# -gt 0 ]]; do
    case "$1" in
        --project)
            [[ $# -ge 2 ]] || { echo "NixGdb.sh: --project requires a directory" >&2; exit 2; }
            project_args=(--project "$2")
            shift 2
            ;;
        --args|--)
            gdb_args+=("$@")
            break
            ;;
        *) gdb_args+=("$1"); shift ;;
    esac
done
exec "$here/NixEnv.sh" "${project_args[@]}" gdb "${gdb_args[@]}"
