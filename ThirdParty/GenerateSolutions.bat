cd Assimp
cmake -G "Visual Studio 15 2017 Win64" -DBUILD_SHARED_LIBS=false
cd ../Bullet
cmake -G "Visual Studio 15 2017 Win64" -DUSE_MSVC_RUNTIME_LIBRARY_DLL=true
cd ../