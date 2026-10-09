#pragma once
#include "Math/Vector2.h"

struct FrameRenderData
{
    Vector2 MousePosition;
    uint32_t m_currentFrame = 0u;

    // so specific atm
    bool WasLeftPressed = false;
    // Set when a pick readback finished; RequestedEntityID is 0 when the click hit nothing.
    bool PickCompleted = false;
    uint64_t RequestedEntityID = 0;
};
