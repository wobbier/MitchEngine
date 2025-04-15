#pragma once
#include <unordered_map>
#include <string>
#include <vector>

#include "Math/Matrix4.h"

namespace Moonlight
{
    struct BoneInfo
    {
        Matrix4 Offset;
        Matrix4 FinalTransform;
    };

    struct Skeleton
    {
        std::unordered_map<std::string, uint32_t> BoneNameToIndex;
        std::vector<BoneInfo> Bones;
    };
}