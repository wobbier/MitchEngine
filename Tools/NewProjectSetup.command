#!/usr/bin/env bash
# Creates a macOS project, preserving an Engine checkout supplied by CI.
set -euo pipefail

id=${1:-}
branch=${2:-master}

if [ -z "$id" ]; then
    read -r -p "Enter project name (Used for the project files): " id
fi
id=${id:-EmptyProject}
branch=${branch:-master}

if [[ ! "$id" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
    echo "Use letters, digits and underscores for the project name (starting with a letter or underscore)." >&2
    exit 2
fi

if [ ! -e .git ]; then
    git init
fi
if [ ! -e Engine/.git ]; then
    git submodule add -b "$branch" --force --name Engine https://github.com/wobbier/MitchEngine Engine
fi
git -C Engine submodule update --init

cp -a Engine/Tools/ProjectTemplate/. ./
sed "s/EmptyProject/$id/g" Game.sharpmake.cs > Game.sharpmake.cs.tmp
mv Game.sharpmake.cs.tmp Game.sharpmake.cs

(cd Engine/ThirdParty && bash GenerateSolutions.command)
(cd Project && bash GenerateSolution.command)
