#pragma once
#include "Math/Vector2.h"

struct FrameRenderData
{
    Vector2 MousePosition;
    uint32_t m_currentFrame = 0u;

    // so specific atm
    bool WasLeftPressed = false;
    uint64_t RequestedEntityID = 0;
};
