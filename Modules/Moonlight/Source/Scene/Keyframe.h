#pragma once
#include "Math/Quaternion.h"

namespace Moonlight
{
    struct Keyframe
    {
        float Time = 0.f;
        Vector3 Translation;
        Quaternion Rotation;
        Vector3 Scale;
    };
}