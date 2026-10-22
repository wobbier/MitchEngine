#!/usr/bin/env bash
set -euo pipefail

dotnet "../Engine/Tools/Sharpmake/macOS/Sharpmake.Application.dll" \
  "/sources('../Game.sharpmake.cs')"