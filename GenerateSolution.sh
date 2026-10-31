#!/usr/bin/env bash
# Generates the engine's own solution (Havana, the Player and the tests; Docs/Projects.md):
# MitchEngine.make on Linux (inside the Nix dev shell), MitchEngine.xcworkspace on macOS.
# Then: make -f MitchEngine.make Havana config=editor_debug -j$(nproc), or open the
# folder in VS Code and press F5.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ "$(uname -s)" != Linux ]]; then
    exec bash "$root/GenerateSolution.command" "$@"
fi
exec "$root/Tools/NixEnv.sh" --project "$root" bash "$root/Tools/GenerateProject.sh" --in-dev-shell "$@"
