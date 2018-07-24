cd ThirdParty/GLFW
cmake -G "Visual Studio 15 2017" -DBUILD_SHARED_LIBS=true
cd ../AssIMP
cmake -G "Visual Studio 15 2017"
cd ../Bullet
cmake -G "Visual Studio 15 2017" -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
cd ../../
%~dp0Tools\premake5.exe --file=Tools/premake.lua vs2017