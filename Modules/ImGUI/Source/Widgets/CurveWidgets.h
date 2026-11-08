#pragma once
#include "ScrubField.h"
#include <string>
#include <vector>

// The curve editor (UX-15): a value over a span (a particle's life, a distance) drawn on a Graph,
// shaped by keys. Drag a key (Shift: fine steps); double-click empty graph space to add a key
// there; press Delete while a key is selected, or right-click it, to remove it. The selected key
// shows its tangent handles ("In", "Out"): drag one to bend the curve (the key's tangents become
// Free). Right-click a key, or use the row under the graph, to set how its tangents are made:
// Auto (smooth, never overshooting a neighbour), Linear (straight to each neighbour), Flat
// (level) or Free (the handles). Presets under the graph reshape the curve between its end
// values: Linear, Ease In, Ease Out, Ease In-Out, Constant (the start value all along) and Reset.
// A constrained editor (fixed key count, keys that keep their X, locked keys, no tangents)
// serves callers whose data is a fixed curve, such as a value that goes linearly from 1 to an
// end value.
//
// Items: the graph is the caption ("Curve", reporting "(0%, 1) (100%, 0.5)"), the keys
// "Curve/Key 1".. (each reporting "100%, 0.5"), the selected key's handles "Curve/Key 2/In" and
// "Curve/Key 2/Out" (reporting their slope), its row "Curve/Key 2/X", "Curve/Key 2/Y" and the
// tangent modes "Curve/Key 2/Auto".., and the presets "Curve/Preset/Ease In", "Curve/Preset/Reset".
namespace MeWidgets
{
    namespace CurveMath
    {
        enum class Tangent : int
        {
            Auto = 0,
            Linear,
            Flat,
            Free,
        };
        const char* TangentName( Tangent InMode );

        struct Key
        {
            double X = 0.0;
            double Y = 0.0;
            // Slopes (Y units per X unit) arriving at and leaving the key.
            double In = 0.0;
            double Out = 0.0;
            Tangent Mode = Tangent::Auto;
        };

        // Recomputes the slopes of every key that isn't Free from its neighbours. Keys in X order.
        void UpdateTangents( std::vector<Key>& InOutKeys );
        // The curve at InX: cubic Hermite between neighbouring keys (with the keys' stored
        // slopes), the end keys' values held beyond them. Keys in X order; none: 0.
        double Evaluate( const std::vector<Key>& InKeys, double InX );

        enum class Preset : int
        {
            Linear = 0,
            EaseIn,
            EaseOut,
            EaseInOut,
            Constant,
            Count,
        };
        const char* PresetName( Preset InPreset );
        // Two keys from (InX0, InY0) to (InX1, InY1) shaped as the preset: Ease In is t^2, Ease
        // Out 1 - (1 - t)^2, Ease In-Out smoothstep, Constant InY0 throughout.
        std::vector<Key> MakePreset( Preset InPreset, double InX0, double InX1, double InY0, double InY1 );

        // Adds an Auto key at (InX, InY), keeping X order, and returns its index.
        int AddKey( std::vector<Key>& InOutKeys, double InX, double InY );
        // Removes the key at InIndex unless that leaves fewer than InMinKeys.
        bool RemoveKey( std::vector<Key>& InOutKeys, int InIndex, int InMinKeys );

        // "100%, 0.5" in the axes' units.
        std::string DescribeKey( const Key& InKey, Unit InXUnit, Unit InYUnit );
        // "(0%, 1) (100%, 0.5)".
        std::string Describe( const std::vector<Key>& InKeys, Unit InXUnit, Unit InYUnit );
    }

    struct CurveOptions
    {
        // The axes: units, and their ranges (HasRange; X defaults to 0..1, Y to 0..1).
        NumberSpec X;
        NumberSpec Y;
        // The Y axis shows Y.Min up to a round value a little above the highest key (held while a
        // key is dragged), instead of the whole Y range: a multiplier allowed up to 10 but
        // usually near 1.
        bool FitY = false;
        int MinKeys = 2;
        int MaxKeys = 16;
        // Keys keep their X (no add, no delete either: the count is the data's).
        bool LockX = false;
        // Bit i: key i doesn't move at all (a start value the data doesn't store).
        unsigned LockedKeys = 0;
        // false: the curve is straight between keys (the data is linear): no handles, no tangent
        // modes.
        bool Tangents = true;
        bool Presets = true;
        // The selected key's row (X, Y, tangent modes) under the graph; off when the fields it
        // would repeat are right above (a constrained curve over one reflected value).
        bool KeyRow = true;
        // What the Reset preset restores; null: Linear across the axes.
        const std::vector<CurveMath::Key>* ResetKeys = nullptr;
        // Shown when the graph is hovered, above the editing hints.
        const char* Tooltip = nullptr;
        // The graph's height in em (0: 6).
        float Height = 0.f;
    };

    // The whole editor at the cursor: a caption row with the graph, the presets, then the
    // selected key's row. Keys in X order. Returns true when the curve changed this frame.
    bool CurveEditor( const char* InCaption, std::vector<CurveMath::Key>& InOutKeys, const CurveOptions& InOptions = CurveOptions() );
    // Forgets every editor's selection (the Widget Gallery's Reset).
    void ResetCurveEditors();
}
