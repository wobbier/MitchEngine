cd ThirdParty/GLFW/src
cmake -G "Visual Studio 15 2017" ..
cd ../../Bullet
cmake -G "Visual Studio 15 2017" -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
cd ../../Tools
%~dp0Tools\premake5.exe --file=premake.lua vs2017
cd ../