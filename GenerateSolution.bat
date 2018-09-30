cd ThirdParty/GLFW
cmake -G "Visual Studio 15 2017 Win64" -DBUILD_SHARED_LIBS=true
cd ../AssIMP
cmake -G "Visual Studio 15 2017 Win64"
cd ../Bullet
cmake -G "Visual Studio 15 2017 Win64" -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
cd ../Brofiler
call GenerateProjects.bat
cd ../../
%~dp0Tools\premake5.exe --file=Tools/premake.lua vs2017