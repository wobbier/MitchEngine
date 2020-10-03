@echo off

set /P id=Enter project name (Used for the .sln): 

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
echo %%~dp0Engine\Tools\premake5.exe --file=premake.lua vs2017 --gfxapi=directx --project-name=%id%>> GenerateSolution.bat

call GenerateSolution.bat