#pragma once
#include "Keyframe.h"

namespace Moonlight
{
    struct NodeAnimation
    {
        std::string NodeName;
        std::vector<Keyframe> Keyframes;
    };
}