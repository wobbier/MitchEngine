#!/usr/bin/env bash
# Prints the solution name of the checkout in the working directory: the game's
# (Project/Game.meproj), or MitchEngine for the engine's own checkout.
set -euo pipefail
if [[ -f Project/Game.meproj ]]; then
    python3 -c 'import json; print(json.load(open("Project/Game.meproj"))["ProjectName"])'
elif [[ -f Engine.sharpmake.cs ]]; then
    echo MitchEngine
else
    echo "SolutionName.sh: $PWD is neither a game nor the engine checkout" >&2
    exit 2
fi
