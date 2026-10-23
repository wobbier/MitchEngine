#!/usr/bin/env bash
# Creates a new MitchEngine project in the current folder.
# Run this from either an already checked out git repo or an empty folder.
#
# Usage: NewProjectSetup.sh [ProjectName] [EngineBranch]
set -euo pipefail

if [ ! -e .git ]; then
    git init
fi

id=${1:-}
branch=${2:-master}

if [ -z "$id" ]; then
    read -r -p "Enter a project name (Used for the project files): " id
fi

if [ -z "$id" ]; then
    id="EmptyProject"
fi

if [[ ! "$id" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
    echo "Use letters, digits and underscores for the project name (starting with a letter or underscore)." >&2
    exit 2
fi

if [ -z "$branch" ]; then
    branch="master"
fi

echo "$id"
# CI checks out the exact event repository and revision into Engine first.
# Preserve that checkout instead of cloning the current upstream branch tip.
if [ ! -e Engine/.git ]; then
    git submodule add -b "$branch" --force --name Engine https://github.com/wobbier/MitchEngine Engine
fi
git -C Engine submodule update --init

cp -a Engine/Tools/ProjectTemplate/. ./
sed "s/EmptyProject/$id/g" Game.sharpmake.cs > Game.sharpmake.cs.tmp
mv Game.sharpmake.cs.tmp Game.sharpmake.cs

# Linux generation and F5 share the SDK's environment, even from another shell.
if [ "$(uname -s)" = Linux ]; then
    bash Engine/Tools/GenerateProject.sh
else
    (cd Engine/ThirdParty && bash GenerateSolutions.command)
    (cd Project && bash GenerateSolution.command)
fi
