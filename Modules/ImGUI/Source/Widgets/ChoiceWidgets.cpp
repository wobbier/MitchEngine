#include "ChoiceWidgets.h"
#include "ItemLog.h"
#include "Juice.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace MeWidgets
{
    namespace ChoiceLogic
    {
        Look PickLook( int InCount, bool InMask )
        {
            if( InCount <= 0 )
            {
                return Look::Combo;
            }
            if( InMask )
            {
                return InCount <= kMaxChips ? Look::Chips : Look::Combo;
            }
            return InCount <= kMaxSegments ? Look::Segmented : Look::Combo;
        }

        float MinLabelWidth( std::string_view InName, const Measure& InMeasure )
        {
            const float whole = InMeasure( InName );
            if( InName.size() <= static_cast<size_t>( kMinChars ) )
            {
                return whole;
            }
            std::string shortened( InName.substr( 0, kMinChars ) );
            shortened += '.';
            return std::min( whole, InMeasure( shortened ) );
        }

        float CellWidth( float InWidth, int InCount, float InGap )
        {
            if( InCount <= 0 )
            {
                return 0.f;
            }
            return std::max( ( InWidth - InGap * static_cast<float>( InCount - 1 ) ) / static_cast<float>( InCount ), 0.f );
        }

        bool CellsFit( float InWidth, float InGap, float InPadding, const std::string_view* InNames, const bool* InHasIcon, int InCount, float InIconWidth,
            const Measure& InMeasure )
        {
            if( InCount <= 0 )
            {
                return false;
            }
            const float room = CellWidth( InWidth, InCount, InGap ) - 2.f * InPadding;
            for( int i = 0; i < InCount; ++i )
            {
                const float need = InHasIcon && InHasIcon[i] ? InIconWidth : MinLabelWidth( InNames[i], InMeasure );
                if( need > room )
                {
                    return false;
                }
            }
            // Two names shortened alike ("SmoothStep" and "SmoothFocalLength" both "Smo.") would
            // be two segments that read the same: the control doesn't fit until they differ.
            std::vector<std::string> shown;
            for( int i = 0; i < InCount; ++i )
            {
                if( !( InHasIcon && InHasIcon[i] ) )
                {
                    shown.push_back( ShortLabel( InNames[i], room, InMeasure ) );
                }
            }
            std::sort( shown.begin(), shown.end() );
            return std::adjacent_find( shown.begin(), shown.end() ) == shown.end();
        }

        std::string ShortLabel( std::string_view InName, float InMaxWidth, const Measure& InMeasure )
        {
            if( InName.empty() || InMeasure( InName ) <= InMaxWidth )
            {
                return std::string( InName );
            }
            for( size_t length = InName.size() - 1; length > 1; --length )
            {
                std::string prefix( InName.substr( 0, length ) );
                while( !prefix.empty() && prefix.back() == ' ' )
                {
                    prefix.pop_back();
                }
                if( prefix.empty() )
                {
                    continue;
                }
                prefix += '.';
                if( InMeasure( prefix ) <= InMaxWidth )
                {
                    return prefix;
                }
            }
            return std::string( InName.substr( 0, 1 ) ) + ".";
        }

        int StepIndex( int InIndex, int InCount, int InStep )
        {
            if( InCount <= 0 )
            {
                return -1;
            }
            if( InIndex < 0 || InIndex >= InCount )
            {
                return InStep < 0 ? InCount - 1 : 0;
            }
            return std::clamp( InIndex + ( InStep < 0 ? -1 : InStep > 0 ? 1 : 0 ), 0, InCount - 1 );
        }

        uint64_t ToggleBit( uint64_t InMask, int InBit )
        {
            if( InBit < 0 || InBit >= 64 )
            {
                return InMask;
            }
            return InMask ^ ( 1ull << InBit );
        }

        uint64_t SetAll( uint64_t InMask, uint64_t InChoices, bool InOn )
        {
            return InOn ? ( InMask | InChoices ) : ( InMask & ~InChoices );
        }

        std::string MaskSummary( uint64_t InMask, uint64_t InChoices, const std::string* InNames, int InCount )
        {
            const uint64_t chosen = InMask & InChoices;
            if( chosen == InChoices && InChoices != 0 )
            {
                return "Everything";
            }
            int count = 0;
            int last = -1;
            for( int i = 0; i < InCount && i < 64; ++i )
            {
                if( chosen & ( 1ull << i ) )
                {
                    ++count;
                    last = i;
                }
            }
            if( count == 0 )
            {
                return "Nothing";
            }
            if( count == 1 && InNames )
            {
                return InNames[last];
            }
            return "Mixed (" + std::to_string( count ) + ")";
        }

        int ChipColumns( float InWidth, float InGap, float InPadding, const std::string_view* InNames, int InCount, const Measure& InMeasure )
        {
            if( InCount <= 0 )
            {
                return 0;
            }
            // Chips wrap instead of shortening: every name shows whole, so "Layer 1" and
            // "Layer 12" never both read "Lay.".
            float widest = 0.f;
            for( int i = 0; i < InCount; ++i )
            {
                widest = std::max( widest, InMeasure( InNames[i] ) );
            }
            for( int columns = InCount; columns >= 1; --columns )
            {
                const float room = CellWidth( InWidth, columns, InGap ) - 2.f * InPadding;
                const bool fits = widest <= room;
                if( fits )
                {
                    return columns;
                }
            }
            return 0;
        }
    }

    namespace
    {
        IconDrawer s_iconDrawer = nullptr;

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 U32( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

        // Room inside a segment or chip on each side of its text.
        float CellPadding()
        {
            return ImGuiTheme::Em( 0.3f );
        }

        ChoiceLogic::Measure TextMeasure()
        {
            return []( std::string_view InText ) { return ImGui::CalcTextSize( InText.data(), InText.data() + InText.size() ).x; };
        }

        bool HasIcon( const char* const* InIcons, int InIndex )
        {
            return s_iconDrawer && InIcons && InIcons[InIndex] && InIcons[InIndex][0];
        }

        void CellTooltip( const char* InName, const char* InState, const char* InTooltip, bool InMixed )
        {
            std::string text = InName;
            if( InState && InState[0] )
            {
                text += InState;
            }
            if( InMixed )
            {
                text += "\nMixed: the selected entities differ. Click to set them all.";
            }
            if( InTooltip && InTooltip[0] )
            {
                text += "\n\n";
                text += InTooltip;
            }
            ImGui::SetTooltip( "%s", text.c_str() );
        }

        // Left / Right while one of a control's items has keyboard focus: claimed from ImGui's
        // navigation (which would otherwise move the focus) and returned as -1 / +1.
        int ArrowStep( ImGuiID InOwner, bool InFocused )
        {
            if( !InFocused )
            {
                return 0;
            }
            ImGui::SetKeyOwner( ImGuiKey_LeftArrow, InOwner );
            ImGui::SetKeyOwner( ImGuiKey_RightArrow, InOwner );
            if( ImGui::IsKeyPressed( ImGuiKey_LeftArrow, InOwner, ImGuiInputFlags_Repeat ) )
            {
                return -1;
            }
            if( ImGui::IsKeyPressed( ImGuiKey_RightArrow, InOwner, ImGuiInputFlags_Repeat ) )
            {
                return 1;
            }
            return 0;
        }
    }

    void SetIconDrawer( IconDrawer InDrawer )
    {
        s_iconDrawer = InDrawer;
    }

    bool SegmentedFits( const char* const* InLabels, const char* const* InIcons, int InCount, float InWidth )
    {
        if( InCount <= 0 || InCount > ChoiceLogic::kMaxSegments )
        {
            return false;
        }
        const float width = InWidth < 0.f ? ImGui::CalcItemWidth() : InWidth;
        std::string_view names[ChoiceLogic::kMaxSegments];
        bool icons[ChoiceLogic::kMaxSegments] = {};
        for( int i = 0; i < InCount; ++i )
        {
            names[i] = InLabels[i] ? InLabels[i] : "";
            icons[i] = HasIcon( InIcons, i );
        }
        return ChoiceLogic::CellsFit( width, 0.f, CellPadding(), names, icons, InCount, ImGui::GetFontSize(), TextMeasure() );
    }

    bool Segmented( const char* InId, const char* const* InLabels, const char* const* InIcons, int InCount, int& InOutIndex, bool InMixed, const char* InTooltip )
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if( window->SkipItems || InCount <= 0 )
        {
            return false;
        }
        ImGuiContext& g = *GImGui;
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float width = std::max( ImGui::CalcItemWidth(), 1.f );
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect frame( pos, ImVec2( pos.x + width, pos.y + height ) );
        const ImGuiID id = window->GetID( InId );
        const bool valid = !InMixed && InOutIndex >= 0 && InOutIndex < InCount;

        // The whole control: what the row shows, for a row query. Not a navigation stop itself.
        ImGui::ItemSize( frame, g.Style.FramePadding.y );
        ImGui::PushItemFlag( ImGuiItemFlags_NoNav, true );
        ImGui::ItemAdd( frame, id );
        ImGui::PopItemFlag();
        ItemLog::SetItemValue( id, InMixed ? "Mixed" : valid ? InLabels[InOutIndex] : "" );
        const std::string row = ItemLog::ItemRow( id );

        const float cell = ChoiceLogic::CellWidth( width, InCount, 0.f );
        auto cellRect = [&]( int InIndex ) {
            return ImRect( ImVec2( std::round( pos.x + cell * InIndex ), pos.y ), ImVec2( InIndex == InCount - 1 ? frame.Max.x : std::round( pos.x + cell * ( InIndex + 1 ) ), frame.Max.y ) );
        };

        bool changed = false;
        int hoveredIndex = -1;
        int heldIndex = -1;
        bool focused = false;
        std::vector<ImGuiID> ids( InCount );
        ImGui::PushOverrideID( id );
        for( int i = 0; i < InCount; ++i )
        {
            const ImRect rect = cellRect( i );
            const ImGuiID segment = window->GetID( i );
            ids[i] = segment;
            bool hovered = false;
            bool held = false;
            bool pressed = false;
            if( ImGui::ItemAdd( rect, segment ) )
            {
                pressed = ImGui::ButtonBehavior( rect, segment, &hovered, &held );
            }
            IMGUI_TEST_ENGINE_ITEM_INFO( segment, InLabels[i], g.LastItemData.StatusFlags );
            if( !row.empty() )
            {
                ItemLog::SetLastItemRow( row );
            }
            ItemLog::MarkLastItemRowScoped();
            const bool selected = valid && i == InOutIndex;
            ItemLog::SetItemValue( segment, selected ? "selected" : "unselected" );
            if( pressed && ( InMixed || i != InOutIndex ) )
            {
                InOutIndex = i;
                changed = true;
                ImGui::MarkItemEdited( segment );
            }
            if( hovered )
            {
                hoveredIndex = i;
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
                if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
                {
                    CellTooltip( InLabels[i], selected ? " (chosen)" : nullptr, InTooltip, InMixed );
                }
            }
            if( held )
            {
                heldIndex = i;
            }
            focused = focused || ( g.NavId == segment && g.NavWindow == window && g.ActiveId == 0 );
        }
        // Left / Right step the choice while a segment has keyboard focus; the focus follows it.
        const int step = ArrowStep( id, focused );
        if( step != 0 )
        {
            const int next = ChoiceLogic::StepIndex( InMixed ? -1 : InOutIndex, InCount, step );
            if( next >= 0 && ( InMixed || next != InOutIndex ) )
            {
                InOutIndex = next;
                changed = true;
                ImGui::MarkItemEdited( ids[next] );
            }
            if( next >= 0 )
            {
                ImGui::SetFocusID( ids[next], window );
                g.NavDisableHighlight = false;
            }
        }
        ImGui::PopID();
        if( changed )
        {
            Juice::Flash( id );
            Juice::Play( Juice::Cue::Tick );
        }

        // The pill springs between segments in segment units (resizing the panel doesn't animate
        // it); a choice changed from outside (undo, another entity) jumps there.
        static std::unordered_map<ImGuiID, int> s_shown;
        const int target = InMixed ? -1 : InOutIndex;
        // Keyed by the control, so two controls in one ID scope never share a spring.
        const ImGuiID pillId = ImHashStr( "##segmentpill", 0, id );
        auto shown = s_shown.find( id );
        if( target >= 0 && ( shown == s_shown.end() || ( shown->second != target && !changed ) || shown->second < 0 ) )
        {
            Juice::Snap( pillId, static_cast<float>( target ) );
        }
        s_shown[id] = target;

        if( !ImGui::IsRectVisible( frame.Min, frame.Max ) )
        {
            return changed;
        }
        ImDrawList* drawList = window->DrawList;
        const float rounding = g.Style.FrameRounding;
        const float thin = Thin();
        drawList->AddRectFilled( frame.Min, frame.Max, ImGui::GetColorU32( ImGuiCol_FrameBg ), rounding );
        // Hover and press under the unchosen segments.
        const int chosen = InMixed ? -1 : InOutIndex;
        if( hoveredIndex >= 0 && hoveredIndex != chosen )
        {
            const ImRect rect = cellRect( hoveredIndex );
            drawList->AddRectFilled( rect.Min, rect.Max, ImGui::GetColorU32( heldIndex == hoveredIndex ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered ), rounding );
        }
        if( chosen >= 0 && chosen < InCount )
        {
            const float at = std::clamp( Juice::Spring( pillId, static_cast<float>( chosen ), 12.f ), -0.25f, static_cast<float>( InCount ) - 0.75f );
            const float inset = thin;
            const float left = std::max( pos.x + cell * at, frame.Min.x ) + inset;
            const float right = std::min( pos.x + cell * ( at + 1.f ), frame.Max.x ) - inset;
            const ImVec4 fill = heldIndex == chosen ? palette.Pressed : hoveredIndex == chosen ? palette.Hovered : palette.Accent;
            drawList->AddRectFilled( ImVec2( left, frame.Min.y + inset ), ImVec2( std::max( right, left + 1.f ), frame.Max.y - inset ), U32( fill ), std::max( rounding - inset, 0.f ) );
        }
        // Dividers between neighbouring segments that are neither chosen nor hovered.
        for( int i = 1; i < InCount; ++i )
        {
            const bool plain = i != chosen && i - 1 != chosen && i != hoveredIndex && i - 1 != hoveredIndex;
            if( plain )
            {
                const float x = cellRect( i ).Min.x;
                drawList->AddLine( ImVec2( x, frame.Min.y + height * 0.25f ), ImVec2( x, frame.Max.y - height * 0.25f ), U32( palette.Separator ), thin );
            }
        }
        const ChoiceLogic::Measure measure = TextMeasure();
        const float padding = CellPadding();
        const float iconSize = ImGui::GetFontSize();
        const float iconGap = ImGuiTheme::Em( 0.25f );
        for( int i = 0; i < InCount; ++i )
        {
            const ImRect rect = cellRect( i );
            const bool selected = i == chosen;
            const ImU32 ink = U32( selected || i == hoveredIndex ? palette.Text : ImGuiTheme::Mix( palette.Text, palette.Muted, 0.6f ) );
            const float room = rect.GetWidth() - 2.f * padding;
            const char* name = InLabels[i] ? InLabels[i] : "";
            if( HasIcon( InIcons, i ) )
            {
                // The icon, and the name beside it when that fits whole.
                const float nameWidth = measure( name );
                const bool withName = iconSize + iconGap + nameWidth <= room;
                const float content = withName ? iconSize + iconGap + nameWidth : iconSize;
                const float left = std::round( rect.GetCenter().x - content * 0.5f );
                s_iconDrawer( drawList, InIcons[i], ImVec2( left, std::round( rect.GetCenter().y - iconSize * 0.5f ) ), iconSize, ink );
                if( withName )
                {
                    drawList->AddText( ImVec2( left + iconSize + iconGap, std::round( rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f ) ), ink, name );
                }
            }
            else
            {
                const std::string text = ChoiceLogic::ShortLabel( name, room, measure );
                const float textWidth = measure( text );
                drawList->AddText( ImVec2( std::round( rect.GetCenter().x - textWidth * 0.5f ), std::round( rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f ) ), ink, text.c_str() );
            }
            ImGui::RenderNavHighlight( rect, ids[i] );
        }
        if( InMixed )
        {
            drawList->AddRect( frame.Min, frame.Max, U32( palette.Warning ), rounding, 0, thin );
        }
        const float flash = Juice::FlashAmount( id );
        if( flash > 0.f )
        {
            drawList->AddRect( frame.Min, frame.Max, U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), rounding, 0, thin * 2.f );
        }
        return changed;
    }

    bool ChipsFit( const char* const* InLabels, int InCount, float InWidth )
    {
        if( InCount <= 0 || InCount > ChoiceLogic::kMaxChips )
        {
            return false;
        }
        const float width = InWidth < 0.f ? ImGui::CalcItemWidth() : InWidth;
        std::string_view names[ChoiceLogic::kMaxChips];
        for( int i = 0; i < InCount; ++i )
        {
            names[i] = InLabels[i] ? InLabels[i] : "";
        }
        return ChoiceLogic::ChipColumns( width, ImGuiTheme::InnerGap(), CellPadding(), names, InCount, TextMeasure() ) > 0;
    }

    bool MaskChips( const char* InId, const char* const* InLabels, const int* InBits, int InCount, uint64_t& InOutMask, uint64_t InChoices, bool InMixed,
        const char* InTooltip )
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if( window->SkipItems || InCount <= 0 || InCount > ChoiceLogic::kMaxChips )
        {
            return false;
        }
        ImGuiContext& g = *GImGui;
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float width = std::max( ImGui::CalcItemWidth(), 1.f );
        const float height = ImGui::GetFrameHeight();
        const float gap = ImGuiTheme::InnerGap();
        const float padding = CellPadding();
        const ChoiceLogic::Measure measure = TextMeasure();
        std::string_view names[ChoiceLogic::kMaxChips];
        std::string bitNames[64];
        for( int i = 0; i < InCount; ++i )
        {
            names[i] = InLabels[i] ? InLabels[i] : "";
            if( InBits[i] >= 0 && InBits[i] < 64 )
            {
                bitNames[InBits[i]] = std::string( names[i] );
            }
        }
        int columns = ChoiceLogic::ChipColumns( width, gap, padding, names, InCount, measure );
        if( columns <= 0 )
        {
            columns = InCount;
        }
        const int lines = ( InCount + columns - 1 ) / columns;
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect frame( pos, ImVec2( pos.x + width, pos.y + lines * height + ( lines - 1 ) * gap ) );
        const ImGuiID id = window->GetID( InId );

        ImGui::ItemSize( frame, g.Style.FramePadding.y );
        ImGui::PushItemFlag( ImGuiItemFlags_NoNav, true );
        ImGui::ItemAdd( frame, id );
        ImGui::PopItemFlag();
        ItemLog::SetItemValue( id, InMixed ? "Mixed" : ChoiceLogic::MaskSummary( InOutMask, InChoices, bitNames, 64 ) );
        const std::string row = ItemLog::ItemRow( id );

        const float cell = ChoiceLogic::CellWidth( width, columns, gap );
        auto chipRect = [&]( int InIndex ) {
            const int line = InIndex / columns;
            const int column = InIndex % columns;
            const float left = std::round( pos.x + column * ( cell + gap ) );
            const float right = column == columns - 1 ? frame.Max.x : std::round( pos.x + column * ( cell + gap ) + cell );
            const float top = pos.y + line * ( height + gap );
            return ImRect( ImVec2( left, top ), ImVec2( right, top + height ) );
        };

        bool changed = false;
        int hoveredIndex = -1;
        int heldIndex = -1;
        bool focused = false;
        int focusedIndex = -1;
        std::vector<ImGuiID> ids( InCount );
        ImGui::PushOverrideID( id );
        for( int i = 0; i < InCount; ++i )
        {
            const ImRect rect = chipRect( i );
            const ImGuiID chip = window->GetID( i );
            ids[i] = chip;
            bool hovered = false;
            bool held = false;
            bool pressed = false;
            if( ImGui::ItemAdd( rect, chip ) )
            {
                pressed = ImGui::ButtonBehavior( rect, chip, &hovered, &held );
            }
            IMGUI_TEST_ENGINE_ITEM_INFO( chip, InLabels[i], g.LastItemData.StatusFlags );
            if( !row.empty() )
            {
                ItemLog::SetLastItemRow( row );
            }
            ItemLog::MarkLastItemRowScoped();
            const bool on = ( InOutMask >> InBits[i] ) & 1ull;
            ItemLog::SetItemValue( chip, on ? "on" : "off" );
            if( pressed )
            {
                InOutMask = ChoiceLogic::ToggleBit( InOutMask, InBits[i] );
                changed = true;
                ImGui::MarkItemEdited( chip );
                Juice::Flash( chip );
            }
            if( hovered )
            {
                hoveredIndex = i;
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
                if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
                {
                    const bool nowOn = ( InOutMask >> InBits[i] ) & 1ull;
                    CellTooltip( InLabels[i], nowOn ? ": on. Click to turn it off.\nRight-click: Everything / Nothing" : ": off. Click to turn it on.\nRight-click: Everything / Nothing",
                        InTooltip, InMixed );
                }
            }
            if( held )
            {
                heldIndex = i;
            }
            if( g.NavId == chip && g.NavWindow == window )
            {
                focused = true;
                focusedIndex = i;
            }
        }
        // Left / Right move the keyboard focus along the chips (Space toggles, as on any button).
        const int step = ArrowStep( id, focused && g.ActiveId == 0 );
        if( step != 0 && focusedIndex >= 0 )
        {
            ImGui::SetFocusID( ids[ChoiceLogic::StepIndex( focusedIndex, InCount, step )], window );
            g.NavDisableHighlight = false;
        }
        // Right-click anywhere on the chips: set or clear every bit.
        if( ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) && ImGui::IsMouseHoveringRect( frame.Min, frame.Max ) && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
        {
            ImGui::OpenPopup( "##chipsmenu" );
        }
        if( ImGui::BeginPopup( "##chipsmenu" ) )
        {
            const uint64_t all = ChoiceLogic::SetAll( InOutMask, InChoices, true );
            const uint64_t none = ChoiceLogic::SetAll( InOutMask, InChoices, false );
            if( ImGui::MenuItem( "Everything", nullptr, !InMixed && InOutMask == all ) && ( InMixed || InOutMask != all ) )
            {
                InOutMask = all;
                changed = true;
            }
            if( ImGui::MenuItem( "Nothing", nullptr, !InMixed && InOutMask == none ) && ( InMixed || InOutMask != none ) )
            {
                InOutMask = none;
                changed = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        if( changed )
        {
            Juice::Play( Juice::Cue::Tick );
        }

        if( !ImGui::IsRectVisible( frame.Min, frame.Max ) )
        {
            return changed;
        }
        ImDrawList* drawList = window->DrawList;
        const float rounding = g.Style.FrameRounding;
        const float thin = Thin();
        for( int i = 0; i < InCount; ++i )
        {
            const ImRect rect = chipRect( i );
            const bool on = ( InOutMask >> InBits[i] ) & 1ull;
            const bool hovered = i == hoveredIndex;
            const bool held = i == heldIndex;
            const ImVec4 fill = on ? ( held ? palette.Pressed : hovered ? palette.Hovered : palette.Accent )
                                   : ( held ? palette.ControlPressed : hovered ? palette.ControlHovered : palette.Control );
            drawList->AddRectFilled( rect.Min, rect.Max, U32( fill ), rounding );
            if( !on )
            {
                drawList->AddRect( rect.Min, rect.Max, U32( palette.Separator ), rounding, 0, thin );
            }
            const std::string text = ChoiceLogic::ShortLabel( names[i], rect.GetWidth() - 2.f * padding, measure );
            const float textWidth = measure( text );
            const ImU32 ink = U32( on || hovered ? palette.Text : palette.Muted );
            drawList->AddText( ImVec2( std::round( rect.GetCenter().x - textWidth * 0.5f ), std::round( rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f ) ), ink, text.c_str() );
            const float flash = Juice::FlashAmount( ids[i] );
            if( flash > 0.f )
            {
                drawList->AddRect( rect.Min, rect.Max, U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), rounding, 0, thin * 2.f );
            }
            ImGui::RenderNavHighlight( rect, ids[i] );
        }
        if( InMixed )
        {
            const float outset = std::max( thin, std::round( gap * 0.5f ) );
            drawList->AddRect( ImVec2( frame.Min.x - outset, frame.Min.y - outset ), ImVec2( frame.Max.x + outset, frame.Max.y + outset ), U32( palette.Warning ), rounding, 0, thin );
        }
        return changed;
    }
}
