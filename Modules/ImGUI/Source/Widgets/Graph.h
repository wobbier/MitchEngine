#pragma once
#include "Units.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <functional>

// Small plot canvases for inspector widgets (UX-5): a falloff curve, a temperature bar, a log
// ladder of reference values, a cone. A graph maps two axes (linear or log) onto a screen rect,
// draws a grid with tick labels in the axis unit, plots functions or samples, and carries
// draggable handles. Handles are real ImGui items (active while dragged, edited when they move,
// named for automation: "Light/Falloff/Range") with Juice feedback: they grow on hover and tick
// when they cross a detent. Empty canvas space is an item too, so a click or drag on the canvas
// can set a value. Sizes come from the font, colours from the theme.
//
//   if( Graph::Begin( "Falloff", size, xAxis, yAxis ) ) { Graph::Grid(); Graph::Plot( f, color ); }
//   Graph::DragHandle( "Range", &range, nullptr, spec );
//   const Graph::CanvasInput input = Graph::End();
namespace MeWidgets::Graph
{
    struct Axis
    {
        double Min = 0.0;
        double Max = 1.0;
        // Log axes map through LogMap (a range starting at 0 is shifted so 0 stays on the axis).
        bool Log = false;
        // Tick labels and handle values are formatted in this unit.
        Unit AxisUnit = Unit::None;

        double ToNormalized( double InValue ) const;
        double FromNormalized( double InT ) const;
    };

    // Values <-> screen positions in a rect. Y grows upwards (Max at the top). Pure: no ImGui
    // context needed, so the math is unit tested.
    struct Mapping
    {
        ImVec2 Min;
        ImVec2 Max;
        Axis X;
        Axis Y;

        float XToScreen( double InX ) const;
        float YToScreen( double InY ) const;
        ImVec2 ToScreen( double InX, double InY ) const { return ImVec2( XToScreen( InX ), YToScreen( InY ) ); }
        double XFromScreen( float InScreenX ) const;
        double YFromScreen( float InScreenY ) const;
    };

    // Round tick values inside an axis: NiceStep for linear axes, 1-2-5 per decade for log ones.
    // At most InMaxTicks (the coarser steps win).
    int Ticks( const Axis& InAxis, double* OutValues, int InMaxTicks );

    // Starts a canvas at the cursor. InSize.x <= 0 fills the row; the plot area is inset so
    // tick labels fit below (InXLabels) it. Returns false when the canvas is clipped: skip the
    // drawing, but still call End().
    struct Frame
    {
        // Room below the plot area for X tick labels (or a widget's own markers).
        bool XLabels = true;
        // A recessed background behind the plot area.
        bool Background = true;
        // Gap between the canvas edge and the plot area; < 0: a quarter em.
        float Inset = -1.f;
        // More room under the plot area (and its X labels) for the widget's own row of markers.
        float Below = 0.f;
    };
    bool Begin( const char* InId, ImVec2 InSize, const Axis& InX, const Axis& InY, const Frame& InFrame = Frame() );
    // The whole canvas (plot area plus label room), and its item ID (for Juice flashes).
    ImRect CanvasRect();
    ImGuiID CanvasId();
    const Mapping& Current();
    ImDrawList* DrawList();

    // Grid lines at the axis ticks with labels in the axis unit.
    void Grid( bool InXLabels = true, bool InYLabels = false );
    // Plots y = f(x) over the X axis, optionally filled down to the bottom of the plot area.
    void Plot( const std::function<double( double )>& InFunction, ImU32 InColor, bool InFill = false, int InSamples = 96, float InThickness = 0.f );
    void PlotSamples( const double* InX, const double* InY, int InCount, ImU32 InColor, bool InFill = false, float InThickness = 0.f );

    struct HandleSpec
    {
        // Clamp, in axis units.
        double MinX = -1e300;
        double MaxX = 1e300;
        double MinY = -1e300;
        double MaxY = 1e300;
        // The handle ticks (Juice) when its X crosses one of these.
        const double* Detents = nullptr;
        int DetentCount = 0;
        // 0: the theme accent.
        ImU32 Color = 0;
        // Draws the value next to the handle while it is hovered or dragged.
        bool ShowValue = true;
        // The X axis (or whatever the handle is dragged across) runs from this fixed screen
        // mapping while dragging: a handle whose value rescales the axis stays under the pointer.
        bool FreezeAxis = false;
    };

    // A draggable point at (*InOutX, *InOutY) inside the current graph; a null Y sits on the
    // bottom of the plot area and only moves in X. InLabel is shown and is what automation
    // finds it by. Returns true when the value changed this frame.
    bool DragHandle( const char* InLabel, double* InOutX, double* InOutY, const HandleSpec& InSpec = HandleSpec() );

    struct CanvasInput
    {
        bool Hovered = false;
        bool Pressed = false;   // clicked this frame
        bool Held = false;      // pressed, or dragging
        double X = 0.0;         // the pointer in axis units
        double Y = 0.0;
    };
    // Ends the graph and returns the canvas' input this frame. The canvas is an item named after
    // the graph (CanvasId()) that handles and markers drawn over it take the pointer from.
    CanvasInput End();

    // ---- Angles -------------------------------------------------------------------------
    // InDirection is the cone axis in screen radians (0 = right, pi/2 = down); angles are half
    // angles from it, positive clockwise on screen (screen Y points down).
    ImVec2 ArcPoint( ImVec2 InApex, float InDirection, float InAngle, float InRadius );
    // The signed angle (radians) of InPoint around InApex from InDirection, in -pi..pi.
    float AngleOf( ImVec2 InApex, float InDirection, ImVec2 InPoint );
    // A cone from InApex: full InColor inside the inner half angle, feathered to nothing at the
    // outer one (squared, like the engine's spot cone), fading along the radius.
    void Wedge( ImDrawList* InDrawList, ImVec2 InApex, float InDirection, float InInnerHalf, float InOuterHalf, float InRadius, ImU32 InColor );
    // A handle on an arc around InApex at InRadius, dragged around it: InOutDegrees is the half
    // angle, clamped to InMin..InMax. Same item rules as DragHandle.
    // InSide (+1 or -1) picks the side of the axis the handle sits on. InShownScale scales what
    // the handle shows, reports and rounds to (2: a full angle such as a field of view, dragged
    // by its half angle).
    bool AngleHandle( const char* InLabel, ImVec2 InApex, float InDirection, float InSide, float InRadius, double& InOutDegrees, double InMin, double InMax, ImU32 InColor = 0, double InShownScale = 1.0 );

    // ---- Shared by the widgets built on graphs --------------------------------------------
    // The ID DragHandle( InLabel ) gets inside Begin( InGraphId ), for widgets that need to know
    // before Begin whether one of their handles is being dragged (to hold an axis still).
    ImGuiID HandleId( const char* InGraphId, const char* InLabel );
    // Names the last item's row for automation (the widget's caption: "Falloff").
    void SetRow( const char* InRow );
    // A small clickable label/marker item at InRect (presets on a bar, rungs on a ladder).
    // Returns true when clicked. Flashes and ticks are the caller's.
    bool MarkerButton( const char* InLabel, const ImRect& InRect, bool* OutHovered = nullptr );
}
