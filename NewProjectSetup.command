#!/bin/bash
# Run this from either an already checked out git repo or an empty folder 

if [[ ! -f ".git" ]]
then
    git init
fi

id=$1;
if [[ $1 == "" ]]
then
echo Enter a project name \(Used for the project files\): 
read id
fi

if [[ $id == "" ]]
then
id="EmptyProject";
fi

echo $id
echo 2> .gitmodules

git submodule add --force --name Engine https://github.com/wobbier/MitchEngine Engine

git submodule init
git submodule update

cd Engine
git submodule init
git submodule update
cd ThirdParty
sh GenerateSolutions.command
cd ../../

cp -a Engine/Tools/ProjectTemplate/. ./

echo 2> GenerateSolution.bat
echo %%~dp0Engine\Tools\premake5.exe --file=premake.lua vs2019 --project-type=Win64 --project-name=$id>> GenerateSolution.bat

echo 2> GenerateSolutionUWP.bat
echo %%~dp0Engine\Tools\premake5.exe --file=premake.lua vs2019 --project-type=UWP --project-name=$id>> GenerateSolutionUWP.bat

echo 2> GenerateSolution.command
echo ./Engine/Tools/premake5 --file=premake.lua xcode4 --project-type=macOS --project-name=$id>> GenerateSolution.command

sh GenerateSolution.command