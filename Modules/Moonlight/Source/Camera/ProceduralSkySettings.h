#pragma once
#include "Reflection/Reflection.h"
#include <algorithm>

namespace Moonlight
{
    // Authored per-camera values. The animated clock is kept separately by Camera.
    struct ProceduralSkySettings
    {
        ME_REFLECTABLE( ProceduralSkySettings )
    public:
        float Time = 15.f;
        float TimeScale = 0.f; // sky hours per real second; zero holds the time of day
        float Latitude = -6.f;
        float Turbidity = 1.9f;
        int Month = 5; // January = 0

        void Clamp()
        {
            Time = std::clamp( Time, 0.f, 24.f );
            TimeScale = std::clamp( TimeScale, 0.f, 1.f );
            Latitude = std::clamp( Latitude, -90.f, 90.f );
            Turbidity = std::clamp( Turbidity, 1.9f, 10.f );
            Month = std::clamp( Month, 0, 11 );
        }
    };
}
