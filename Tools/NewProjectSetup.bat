@echo off

IF NOT EXIST .git (
git init
)

set id=%1
set branch=%2

if "%1"=="" set /P id=Enter project name (Used for the .sln): 

if "%id%"=="" set id=ExampleProject

if "%2"=="" set branch=master

echo 2> .gitmodules

git submodule add -b %branch% --force --name Engine https://github.com/wobbier/MitchEngine Engine

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

cd Project
call GenerateSolution.bat
cd ..