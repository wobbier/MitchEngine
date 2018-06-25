cd ThirdParty/GLFW/src
cmake -G "Visual Studio 15 2017" ..
cd ../../
cd Bullet/build3
premake5 --targetdir="../bin" vs2010
cd ../../../Tools
%~dp0Tools\premake5.exe --file=premake.lua vs2017