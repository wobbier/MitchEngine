#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

dotnet "../Engine/Tools/Sharpmake/Sharpmake.Application.dll" "/sources('../Game.sharpmake.cs')"
