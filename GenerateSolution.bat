@echo off
rem Generates the engine's own solution, MitchEngine.sln (Havana, the Player and the tests;
rem Docs/Projects.md). Open it, pick Editor_Debug and press F5: Havana opens the Project Hub.
setlocal
pushd "%~dp0"
if not exist "ThirdParty\Assimp\include\assimp\config.h" (
    pushd ThirdParty
    call GenerateSolutions.bat
    popd
)
"Tools\Sharpmake\Sharpmake.Application.exe" "/sources('Engine.sharpmake.cs')"
set "status=%errorlevel%"
popd
exit /b %status%
