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
        float TimeScale = 0.f; // sky hours per game second in Play; zero holds the time of day
        // Evaluated runtime state, excluded from reflection and scene serialization.
        float AnimationTime = 0.f;
        float Latitude = -6.f;
        float Turbidity = 1.9f;
        int Month = 5; // January = 0

        // Sun, moon and stars
        float MoonPhase = 0.4f;         // 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter
        float MoonOrbitInclination = 5.145f; // degrees relative to the ecliptic; zero aligns a new moon for an eclipse
        float MoonAscendingNode = 125.08f;   // degrees along the ecliptic where the moon crosses north
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

        // Metres added to the camera's own height for the sky alone: the sky and its clouds as seen
        // from up there, for a scene that fakes a climb (the set stays put and the sky goes by).
        float ViewerAltitude = 0.f;

        // A convective sky (0 off .. 1): the layer's clouds heap up, each with its own top, and
        // cumulonimbus towers (most with anvils) stand up out of it far off, toward the horizon.
        float Convection = 0.f;
        // A clearing in the layer round the viewer (0 none .. 1): the cover thins out within about
        // CloudClearingRadius of the camera, so a climb through the layer goes up a gap in it.
        float CloudClearing = 0.f;
        float CloudClearingRadius = 400.f;  // metres

        // Runtime only (not reflected): the scene below world height SceneUnderCloudsBelow lies
        // under the cloud layer, so the sky's clouds are laid over it where it shows (a faked
        // climb's ground, seen down through the gaps). Only while the camera is above that height.
        bool SceneUnderClouds = false;
        float SceneUnderCloudsBelow = 0.f;

        void Clamp()
        {
            Time = std::clamp( Time, 0.f, 24.f );
            TimeScale = std::clamp( TimeScale, 0.f, 1.f );
            Latitude = std::clamp( Latitude, -90.f, 90.f );
            Turbidity = std::clamp( Turbidity, 1.f, 10.f );
            Month = std::clamp( Month, 0, 11 );
            MoonPhase = std::clamp( MoonPhase, 0.f, 1.f );
            MoonOrbitInclination = std::clamp( MoonOrbitInclination, 0.f, 10.f );
            MoonAscendingNode = std::clamp( MoonAscendingNode, 0.f, 360.f );
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
            ViewerAltitude = std::clamp( ViewerAltitude, 0.f, 20000.f );
            Convection = std::clamp( Convection, 0.f, 1.f );
            CloudClearing = std::clamp( CloudClearing, 0.f, 1.f );
            CloudClearingRadius = std::clamp( CloudClearingRadius, 10.f, 20000.f );
        }
    };
}
