cd ThirdParty/GLEW/build
cmake -G "Visual Studio 15 2017" ..
cd ../../../
%~dp0Tools\premake5.exe --file=Tools\premake.lua vs2017