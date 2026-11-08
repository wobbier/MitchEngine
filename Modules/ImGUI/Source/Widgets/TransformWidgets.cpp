#include "TransformWidgets.h"
#include "ItemLog.h"
#include "Juice.h"
#include "Units.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace MeWidgets
{
    namespace TransformMath
    {
        namespace
        {
            constexpr float kPi = 3.14159265358979f;
        }

        float WrapDegrees( float InDegrees )
        {
            if( !std::isfinite( InDegrees ) )
            {
                return 0.f;
            }
            float wrapped = std::fmod( InDegrees + 180.f, 360.f );
            if( wrapped < 0.f )
            {
                wrapped += 360.f;
            }
            wrapped -= 180.f;
            if( wrapped <= -180.f )
            {
                wrapped = 180.f;
            }
            // No "-0".
            return wrapped == 0.f ? 0.f : wrapped;
        }

        float DialDegrees( float InDx, float InDy )
        {
            if( InDx == 0.f && InDy == 0.f )
            {
                return 0.f;
            }
            return WrapDegrees( std::atan2( -InDy, InDx ) * 180.f / kPi );
        }

        float SnapDegrees( float InDegrees, float InStep )
        {
            if( !( InStep > 0.f ) )
            {
                return WrapDegrees( InDegrees );
            }
            return WrapDegrees( std::round( InDegrees / InStep ) * InStep );
        }

        bool ProportionalScale( const float* InBefore, float* InOutValues, int InCount, int InAxis )
        {
            if( InAxis < 0 || InAxis >= InCount )
            {
                return false;
            }
            const float old = InBefore[InAxis];
            const float now = InOutValues[InAxis];
            const bool fromZero = std::abs( old ) < 1e-6f;
            for( int i = 0; i < InCount; ++i )
            {
                if( i == InAxis )
                {
                    continue;
                }
                InOutValues[i] = fromZero ? InBefore[i] + ( now - old ) : InBefore[i] * ( now / old );
            }
            return true;
        }

        int ChangedAxis( const float* InBefore, const float* InValues, int InCount )
        {
            for( int i = 0; i < InCount; ++i )
            {
                if( InBefore[i] != InValues[i] )
                {
                    return i;
                }
            }
            return -1;
        }

        bool ScaleLockEdit( ScaleLockGesture& InOutGesture, const float* InBefore, float* InOutValues, int InCount )
        {
            const int count = std::clamp( InCount, 1, 4 );
            const int axis = ChangedAxis( InBefore, InOutValues, count );
            if( axis < 0 )
            {
                return false;
            }
            if( !InOutGesture.Held )
            {
                std::copy( InBefore, InBefore + count, InOutGesture.Start );
                InOutGesture.Held = true;
            }
            return ProportionalScale( InOutGesture.Start, InOutValues, count, axis );
        }

        std::string FormatVector( const float* InValues, int InCount )
        {
            std::string text = "[";
            for( int i = 0; i < InCount; ++i )
            {
                char number[32];
                const float value = InValues[i] == 0.f ? 0.f : InValues[i];
                std::snprintf( number, sizeof( number ), "%.6g", static_cast<double>( value ) );
                text += ( i > 0 ? ", " : "" );
                text += std::string( number ) == "-0" ? "0" : number;
            }
            return text + "]";
        }

        bool ParseVector( std::string_view InText, float* OutValues, int InCount, std::string* OutError )
        {
            auto fail = [&]( const std::string& InWhy ) {
                if( OutError )
                {
                    *OutError = InWhy;
                }
                return false;
            };
            // Brackets, commas and semicolons separate numbers like spaces do.
            std::string text( InText );
            for( char& c : text )
            {
                if( c == '[' || c == ']' || c == '(' || c == ')' || c == '{' || c == '}' || c == ',' || c == ';' || std::isspace( static_cast<unsigned char>( c ) ) )
                {
                    c = ' ';
                }
            }
            float values[16] = {};
            int found = 0;
            const char* cursor = text.c_str();
            while( true )
            {
                while( *cursor == ' ' )
                {
                    ++cursor;
                }
                if( *cursor == '\0' )
                {
                    break;
                }
                char* end = nullptr;
                const double number = std::strtod( cursor, &end );
                if( end == cursor || ( *end != ' ' && *end != '\0' ) )
                {
                    const char* tokenEnd = cursor;
                    while( *tokenEnd != ' ' && *tokenEnd != '\0' )
                    {
                        ++tokenEnd;
                    }
                    return fail( "'" + std::string( cursor, tokenEnd ) + "' isn't a number" );
                }
                if( !std::isfinite( number ) )
                {
                    return fail( "numbers must be finite" );
                }
                if( found < 16 )
                {
                    values[found] = static_cast<float>( number );
                }
                ++found;
                cursor = end;
            }
            if( found == 0 )
            {
                return fail( "no numbers to paste" );
            }
            if( found != InCount )
            {
                return fail( "expected " + std::to_string( InCount ) + " numbers, found " + std::to_string( found ) );
            }
            std::copy( values, values + InCount, OutValues );
            return true;
        }
    }

    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        const char* const kAxisNames[4] = { "X", "Y", "Z", "W" };

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 U32( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

        bool IsLightTheme()
        {
            return ImGuiTheme::Luminance( ImGuiTheme::ActivePalette().Window ) > 0.5f;
        }

        // Axis colours as marks on the panel: darkened a little on the light theme.
        ImVec4 AxisInk( int InAxis )
        {
            const ImVec4 axis = ImGui::ColorConvertU32ToFloat4( ImGuiTheme::AxisColor( InAxis ) );
            return IsLightTheme() ? ImGuiTheme::Mix( axis, ImGuiTheme::ActivePalette().Text, 0.3f ) : axis;
        }

        void ChainGlyph( ImDrawList* InDrawList, ImVec2 InCenter, float InSize, bool InLinked, ImU32 InColor )
        {
            const float thickness = Thin() * 1.5f;
            const float w = std::round( InSize * 0.38f );
            const float h = std::round( InSize * ( InLinked ? 0.5f : 0.4f ) );
            // Linked: the links overlap. Broken: pulled apart with a crack between them.
            const float offset = InLinked ? InSize * 0.2f : InSize * 0.3f;
            for( int side = -1; side <= 1; side += 2 )
            {
                const float cy = InCenter.y + side * offset;
                const ImVec2 min( std::round( InCenter.x - w * 0.5f ), std::round( cy - h * 0.5f ) );
                InDrawList->AddRect( min, ImVec2( min.x + w, min.y + h ), InColor, w * 0.5f, 0, thickness );
            }
            if( !InLinked )
            {
                const float crack = InSize * 0.16f;
                InDrawList->AddLine( ImVec2( InCenter.x - w * 0.5f - crack, InCenter.y - crack * 0.5f ), ImVec2( InCenter.x - w * 0.5f, InCenter.y ), InColor, Thin() );
                InDrawList->AddLine( ImVec2( InCenter.x + w * 0.5f + crack, InCenter.y + crack * 0.5f ), ImVec2( InCenter.x + w * 0.5f, InCenter.y ), InColor, Thin() );
            }
        }

        void DialGlyph( ImDrawList* InDrawList, ImVec2 InCenter, float InSize, ImU32 InColor )
        {
            const float radius = InSize * 0.36f;
            InDrawList->AddCircle( InCenter, radius, InColor, 20, Thin() * 1.5f );
            const float angle = -kPi * 0.25f;
            InDrawList->AddLine( InCenter, ImVec2( InCenter.x + std::cos( angle ) * radius * 0.85f, InCenter.y + std::sin( angle ) * radius * 0.85f ), InColor, Thin() * 1.5f );
            InDrawList->AddCircleFilled( InCenter, Thin() * 1.5f, InColor, 8 );
        }
    }

    bool GlyphToggle( const char* InLabel, bool& InOutOn, ToggleGlyph InGlyph, const char* InTooltipOn, const char* InTooltipOff )
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if( window->SkipItems )
        {
            return false;
        }
        ImGuiContext& g = *GImGui;
        const float size = ImGui::GetFrameHeight();
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect rect( pos, ImVec2( pos.x + size, pos.y + size ) );
        const ImGuiID id = window->GetID( InLabel );
        ImGui::ItemSize( rect, g.Style.FramePadding.y );
        bool hovered = false;
        bool held = false;
        bool pressed = false;
        if( ImGui::ItemAdd( rect, id ) )
        {
            pressed = ImGui::ButtonBehavior( rect, id, &hovered, &held );
        }
        IMGUI_TEST_ENGINE_ITEM_INFO( id, InLabel, g.LastItemData.StatusFlags );
        if( pressed )
        {
            InOutOn = !InOutOn;
            ImGui::MarkItemEdited( id );
            Juice::Flash( id );
            Juice::Play( Juice::Cue::Tick );
        }
        const bool on = InOutOn;
        ItemLog::SetItemValue( id, InGlyph == ToggleGlyph::Chain ? ( on ? "locked" : "unlocked" ) : ( on ? "open" : "closed" ) );
        if( hovered )
        {
            ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
            const char* tooltip = on ? InTooltipOn : InTooltipOff;
            if( tooltip && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            {
                ImGui::SetTooltip( "%s", tooltip );
            }
        }
        if( !ImGui::IsRectVisible( rect.Min, rect.Max ) )
        {
            return pressed;
        }
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float rounding = g.Style.FrameRounding;
        const ImVec4 background = on ? ( held ? palette.Pressed : hovered ? palette.Hovered : palette.Accent )
                                     : ( held ? palette.ControlPressed : hovered ? palette.ControlHovered : palette.Control );
        window->DrawList->AddRectFilled( rect.Min, rect.Max, U32( background ), rounding );
        if( !on )
        {
            window->DrawList->AddRect( rect.Min, rect.Max, U32( palette.Separator ), rounding, 0, Thin() );
        }
        const ImU32 ink = U32( on ? palette.Text : palette.Muted );
        if( InGlyph == ToggleGlyph::Chain )
        {
            ChainGlyph( window->DrawList, rect.GetCenter(), size, on, ink );
        }
        else
        {
            DialGlyph( window->DrawList, rect.GetCenter(), size, ink );
        }
        const float flash = Juice::FlashAmount( id );
        if( flash > 0.f )
        {
            window->DrawList->AddRect( rect.Min, rect.Max, U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), rounding, 0, Thin() * 2.f );
        }
        return pressed;
    }

    bool LockedAxes( const char* InId, float* InOutValues, int InCount, float InSpeed, const char* InFormat, bool& InOutLocked, bool* OutLockToggled, bool* OutActive )
    {
        const int count = std::clamp( InCount, 1, 4 );
        const float gap = ImGuiTheme::InnerGap();
        const float width = ImGui::CalcItemWidth();
        float before[4] = {};
        std::copy( InOutValues, InOutValues + count, before );
        // The lock scales from the values at the start of the gesture, kept per row while it is held.
        static std::unordered_map<ImGuiID, TransformMath::ScaleLockGesture> gestures;
        const ImGuiID rowId = ImGui::GetID( InId );
        bool active = false;
        ImGui::SetNextItemWidth( std::max( width - ImGui::GetFrameHeight() - gap, 1.f ) );
        bool changed = HavanaUtils::DragAxes( InId, InOutValues, count, InSpeed, InFormat, &active );
        if( OutActive && active )
        {
            *OutActive = true;
        }
        if( changed && InOutLocked )
        {
            TransformMath::ScaleLockEdit( gestures[rowId], before, InOutValues, count );
        }
        if( !active || !InOutLocked )
        {
            gestures.erase( rowId );
        }
        ImGui::SameLine( 0.f, gap );
        ImGui::PushID( InId );
        const bool toggled = GlyphToggle( "Lock", InOutLocked, ToggleGlyph::Chain,
            "Uniform scale: editing one axis scales the others in proportion. Click to edit axes on their own.",
            "Axes edit on their own. Click to lock them together (uniform scale)." );
        ImGui::PopID();
        if( OutLockToggled )
        {
            *OutLockToggled = toggled;
        }
        return changed;
    }

    bool AngleDials( const char* InCaption, float* InOutDegrees, int InCount, bool* OutActive )
    {
        const int count = std::clamp( InCount, 1, 4 );
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        ImDrawList* drawList = window->DrawList;
        const float gap = ImGuiTheme::InnerGap();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float slot = std::max( ( width - gap * static_cast<float>( count - 1 ) ) / static_cast<float>( count ), 1.f );
        // The dial leaves room beside it for "Y -180°".
        const float textRoom = ImGui::CalcTextSize( "Y -180\xC2\xB0" ).x + ImGuiTheme::Em( 0.55f );
        const float diameter = std::round( std::clamp( slot - textRoom, ImGui::GetFrameHeight(), ImGuiTheme::Em( 2.4f ) ) );
        const float radius = diameter * 0.5f;
        bool changed = false;

        for( int i = 0; i < count; ++i )
        {
            const float left = start.x + static_cast<float>( i ) * ( slot + gap );
            const ImRect rect( ImVec2( left, start.y ), ImVec2( left + diameter, start.y + diameter ) );
            const ImVec2 center = rect.GetCenter();
            const ImGuiID id = window->GetID( kAxisNames[i] );
            const bool added = ImGui::ItemAdd( rect, id );
            IMGUI_TEST_ENGINE_ITEM_INFO( id, kAxisNames[i], g.LastItemData.StatusFlags );
            ItemLog::SetLastItemRow( InCaption );
            float& value = InOutDegrees[i];
            const bool hovered = added && ImGui::ItemHoverable( rect, id, g.LastItemData.InFlags );
            bool point = false;
            if( hovered )
            {
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
                if( ImGui::IsMouseClicked( ImGuiMouseButton_Left, id ) )
                {
                    ImGui::SetActiveID( id, window );
                    ImGui::SetFocusID( id, window );
                    ImGui::FocusWindow( window );
                    ImGui::SetKeyOwner( ImGuiKey_MouseLeft, id );
                    if( g.IO.MouseClickedCount[ImGuiMouseButton_Left] == 2 )
                    {
                        if( value != 0.f )
                        {
                            value = 0.f;
                            changed = true;
                            ImGui::MarkItemEdited( id );
                            Juice::Flash( id );
                            Juice::Play( Juice::Cue::Reset );
                        }
                    }
                    else
                    {
                        point = true;
                    }
                }
                else if( !ImGui::IsItemActive() && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
                {
                    ImGui::SetTooltip( "%s: drag around the dial to point it (Shift tenths, Alt 15\xC2\xB0 steps)\nDouble-click for 0\xC2\xB0", kAxisNames[i] );
                }
            }
            const bool active = g.ActiveId == id;
            if( active )
            {
                if( g.IO.MouseDown[ImGuiMouseButton_Left] )
                {
                    point = point || g.IO.MouseDelta.x != 0.f || g.IO.MouseDelta.y != 0.f;
                    if( OutActive )
                    {
                        *OutActive = true;
                    }
                }
                else
                {
                    ImGui::ClearActiveID();
                    point = false;
                }
            }
            if( point && active )
            {
                const float dx = g.IO.MousePos.x - center.x;
                const float dy = g.IO.MousePos.y - center.y;
                // The centre has no direction.
                if( dx * dx + dy * dy > radius * radius * 0.09f )
                {
                    const float step = g.IO.KeyShift ? 0.1f : g.IO.KeyAlt ? 15.f : 1.f;
                    const float next = TransformMath::SnapDegrees( TransformMath::DialDegrees( dx, dy ), step );
                    if( next != value )
                    {
                        if( g.IO.KeyAlt )
                        {
                            Juice::Tick( id, g.IO.MousePos );
                        }
                        value = next;
                        changed = true;
                        ImGui::MarkItemEdited( id );
                    }
                }
            }
            // Euler angles from a quaternion may come back as -180 or 200: show them wrapped.
            const float wrapped = TransformMath::WrapDegrees( value );
            const float shown = std::abs( wrapped ) < 0.05f ? 0.f : wrapped;
            const std::string text = FormatValue( Reflection::Unit::Degrees, shown );
            ItemLog::SetItemValue( id, text );

            if( !ImGui::IsRectVisible( rect.Min, ImVec2( left + slot, rect.Max.y ) ) )
            {
                continue;
            }
            // Face, ticks every 45°, the arc from 0° to the value, the needle.
            const ImVec4 axis = AxisInk( i );
            drawList->AddCircleFilled( center, radius, U32( active ? palette.ControlPressed : hovered ? palette.ControlHovered : palette.Control ), 32 );
            drawList->AddCircle( center, radius, U32( active || hovered ? axis : palette.Separator ), 32, Thin() );
            for( int t = 0; t < 8; ++t )
            {
                const float a = static_cast<float>( t ) * kPi * 0.25f;
                const float inner = radius * ( t % 2 == 0 ? 0.74f : 0.84f );
                drawList->AddLine( ImVec2( center.x + std::cos( a ) * inner, center.y - std::sin( a ) * inner ),
                    ImVec2( center.x + std::cos( a ) * radius * 0.94f, center.y - std::sin( a ) * radius * 0.94f ), U32( ImGuiTheme::WithAlpha( palette.Muted, 0.8f ) ), Thin() );
            }
            const float radians = shown * kPi / 180.f;
            if( std::abs( radians ) > 1e-4f )
            {
                drawList->PathArcTo( center, radius * 0.56f, 0.f, -radians, std::max( 4, static_cast<int>( std::abs( shown ) / 10.f ) + 2 ) );
                drawList->PathStroke( U32( ImGuiTheme::WithAlpha( axis, 0.6f ) ), 0, Thin() * 2.5f );
            }
            const ImVec2 tip( center.x + std::cos( radians ) * radius * 0.82f, center.y - std::sin( radians ) * radius * 0.82f );
            drawList->AddLine( center, tip, U32( axis ), Thin() * 2.f );
            drawList->AddCircleFilled( tip, Thin() * 2.5f, U32( axis ), 10 );
            drawList->AddCircleFilled( center, Thin() * 2.f, U32( palette.Text ), 10 );
            const float flash = Juice::FlashAmount( id );
            if( flash > 0.f )
            {
                drawList->AddCircle( center, radius, U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), 32, Thin() * 2.f );
            }
            const float tick = Juice::TickAmount( id );
            if( tick > 0.f )
            {
                drawList->AddCircle( center, radius + Thin() * 2.f, U32( ImGuiTheme::WithAlpha( axis, tick ) ), 32, Thin() * 1.5f );
            }
            // "X 45°" beside the dial.
            const ImVec2 letterSize = ImGui::CalcTextSize( kAxisNames[i] );
            const float textY = std::round( center.y - letterSize.y * 0.5f );
            const float textX = rect.Max.x + ImGuiTheme::Em( 0.3f );
            drawList->AddText( ImVec2( textX, textY ), U32( axis ), kAxisNames[i] );
            drawList->AddText( ImVec2( textX + letterSize.x + ImGuiTheme::Em( 0.25f ), textY ), U32( palette.Text ), text.c_str() );
        }
        ImGui::ItemSize( ImRect( start, ImVec2( start.x + width, start.y + diameter ) ) );
        ImGui::PopID();
        return changed;
    }

    bool SpringPill( const char* InId, const char* const* InLabels, const char* const* InTooltips, int InCount, int& InOutIndex )
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if( window->SkipItems || InCount <= 0 )
        {
            return false;
        }
        ImGui::PushID( InId );
        // In a menu bar items already flow in a row (SameLine there drops to a hidden line).
        const bool horizontal = window->DC.LayoutType == ImGuiLayoutType_Horizontal;
        ImDrawList* drawList = window->DrawList;
        ImDrawListSplitter splitter;
        splitter.Split( drawList, 2 );
        splitter.SetCurrentChannel( drawList, 1 );
        bool pressed = false;
        ImRect first;
        ImRect last;
        ImRect current;
        const ImVec4 clear( 0.f, 0.f, 0.f, 0.f );
        for( int i = 0; i < InCount; ++i )
        {
            if( i > 0 && !horizontal )
            {
                ImGui::SameLine( 0.f, ImGuiTheme::InnerGap() );
            }
            const bool active = i == InOutIndex;
            ImGui::PushStyleColor( ImGuiCol_Button, clear );
            if( active )
            {
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, clear );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, clear );
            }
            if( ImGui::Button( InLabels[i] ) && !active )
            {
                InOutIndex = i;
                pressed = true;
            }
            ImGui::PopStyleColor( active ? 3 : 1 );
            if( InTooltips && InTooltips[i] )
            {
                ImGui::SetItemTooltip( "%s", InTooltips[i] );
            }
            ItemLog::SetLastItemValue( active ? "active" : "inactive" );
            const ImRect rect( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
            if( i == 0 )
            {
                first = rect;
            }
            if( i == InCount - 1 )
            {
                last = rect;
            }
            if( active )
            {
                current = rect;
            }
        }
        // Behind the buttons: a track spanning them all, and the pill under the active one,
        // springing there from wherever it was (relative to the track, so moving the window
        // doesn't animate it).
        splitter.SetCurrentChannel( drawList, 0 );
        const float rounding = ImGui::GetStyle().FrameRounding;
        drawList->AddRectFilled( first.Min, ImVec2( last.Max.x, first.Max.y ), ImGui::GetColorU32( ImGuiCol_Button ), rounding );
        if( InOutIndex >= 0 && InOutIndex < InCount )
        {
            const float x = Juice::Spring( ImGui::GetID( "##pillx" ), current.Min.x - first.Min.x, 6.f );
            const float w = Juice::Spring( ImGui::GetID( "##pillw" ), current.GetWidth(), 6.f );
            const ImVec2 min( first.Min.x + x, first.Min.y );
            drawList->AddRectFilled( min, ImVec2( min.x + std::max( w, 1.f ), first.Max.y ), ImGui::GetColorU32( ImGuiCol_HeaderActive ), rounding );
        }
        splitter.Merge( drawList );
        ImGui::PopID();
        return pressed;
    }
}
