#pragma once
#include <string>
#include "Dementia.h"
#include "Resource/Resource.h"

class Cubemap
    : public Resource
{
public:
    unsigned int Id = 0;
    std::string Type;
    std::string Directory;

    int Width = 0;
    int Height = 0;

    Cubemap( const Path& InFilePath );
    ~Cubemap();

    // Textures should not be copied around in memory
    ME_NONCOPYABLE( Cubemap );
    int nrChannels = 0;
};