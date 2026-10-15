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

        // Sun, moon and stars
        float MoonPhase = 0.4f;         // 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter
        float SunSize = 1.5f;           // angular size relative to the real sun
        float MoonSize = 3.f;           // angular size relative to the real moon
        float MoonBrightness = 1.f;     // scales moonlight on the scene and in the sky
        float StarBrightness = 1.f;

        // Clouds
        float CloudCoverage = 0.4f;     // 0 clear .. 1 overcast
        float CloudDensity = 1.f;
        float CloudAltitude = 1500.f;   // metres to the cloud base
        float CloudThickness = 2500.f;  // metres from base to top
        float WindSpeed = 8.f;          // metres per second
        float WindDirection = 60.f;     // degrees clockwise from north (+Z) the wind blows towards

        void Clamp()
        {
            Time = std::clamp( Time, 0.f, 24.f );
            TimeScale = std::clamp( TimeScale, 0.f, 1.f );
            Latitude = std::clamp( Latitude, -90.f, 90.f );
            Turbidity = std::clamp( Turbidity, 1.f, 10.f );
            Month = std::clamp( Month, 0, 11 );
            MoonPhase = std::clamp( MoonPhase, 0.f, 1.f );
            SunSize = std::clamp( SunSize, 0.1f, 10.f );
            MoonSize = std::clamp( MoonSize, 0.1f, 10.f );
            MoonBrightness = std::clamp( MoonBrightness, 0.f, 10.f );
            StarBrightness = std::clamp( StarBrightness, 0.f, 10.f );
            CloudCoverage = std::clamp( CloudCoverage, 0.f, 1.f );
            CloudDensity = std::clamp( CloudDensity, 0.f, 4.f );
            CloudAltitude = std::clamp( CloudAltitude, 100.f, 10000.f );
            CloudThickness = std::clamp( CloudThickness, 100.f, 8000.f );
            WindSpeed = std::clamp( WindSpeed, 0.f, 100.f );
            WindDirection = std::clamp( WindDirection, 0.f, 360.f );
        }
    };
}
