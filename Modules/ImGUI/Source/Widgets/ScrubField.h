#pragma once
#include "Units.h"
#include <imgui.h>

// The unit field (UX-2): numbers that mean something. A row's label and its value box both scrub:
// drag left/right to change the value (Shift fine, Ctrl coarse, Alt snaps to round steps), in
// log space for log fields. A click without a drag, a double-click on the box, or Ctrl+click
// types a value ("2 m", "150 cm", "90°", "-6 dB", "1+2"); Enter applies it, Escape cancels.
// Double-clicking the label restores the default. The value shows in its unit with a fill bar
// over the (soft) range. Both are real ImGui items: active while dragged, edited when the value
// changes, so the inspector's undo makes one step per gesture.
namespace MeWidgets
{
    struct NumberSpec
    {
        Unit FieldUnit = Unit::None;
        // Hard clamp, in stored units; also the scrub span and fill bar when there's no soft range.
        bool HasRange = false;
        double Min = 0.0;
        double Max = 0.0;
        // A clamp that never sets the scrub span (a type's limits, a .Drag field's range).
        bool HasClamp = false;
        double ClampMin = 0.0;
        double ClampMax = 0.0;
        // The span a scrub across the field and the fill bar cover; the hard range when unset.
        bool HasSoftRange = false;
        double SoftMin = 0.0;
        double SoftMax = 0.0;
        bool Log = false;
        // Stored units per pixel when there is no range (a field's Speed).
        double Speed = 0.1;
        bool Integer = false;
        bool HasDefault = false;
        double Default = 0.0;
        int Decimals = -1;

        // The span scrubs and the bar use, or false when the field is unbounded.
        bool ScaleRange( double& OutMin, double& OutMax ) const;
    };

    // Makes the last item (a row label from HavanaUtils::Label) scrub InOutValue and reset it on
    // double-click. InPixelsForSpan is how far a drag goes to cross the scale range (the value
    // box's width). Returns true when the value changed.
    // InMixed: multi-selected entities hold different values, so a reset or scrub always writes.
    bool ScrubLastItem( double& InOutValue, const NumberSpec& InSpec, float InPixelsForSpan, bool InMixed = false );

    // The value box at the current cursor, CalcItemWidth() wide. Returns true when it changed.
    bool UnitField( const char* InId, double& InOutValue, const NumberSpec& InSpec );

    // A whole inspector row: HavanaUtils::Label + scrubbable label + UnitField.
    bool UnitRow( const char* InLabel, double& InOutValue, const NumberSpec& InSpec );

    // Scrub math, exposed for tests: the value after moving InPixels from InRaw (the unsnapped
    // value the gesture accumulates), with modifier speeds already folded into InPixels.
    double ScrubValue( double InRaw, double InPixels, const NumberSpec& InSpec, float InPixelsForSpan );
    // Clamps to the hard range (and any clamp) and rounds integers.
    double Constrain( double InValue, const NumberSpec& InSpec );
    // Where the fill bar ends, 0..1 (log fields fill in log space).
    double FillFraction( double InValue, const NumberSpec& InSpec );
}
