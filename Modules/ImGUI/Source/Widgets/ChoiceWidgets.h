#pragma once
#include <imgui.h>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// Choice widgets (UX-13): a few named values shown at once instead of hidden in a combo. A
// segmented control for a one-of-N choice (an enum with up to five values): one segment per
// value across the value column, the chosen one under a pill that springs there through Juice
// (static at Juice Off or with reduce motion). Toggle chips for a mask: one chip per bit, on or
// off, wrapping onto more lines as needed. Both are real ImGui items: the whole control is an item reporting what
// the row shows ("Dynamic", "Everything", "Mixed"), so a row query ("Rigidbody/Body Type") reads
// the value, and each segment or chip is an item named by its value under the row
// ("Rigidbody/Body Type/Dynamic", reporting "selected" / "unselected", or "on" / "off").
// Callers check Fits() first and fall back to a combo when the column is too narrow.
namespace MeWidgets
{
    // The decisions behind the widgets. Pure: no ImGui, unit tested.
    namespace ChoiceLogic
    {
        constexpr int kMaxSegments = 5;
        // Masks with up to this many bits get chips; more (Area Mask's 16, Include Layers' 32)
        // would stack into a tall wall of chips, so they keep the combo.
        constexpr int kMaxChips = 8;
        // A segment or chip shows at least this many characters of its name (plus a "."), or the
        // whole name when it is shorter; otherwise the control doesn't fit.
        constexpr int kMinChars = 3;

        enum class Look
        {
            Segmented,
            Chips,
            Combo,
        };
        // How InCount named values (or mask bits) are shown, before the width is known.
        Look PickLook( int InCount, bool InMask );

        using Measure = std::function<float( std::string_view )>;
        // The width a name needs at the least: the whole name, or its first kMinChars characters
        // and a ".", whichever is narrower.
        float MinLabelWidth( std::string_view InName, const Measure& InMeasure );
        // The width of each of InCount equal cells across InWidth with InGap between them.
        float CellWidth( float InWidth, int InCount, float InGap );
        // Whether InCount cells across InWidth (InGap apart, InPadding inside each side) each have
        // room for their name's MinLabelWidth (or InIconWidth, when that cell has an icon), and
        // no two names read the same once shortened to the room.
        bool CellsFit( float InWidth, float InGap, float InPadding, const std::string_view* InNames, const bool* InHasIcon, int InCount, float InIconWidth,
            const Measure& InMeasure );
        // The name as it fits in InMaxWidth: whole, or the longest prefix (trailing spaces dropped)
        // with a "." after it. At least one character.
        std::string ShortLabel( std::string_view InName, float InMaxWidth, const Measure& InMeasure );
        // The choice an arrow key moves to: one step (InStep -1 or +1), stopping at the ends. From
        // no choice (-1: mixed) Right picks the first, Left the last.
        int StepIndex( int InIndex, int InCount, int InStep );

        // Mask bits: flip one, or set / clear every bit in InChoices (other bits kept).
        uint64_t ToggleBit( uint64_t InMask, int InBit );
        uint64_t SetAll( uint64_t InMask, uint64_t InChoices, bool InOn );
        // What a mask row shows: "Everything" (every bit of InChoices set), "Nothing", the one
        // set bit's name, or "Mixed (n)". InNames is indexed by bit.
        std::string MaskSummary( uint64_t InMask, uint64_t InChoices, const std::string* InNames, int InCount );
        // How many chips go on a line: as many as fit with every name whole (chips wrap onto
        // further lines rather than shorten). 0 when the widest name doesn't fit even alone.
        int ChipColumns( float InWidth, float InGap, float InPadding, const std::string_view* InNames, int InCount, const Measure& InMeasure );
    }

    // Draws an icon (by name) into the InSize square at InMin: the editor installs EditorIcons.
    using IconDrawer = void ( * )( ImDrawList* InDrawList, const char* InIcon, const ImVec2& InMin, float InSize, ImU32 InColor );
    void SetIconDrawer( IconDrawer InDrawer );

    // Whether a segmented control of InCount values fits in InWidth (CalcItemWidth() when < 0).
    // InIcons may be null, or hold null for values without an icon.
    bool SegmentedFits( const char* const* InLabels, const char* const* InIcons, int InCount, float InWidth = -1.f );
    // A segmented control across CalcItemWidth(). InOutIndex is the chosen value (-1: none).
    // InMixed (selected entities differ): no pill, every segment unselected, a "Mixed" hint;
    // clicking any segment chooses it. Left / Right step the choice while a segment has keyboard
    // focus. InTooltip (may be null) heads each segment's tooltip. Returns true when a segment
    // was chosen (one edit per click or key press).
    bool Segmented( const char* InId, const char* const* InLabels, const char* const* InIcons, int InCount, int& InOutIndex, bool InMixed = false,
        const char* InTooltip = nullptr );

    // Whether InCount chips fit (each name whole, on as many lines as needed) in InWidth
    // (CalcItemWidth() when < 0).
    bool ChipsFit( const char* const* InLabels, int InCount, float InWidth = -1.f );
    // Toggle chips across CalcItemWidth(), one per bit: InBits[i] is chip i's bit. Click a chip to
    // flip its bit; right-click for Everything / Nothing (every bit in InChoices). The control
    // reports ChoiceLogic::MaskSummary. Returns true when the mask changed.
    bool MaskChips( const char* InId, const char* const* InLabels, const int* InBits, int InCount, uint64_t& InOutMask, uint64_t InChoices,
        bool InMixed = false, const char* InTooltip = nullptr );
}
