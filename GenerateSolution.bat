mkdir ThirdParty/GLFW/build
cd ThirdParty/GLFW/src
cmake -G "Visual Studio 15 2017" ..
cd ../../../
cd ThirdParty/SOIL/src
cmake -G "Visual Studio 15 2017" ..
cd ../../../
%~dp0Tools\premake5.exe --file=Tools\premake.lua vs2017