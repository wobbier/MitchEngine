#pragma once
#include "NodeAnimation.h"

namespace Moonlight
{
    struct AnimationClip
    {
        std::string Name;
        float Duration = 0.f;
        float TicksPerSecond = 0.f;
        std::vector<NodeAnimation> NodeChannels;
    };
}