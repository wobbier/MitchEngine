#pragma once
#include "Camera/PostProcessSettings.h"
#include <algorithm>
#include <cmath>

namespace Moonlight
{
    // Shared CPU lens setup. The shader evaluates Scale * (1 - FocusDistance / viewDepth).
    // Radius is signed: negative in front of focus, positive behind it, in full-resolution pixels.
    struct DepthOfFieldLens
    {
        float FocusDistance;
        float Scale;
        float MaxRadius;

        float Radius( float InViewDepth ) const
        {
            return std::clamp( Scale * (1.f - FocusDistance / std::max( InViewDepth, 0.0001f )), -MaxRadius, MaxRadius );
        }
    };

    inline DepthOfFieldLens MakeDepthOfFieldLens( const PostProcessSettings& InSettings, float InProjectionY, uint32_t InHeight )
    {
        const float sensor = std::clamp( InSettings.DoFSensorHeight, 1.f, 70.f ) * 0.001f;
        const float focalLength = sensor * std::clamp( std::abs( InProjectionY ), 0.01f, 200.f ) * 0.5f;
        const float focus = std::max( InSettings.DoFFocusDistance, focalLength + 0.001f );
        const float aperture = std::clamp( InSettings.DoFAperture, 0.7f, 32.f );
        const float height = static_cast<float>( std::max( InHeight, 1u ) );
        return { focus, focalLength * focalLength * height / (2.f * aperture * (focus - focalLength) * sensor),
            std::min( std::clamp( InSettings.DoFMaxBlurRadius, 0.f, 32.f ) * height / 1080.f, 64.f ) };
    }
}
