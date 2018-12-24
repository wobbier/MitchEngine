cd ThirdParty/OZZ
cmake -G "Visual Studio 15 2017 Win64" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0.14393.0
cd ../../
%~dp0Tools\premake5.exe --file=Tools/premake.lua vs2017