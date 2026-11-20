#pragma once
#include "ScrubField.h"
#include <array>
#include <string>
#include <vector>

// The gradient editor (UX-15): a colour-and-alpha ramp along a bar, edited by its stops. The bar
// shows the gradient over a checkerboard (its alpha), with an opaque band under it so a colour
// that fades out can still be seen. Each stop is one marker under the bar carrying both a colour
// and an alpha: the data this editor serves (particles' start and end, ramps baked to textures)
// keys colour and alpha at the same times, and one row of markers keeps the bar readable and
// every stop one click away. Click a stop to select it; its colour swatch (the UX-14 popover),
// position and alpha sit in a row under the bar. Drag a stop along the bar (Shift: fine steps
// and no snapping; it snaps to 0, 25, 50, 75 and 100%); drag it off the bar, or press Delete
// while it is selected, to remove it. Click the bar where there is no stop to add one there,
// sampled from the gradient (keep the button down to place it). A constrained editor (fixed
// stop count, locked positions) serves callers whose data is a fixed pair of colours.
//
// Colours are sRGB 0..1 as shown, and the gradient blends them in sRGB, as the particle shader
// path does (ParticleSystem::FillBatch mixes the sRGB colours, then converts to linear).
//
// Items: the bar is the caption ("Gradient", reporting "#FF9933 100% -> #99260D 0%"), the stops
// "Gradient/Stop 1".. (each reporting "#FF9933 100% at 0%"), and the selected stop's row
// "Gradient/Color", "Gradient/Position", "Gradient/Alpha" (also as "Gradient/Stop 2/Alpha").
namespace MeWidgets
{
    namespace GradientMath
    {
        using Rgb = std::array<float, 3>;

        struct Stop
        {
            // Where along the gradient, 0..1.
            float Position = 0.f;
            // sRGB 0..1 (above 1 for HDR colours).
            Rgb Color { 1.f, 1.f, 1.f };
            float Alpha = 1.f;
        };

        struct Sample
        {
            Rgb Color { 1.f, 1.f, 1.f };
            float Alpha = 1.f;
        };

        // The gradient at InT: the stops in position order (equal positions keep their order),
        // blended linearly in sRGB between neighbours, the end stops held beyond them. No stops:
        // opaque white.
        Sample Evaluate( const std::vector<Stop>& InStops, float InT );
        // Stop indices in position order (stable).
        std::vector<int> Order( const std::vector<Stop>& InStops );
        // Puts the stops in position order (stable) and returns where the stop at InTrack went
        // (-1 stays -1).
        int Sort( std::vector<Stop>& InOutStops, int InTrack = -1 );
        // Adds a stop at InT (clamped to 0..1) with the gradient's colour and alpha there, keeping
        // the stops in order, and returns its index. The gradient looks the same afterwards.
        int AddStop( std::vector<Stop>& InOutStops, float InT );
        // Removes the stop at InIndex unless that leaves fewer than InMinStops.
        bool RemoveStop( std::vector<Stop>& InOutStops, int InIndex, int InMinStops );

        // The positions a dragged stop snaps to and ticks at: 0, 0.25, 0.5, 0.75, 1.
        const float* Detents( int& OutCount );
        // InT on the detent within InReach of it, else InT.
        float SnapToDetent( float InT, float InReach );
        // A dragged position: rounded to whole percent (InFine: a tenth of one), clamped to 0..1.
        float RoundPosition( float InT, bool InFine );

        // "#FF9933 100%" (alpha as a percentage).
        std::string DescribeStop( const Stop& InStop );
        // The stops in position order joined by " -> ": "#FF9933 100% -> #99260D 0%".
        std::string Describe( const std::vector<Stop>& InStops );
    }

    struct GradientOptions
    {
        // Add and remove stay within these.
        int MinStops = 2;
        int MaxStops = 8;
        // Stops keep their positions (no drag along the bar, no position field): with MinStops ==
        // MaxStops, a constrained editor for data that is a fixed set of colours.
        bool FixedPositions = false;
        // Stop colours may go above 1 (the swatch edits an intensity).
        bool Hdr = false;
        // A second colour for the first stop (particles pick a start colour between two): drawn
        // as a thin band under the bar, the gradient with the first stop's colour swapped for it.
        const float* Variant = nullptr;
        // Shown when the bar is hovered, above the editing hints.
        const char* Tooltip = nullptr;
        // Names of the stops for the selected stop's row ("Start", "End"); null: "Stop N".
        const char* const* StopNames = nullptr;
    };

    // The whole editor at the cursor: a caption row with the bar and its stops, then the selected
    // stop's row. Returns true when any stop changed this frame (each gesture is an active item,
    // so the inspector makes it one undo step).
    bool GradientEditor( const char* InCaption, std::vector<GradientMath::Stop>& InOutStops, const GradientOptions& InOptions = GradientOptions() );
    // Forgets every editor's selection (the Widget Gallery's Reset).
    void ResetGradientEditors();
}
