@echo off
setlocal

set "id=%~1"
set "branch=%~2"
if "%id%"=="" set /P id=Enter project name (Used for the .sln):
if "%id%"=="" set "id=EmptyProject"
if "%branch%"=="" set "branch=master"

if not exist .git (
    git init || exit /b 1
)

rem CI supplies the exact event checkout. Keep its revision and repository.
if not exist Engine\.git (
    git submodule add -b "%branch%" --force --name Engine https://github.com/wobbier/MitchEngine Engine || exit /b 1
)
git -C Engine submodule update --init || exit /b 1

pushd Engine\ThirdParty
call GenerateSolutions.bat
if errorlevel 1 goto failed
popd

robocopy "Engine\Tools\ProjectTemplate" "." *.* /w:0 /r:1 /E /XC /XN /XO /log:"ProjectTemplate.log"
if errorlevel 8 exit /b 1

pushd Project
call GenerateSolution.bat
if errorlevel 1 goto failed
popd
exit /b 0

:failed
popd
exit /b 1
