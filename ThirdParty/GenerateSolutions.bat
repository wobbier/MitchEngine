cd Assimp
cmake -G "Visual Studio 15 2017 Win64" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0.14393.0
cd ../Bullet
cmake -G "Visual Studio 15 2017 Win64" -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
cd ../Brofiler
call GenerateProjects.bat
cd ../