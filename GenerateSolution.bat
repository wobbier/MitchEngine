cd ThirdParty/GLFW
cmake -G "Visual Studio 15 2017"
cd ../../
cmake ThirdParty/Bullet/ -G "Visual Studio 15 2017" -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
%~dp0Tools\premake5.exe --file=Tools/premake.lua vs2017