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

if [[ ! "$id" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
    echo "Use letters, digits and underscores for the project name (starting with a letter or underscore)." >&2
    exit 2
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
sed "s/EmptyProject/$id/g" Game.sharpmake.cs > Game.sharpmake.cs.tmp
mv Game.sharpmake.cs.tmp Game.sharpmake.cs

# Linux generation and F5 share the SDK's environment, even from another shell.
if [ "$(uname -s)" = Linux ]; then
    bash Engine/Tools/GenerateProject.sh
else
    bash Engine/ThirdParty/GenerateSolutions.sh
    bash Project/GenerateSolution.sh
fi
