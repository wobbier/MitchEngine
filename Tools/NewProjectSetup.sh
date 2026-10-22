#!/usr/bin/env bash
# Creates a new MitchEngine project in the current folder.
# Run this from either an already checked out git repo or an empty folder.
#
# Usage: NewProjectSetup.sh [ProjectName] [EngineBranch]
set -e

if [ ! -e .git ]; then
    git init
fi

id=$1
branch=$2

if [ -z "$id" ]; then
    read -r -p "Enter a project name (Used for the project files): " id
fi

if [ -z "$id" ]; then
    id="EmptyProject"
fi

if [ -z "$branch" ]; then
    branch="master"
fi

echo "$id"
: > .gitmodules

git submodule add -b "$branch" --force --name Engine https://github.com/wobbier/MitchEngine Engine

git submodule update --init
git -C Engine submodule update --init

cp -a Engine/Tools/ProjectTemplate/. ./

# Stage the template so `nix develop` (which only sees files known to git) can
# pick up the flake in the fresh repo.
git add -A

# Generation needs cmake + dotnet. When nix is available (and we aren't already
# inside a shell), run those steps through the project's dev shell.
if [ -z "$IN_NIX_SHELL" ] && command -v nix >/dev/null 2>&1; then
    nix() { command nix --extra-experimental-features "nix-command flakes" "$@"; }
    nix develop --command bash Engine/ThirdParty/GenerateSolutions.sh
    nix develop --command bash Project/GenerateSolution.sh
else
    bash Engine/ThirdParty/GenerateSolutions.sh
    bash Project/GenerateSolution.sh
fi
