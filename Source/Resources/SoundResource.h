#pragma once
#include "Resource/Resource.h"

namespace FMOD
{
    class Sound;
    class System;
}

class Sound
    : public Resource
{
public:
    Sound( const Path& path, void* fmodSystem = nullptr, bool isImmediate = true );
    ~Sound();

    bool IsReady() const;

    FMOD::Sound* Handle = nullptr;
};
