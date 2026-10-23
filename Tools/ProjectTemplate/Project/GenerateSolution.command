#!/usr/bin/env bash
set -euo pipefail

# Use the same portable Sharpmake bundle as Windows and Linux. The separate
# macOS bundle does not include the engine's UWP configuration extensions.
dotnet "../Engine/Tools/Sharpmake/Sharpmake.Application.dll" \
  "/sources('../Game.sharpmake.cs')"
