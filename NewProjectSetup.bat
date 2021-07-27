@echo off

IF NOT EXIST .git (
git init
)

set id=%1

if "%1"=="" set /P id=Enter project name (Used for the .sln): 

if "%id%"=="" set id=ExampleProject

echo 2> .gitmodules

git submodule add --force --name Engine https://github.com/wobbier/MitchEngine Engine

git submodule init
git submodule update

cd Engine
git submodule init
git submodule update
cd ThirdParty
call GenerateSolutions.bat
cd ../../

mkdir Assets

robocopy "Engine\\Tools\\ProjectTemplate" "./" *.* /w:0 /r:1 /v /E /XC /XN /XO /log:"ProjectTemplate.log"

echo 2> GenerateSolution.bat
echo %%~dp0Engine\Tools\premake5.exe --file=premake.lua vs2019 --project-type=Win64 --project-name=%id%>> GenerateSolution.bat

echo 2> GenerateSolutionUWP.bat
echo %%~dp0Engine\Tools\premake5.exe --file=premake.lua vs2019 --project-type=UWP --project-name=%id%>> GenerateSolutionUWP.bat

echo 2> GenerateSolution.command
echo ./Engine/Tools/premake5 --file=premake.lua xcode4 --project-type=macOS --project-name=%id%>> GenerateSolution.command

call GenerateSolution.bat