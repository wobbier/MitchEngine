#pragma once
#include <imgui.h>
#include <string>
#include <string_view>

// Transform widgets (UX-11): the pieces the inspector's Position / Rotation / Scale rows are made
// of, usable on any vector. Every interactive part is a real ImGui item (active while dragged,
// edited when it changes a value), so the inspector turns each gesture into one undo step and
// automation finds the parts by name: "Transform/Scale/Lock", "Transform/Dials/Y",
// "World View/Rotate". The axis scrub handles themselves live in HavanaUtils::DragAxes.
namespace MeWidgets
{
    // The math behind the widgets. Pure: no ImGui, unit tested.
    namespace TransformMath
    {
        // An angle in degrees wrapped into (-180, 180]. Non-finite input gives 0.
        float WrapDegrees( float InDegrees );
        // The dial's angle for a pointer InDx, InDy pixels from its centre (screen space, y down):
        // 0° points right, angles grow counter-clockwise (90° straight up), wrapped to (-180, 180].
        float DialDegrees( float InDx, float InDy );
        // Rounds to a multiple of InStep (InStep <= 0: unchanged), then wraps.
        float SnapDegrees( float InDegrees, float InStep );

        // A uniform-scale lock: InOutValues had InBefore until axis InAxis was edited. The other
        // axes follow in proportion (new / old) so the ratios hold. When the edited axis was 0 (no
        // ratio to keep) the others move by the same difference instead. Returns false (nothing
        // changed) for a bad axis.
        bool ProportionalScale( const float* InBefore, float* InOutValues, int InCount, int InAxis );
        // The first axis whose value differs from InBefore, or -1.
        int ChangedAxis( const float* InBefore, const float* InValues, int InCount );

        // One locked-scale gesture (a drag, a handle scrub or typed entry): the values when it
        // started, so every frame of it scales from those. The ratios then survive an axis passing
        // through 0 (typing "0.5" goes through "0") and don't drift with per-frame rounding.
        struct ScaleLockGesture
        {
            float Start[4] = {};
            bool Held = false;
        };
        // One edited frame of a gesture: InOutValues held InBefore until this frame's edit. The
        // axis that changed this frame drives the others, scaled from the gesture's start (taken
        // from InBefore on its first edit). Returns false when no axis changed. Clear Held when
        // the gesture ends.
        bool ScaleLockEdit( ScaleLockGesture& InOutGesture, const float* InBefore, float* InOutValues, int InCount );

        // A vector as the row copies it: "[1, 2.5, -3]" (six significant digits, no "-0").
        std::string FormatVector( const float* InValues, int InCount );
        // Pasted text: "[1, 2, 3]", "1, 2, 3", "1 2 3", "(1; 2; 3)" or a JSON array. Exactly
        // InCount finite numbers, or false (OutValues untouched) with OutError saying why.
        bool ParseVector( std::string_view InText, float* OutValues, int InCount, std::string* OutError = nullptr );
    }

    // A square toggle the height of a frame drawn with a glyph: a chain (linked when on, broken
    // when off) or a dial. Its item is named InLabel and reports "locked" / "unlocked" (chain) or
    // "open" / "closed" (dial). Returns true when clicked (InOutOn flipped).
    enum class ToggleGlyph
    {
        Chain,
        Dial,
    };
    bool GlyphToggle( const char* InLabel, bool& InOutOn, ToggleGlyph InGlyph, const char* InTooltipOn, const char* InTooltipOff );

    // HavanaUtils::DragAxes with a uniform-scale lock at the end of the row ("Lock"): while it is
    // on, editing one axis (a drag, an axis scrub or typed entry) scales the others in proportion
    // (TransformMath::ProportionalScale). Fills CalcItemWidth(). Returns true when a value
    // changed; OutLockToggled says the lock was clicked.
    bool LockedAxes( const char* InId, float* InOutValues, int InCount, float InSpeed, const char* InFormat, bool& InOutLocked, bool* OutLockToggled = nullptr, bool* OutActive = nullptr );

    // A row of angle dials (InCount, at most 4, named X Y Z W and coloured by axis), each with
    // its value beside it. Press or drag around a dial to point it: 0° right, counter-clockwise
    // positive, wrapped at ±180. Whole degrees; Shift for tenths; Alt snaps to 15° (with a tick).
    // The centre (under a third of the radius) has no direction, so a press there changes
    // nothing; double-click resets the axis to 0. Each dial is an item
    // under InCaption's row ("Dials/Y") reporting "45°". OutActive: a dial is being dragged.
    bool AngleDials( const char* InCaption, float* InOutDegrees, int InCount, bool* OutActive = nullptr );

    // One-of-N toggle buttons with a highlight pill that springs (through Juice) to the active
    // one: static at Juice Off or with reduce motion. Works in a menu bar or a normal row. Each
    // button is an item named by its label, reporting "active" / "inactive". InTooltips may be
    // null. Returns true when a button was clicked (InOutIndex set to it).
    bool SpringPill( const char* InId, const char* const* InLabels, const char* const* InTooltips, int InCount, int& InOutIndex );
}
