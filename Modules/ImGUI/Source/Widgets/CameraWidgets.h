#pragma once
#include "ScrubField.h"
#include <functional>

// Camera and lens widgets (UX-6), built on Graph like the light widgets: each is a whole inspector
// row (a caption in the label column, the widget in the value column), returns true when it changed
// a value and is made of real ImGui items, so the inspector turns a gesture into one undo step and
// automation finds the parts by caption: "Camera/Lens/FOV", "Camera/Lens/50 mm",
// "Camera/Resolution/50%", "PostProcess/Iris/2.8", "PostProcess/Focus Band/Focus".
namespace MeWidgets
{
    // Thin-lens photography math. Pure: no ImGui, unit tested.
    namespace Lens
    {
        // Full-frame 35 mm film is 36 x 24 mm: the sensor height cameras without one assume.
        constexpr double kFullFrameHeight = 24.0;

        // The focal length (mm) whose vertical field of view (degrees) on a sensor InSensorHeight
        // mm tall is InVerticalFov: f = h / (2 tan(fov / 2)).
        double FocalLength( double InVerticalFov, double InSensorHeight );
        // The vertical field of view (degrees) of a lens: fov = 2 atan(h / (2 f)).
        double VerticalFov( double InFocalLength, double InSensorHeight );

        // The acceptable circle of confusion (mm): 0.03 mm on full frame, scaled with the sensor.
        double CircleOfConfusion( double InSensorHeight );

        // What a lens focused at InFocus metres keeps acceptably sharp, in metres. Hyperfocal
        // H = f^2 / (N c) + f; near = s (H - f) / (H + s - 2f); far = s (H - f) / (H - s), or
        // infinity (Far = +inf) from the hyperfocal distance on.
        struct FocusRange
        {
            double Near = 0.0;
            double Far = 0.0;
            double Hyperfocal = 0.0;
        };
        FocusRange DepthOfField( double InFocalLength, double InFStop, double InFocus, double InCircleOfConfusion );

        // Nominal third stops (f/0.7 .. f/32) and the full stops among them (1.4 .. 22).
        const double* ThirdStops( int& OutCount );
        const double* FullStops( int& OutCount );
        // The nominal third stop nearest InFStop in log space (f/2.7 -> 2.8, f/3.3 -> 3.2).
        double SnapThirdStop( double InFStop );
    }

    // The equivalent focal length of a vertical field of view as a unit row in millimetres:
    // typing "35 mm" sets the field of view to 2 atan(h / 70). InSensorNote says where the sensor
    // height came from (shown in the tooltip).
    bool FocalLengthRow( const char* InCaption, double& InOutFov, double InSensorHeight, const char* InSensorNote );

    // The view frustum from the side: a wedge of the vertical field of view from its apex, an "FOV"
    // handle on its edge to drag it, and a lens ring under it with the classic primes (14 .. 200 mm)
    // to click; dragging along the ring zooms through focal lengths and ticks at each prime.
    bool LensWedge( const char* InCaption, double& InOutFov, double InSensorHeight, const char* InSensorNote );

    // Render scale with the internal resolution it gives ("1280 x 720 of 1920 x 1080") over a strip
    // of the scale with 50/67/75/100% detents to click. InInternalSize gives the internal size for
    // a scale (the engine's rounding, filters and overrides); InOutputNote says where the output
    // size came from (the Game View, a render texture).
    using RenderSizeFunction = std::function<void( double InScale, int& OutWidth, int& OutHeight )>;
    bool RenderScaleBar( const char* InCaption, double& InOutScale, int InOutputWidth, int InOutputHeight, const RenderSizeFunction& InInternalSize, const char* InOutputNote );

    // An aperture dial: an iris whose opening follows the f-number, and the full stops f/1.4 ..
    // f/22 to click. Dragging along the row scrubs in log space, snapping to third stops with a
    // tick at each.
    bool ApertureDial( const char* InCaption, double& InOutFStop );

    // Focus distance on a log bar (0.1 m .. 1 km) under the band the lens keeps sharp (near and far
    // limits from the focal length, f-number and sensor height), a draggable "Focus" handle and an
    // "H" marker that focuses at the hyperfocal distance.
    bool FocusBar( const char* InCaption, double& InOutFocus, double InFocalLength, double InFStop, double InSensorHeight, const char* InLensNote );
}
