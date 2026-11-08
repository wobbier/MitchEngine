#pragma once
#include "ScrubField.h"

// Light editing widgets (UX-5), built on Graph: each is a whole inspector row (a caption in the
// label column, the widget in the value column) and returns true when it changed a value. All
// are real ImGui items, so the inspector turns a gesture into one undo step, and automation finds
// their parts by caption: "Light/Temperature/Candle", "Light/Cone/Inner Cone", "Light/Falloff/Range".
namespace MeWidgets
{
    // A bar of blackbody colours (1000 K..12000 K, log scale). Click or drag to set InOutSrgb to
    // that temperature's colour (sRGB, brightest channel 1); presets under the bar (Candle 1900 K
    // .. Blue sky 10000 K) set theirs and the drag snaps to them. A marker and "5600 K" show the
    // colour's temperature when it lies on the blackbody curve.
    bool KelvinBar( const char* InCaption, float* InOutSrgb );

    // Reference values on a log strip over InSpec's soft range: labelled rungs to click, the
    // strip to drag along.
    struct Reference
    {
        const char* Name;
        double Value;
        const char* Note;
    };
    bool ReferenceLadder( const char* InCaption, double& InOutValue, const NumberSpec& InSpec, const Reference* InRungs, int InRungCount, const char* InTooltip );

    // A beam seen side-on: the outer half angle as a wedge from its apex, full light colour
    // inside the inner half angle, feathered between. "Inner Cone" and "Outer Cone" handles on the
    // arcs drag the half angles (degrees, 0..InMax); pushing one past the other carries it along,
    // so inner <= outer always holds.
    bool ConeWedge( const char* InCaption, double& InOutInner, double& InOutOuter, const float* InSrgb, double InMax = 89.0 );

    // The engine's distance falloff (Assets/Shaders/Lighting.sh distanceAttenuation): inverse
    // square, windowed so it reaches zero at InRange. InNearField (m^2) softens extended emitters.
    double LightAttenuation( double InDistance, double InRange, double InNearField = 0.0 );

    // The falloff curve against distance (0..1.2 x range, metres) with a "Range" handle on the
    // axis. The axis holds still while the handle is dragged and eases to the new range after.
    bool FalloffGraph( const char* InCaption, double& InOutRange, const NumberSpec& InSpec, const float* InSrgb, double InNearField = 0.0 );
}
