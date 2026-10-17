#pragma once
#include <string>
#include "Math/Vector3.h"
#include "Math/Vector2.h"

class HavanaUtils
{
    HavanaUtils() = delete;

public:
    // Property rows, the layout of every inspector and settings form: a label column, then the
    // value. Within a window every value starts at the same x whatever the indentation (tree
    // levels narrow the label instead) and ends one panel inset from the right edge, also in
    // panels that zero WindowPadding. Label() draws the label (ellipsized, full text in a
    // tooltip), moves to the value column, sets the next item's width and returns it.
    // customWidth >= 0 asks for a value that wide, right-aligned.
    static float Label( const std::string& Name, float customWidth = -1 );
    // A row with an empty label: moves to the value column (for buttons and notes that line up
    // with the values above them) and returns the value width.
    static float SkipLabel();
    // Screen-space x where the current window's values start / where rows end.
    static float ValueColumnX();
    static float RowRight();
    // Width from the cursor to the end of the row: use it instead of -1 widths, which run to
    // the window edge in panels without padding.
    static float RowWidth();

    static void Text( const std::string& Name, const Vector3& Vector );
    static void Text( const std::string& Name, const Vector2& Vector );

    // Vector components side by side, each marked with its axis color, filling the next item's
    // width, as one group (IsItemActive and friends apply to the whole vector). With a reset
    // value, right-clicking a component offers to reset it.
    static bool DragAxes( const char* InId, float* InOutValues, int InCount, float InSpeed, const char* InFormat, bool* OutActive = nullptr, const float* InResetValue = nullptr );

    static bool EditableVector3( const std::string& Name, Vector3& Vector, float ResetValue = 0.f, float customWidth = -1 );
    static bool EditableVector3Spring( const std::string& Name, Vector3& Vector, float ResetValue = 0.f, float customWidth = -1 );
    static bool EditableVector( const std::string& Name, Vector2& Vector, float ResetValue = 0.f, float customWidth = -1 );

    static bool Float( const std::string& Name, float& value );
    static bool Double( const std::string& Name, double& value );

    static bool Int( const std::string& Name, int16_t& value );
    static bool Int( const std::string& Name, int32_t& value );
    static bool Int( const std::string& Name, int64_t& value );

    static bool UInt( const std::string& Name, uint16_t& value );
    static bool UInt( const std::string& Name, uint32_t& value );
    static bool UInt( const std::string& Name, uint64_t& value );

    static void ColorButton( const std::string& Name, Vector3& arr );

    static bool ToggleButton( const std::string& InName, bool& OutPressed );
};
