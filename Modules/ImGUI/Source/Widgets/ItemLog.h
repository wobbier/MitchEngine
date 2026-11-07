#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <string_view>
#include <vector>

// Every ImGui item drawn last frame, with its rect, its visible label, the inspector row it sits
// in and the section above it, so editor automation (imgui-click/drag/scrub/type) can find a
// widget by what a person sees. Built on ImGui's test-engine hooks (IMGUI_ENABLE_TEST_ENGINE is
// defined for the ImGui library); nothing is recorded until Enable(true), so normal sessions pay
// nothing.
//
// A query names an item the way the inspector shows it:
//   Intensity                   the first item whose label or row is "Intensity"
//   Light/Intensity             ...whose window, section header or row is "Light" (any number of scopes)
//   Transform/Position/Y        the Y axis of the Position row
//   Light/Intensity:label       the row's label itself (where scrubbing starts)
//   Transform/Position@2        the second match (1-based), e.g. the Y axis of a vector row
//   0x1A2B3C4D                  an ImGui ID
// Matching ignores case, anything after "##" in an ImGui label and a trailing unit in
// parentheses ("Position (m)" answers to "Position").
namespace MeWidgets::ItemLog
{
    struct Item
    {
        ImGuiID Id = 0;
        ImRect Rect;
        std::string Label;      // visible part of the ImGui label (or the row text for a row label)
        std::string Row;        // the HavanaUtils::Label row this item shares a line with
        std::string Group;      // the inspector section (component) it belongs to, if any
        std::string Section;    // the closest tree node / collapsing header drawn above it
        std::string Window;     // root window name (the panel)
        ImRect ClipRect;        // the window's clip rect when it was drawn
        int TreeDepth = 0;      // tree depth when it was added
        ImGuiItemStatusFlags Flags = 0;
        std::string Value;      // what the widget displays, for widgets that report it
        bool IsRowLabel = false;
        bool IsWindow = false;

        // Whether a pointer at the item's center would land on it (not scrolled or clipped away).
        bool IsPointable() const { return ClipRect.Contains( Rect.GetCenter() ); }
    };

    void Enable( bool InEnabled );
    bool IsEnabled();

    // Called by HavanaUtils::Label right after its ItemAdd: the item is a row label, and items on
    // the same line inherit the row name.
    void MarkRowLabel( const char* InText );
    // A widget reports the value it displays (formatted, with units) for imgui-assert-value.
    void SetLastItemValue( std::string_view InValue );
    void SetItemValue( ImGuiID InId, std::string_view InValue );
    // The last item is a section header with a hidden label (an inspector component header):
    // items below it in the window belong to the section InName.
    void MarkSection( const char* InName );
    // Ends the current section: items drawn after it belong to no component.
    void EndSection();
    // Names an item whose ImGui label is hidden ("##axis" -> "X").
    void SetLastItemLabel( std::string_view InLabel );

    // Items from the last completed frame, in draw order.
    const std::vector<Item>& LastFrame();

    struct Query
    {
        std::vector<std::string> Scopes;
        std::string Name;
        bool WantLabel = false;
        int Index = 1;
        ImGuiID Id = 0;
    };
    Query Parse( std::string_view InQuery );
    bool Matches( const Item& InItem, const Query& InQuery );

    // The matching item from the last frame, or nullptr with OutError saying why.
    const Item* Find( std::string_view InQuery, std::string& OutError );
    std::vector<const Item*> FindAll( std::string_view InQuery );

    // Strips an ImGui label to what is shown: "Bake##3" -> "Bake", "###id" -> "".
    std::string VisibleLabel( std::string_view InLabel );

    // One line per item, for flow authors (imgui-dump).
    std::string Describe( const Item& InItem );
}
