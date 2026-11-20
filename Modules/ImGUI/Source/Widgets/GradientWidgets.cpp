#include "GradientWidgets.h"
#include "ColorPopover.h"
#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <unordered_map>

namespace MeWidgets
{
    namespace GradientMath
    {
        namespace
        {
            constexpr float kDetents[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };

            float Lerp( float InA, float InB, float InT )
            {
                return InA + ( InB - InA ) * InT;
            }
        }

        std::vector<int> Order( const std::vector<Stop>& InStops )
        {
            std::vector<int> order( InStops.size() );
            std::iota( order.begin(), order.end(), 0 );
            std::stable_sort( order.begin(), order.end(), [&]( int InA, int InB ) { return InStops[InA].Position < InStops[InB].Position; } );
            return order;
        }

        Sample Evaluate( const std::vector<Stop>& InStops, float InT )
        {
            Sample sample;
            if( InStops.empty() )
            {
                return sample;
            }
            const std::vector<int> order = Order( InStops );
            const Stop* before = &InStops[order.front()];
            const Stop* after = before;
            for( int index : order )
            {
                const Stop& stop = InStops[index];
                if( stop.Position <= InT )
                {
                    before = &stop;
                    after = &stop;
                }
                else
                {
                    after = &stop;
                    break;
                }
            }
            if( InT < InStops[order.front()].Position )
            {
                before = after = &InStops[order.front()];
            }
            const float span = after->Position - before->Position;
            const float t = span > 0.f ? std::clamp( ( InT - before->Position ) / span, 0.f, 1.f ) : 0.f;
            for( int c = 0; c < 3; ++c )
            {
                sample.Color[c] = Lerp( before->Color[c], after->Color[c], t );
            }
            sample.Alpha = Lerp( before->Alpha, after->Alpha, t );
            return sample;
        }

        int Sort( std::vector<Stop>& InOutStops, int InTrack )
        {
            const std::vector<int> order = Order( InOutStops );
            std::vector<Stop> sorted;
            sorted.reserve( InOutStops.size() );
            int tracked = -1;
            for( size_t i = 0; i < order.size(); ++i )
            {
                sorted.push_back( InOutStops[order[i]] );
                if( order[i] == InTrack )
                {
                    tracked = static_cast<int>( i );
                }
            }
            InOutStops.swap( sorted );
            return tracked;
        }

        int AddStop( std::vector<Stop>& InOutStops, float InT )
        {
            const float t = std::clamp( InT, 0.f, 1.f );
            const Sample sample = Evaluate( InOutStops, t );
            Stop stop;
            stop.Position = t;
            stop.Color = sample.Color;
            stop.Alpha = sample.Alpha;
            // After any stops already at that position, so an equal one keeps its place.
            size_t at = 0;
            while( at < InOutStops.size() && InOutStops[at].Position <= t )
            {
                ++at;
            }
            InOutStops.insert( InOutStops.begin() + static_cast<std::ptrdiff_t>( at ), stop );
            return static_cast<int>( at );
        }

        bool RemoveStop( std::vector<Stop>& InOutStops, int InIndex, int InMinStops )
        {
            if( InIndex < 0 || InIndex >= static_cast<int>( InOutStops.size() ) || static_cast<int>( InOutStops.size() ) <= std::max( InMinStops, 0 ) )
            {
                return false;
            }
            InOutStops.erase( InOutStops.begin() + InIndex );
            return true;
        }

        const float* Detents( int& OutCount )
        {
            OutCount = static_cast<int>( sizeof( kDetents ) / sizeof( kDetents[0] ) );
            return kDetents;
        }

        float SnapToDetent( float InT, float InReach )
        {
            for( float detent : kDetents )
            {
                if( std::abs( InT - detent ) <= InReach )
                {
                    return detent;
                }
            }
            return InT;
        }

        float RoundPosition( float InT, bool InFine )
        {
            const float steps = InFine ? 1000.f : 100.f;
            return std::clamp( std::round( InT * steps ) / steps, 0.f, 1.f );
        }

        std::string DescribeStop( const Stop& InStop )
        {
            return ColorMath::FormatHex( InStop.Color ) + " " + FormatValue( Unit::Percent, std::clamp( InStop.Alpha, 0.f, 1.f ) );
        }

        std::string Describe( const std::vector<Stop>& InStops )
        {
            std::string text;
            for( int index : Order( InStops ) )
            {
                if( !text.empty() )
                {
                    text += " -> ";
                }
                text += DescribeStop( InStops[index] );
            }
            return text;
        }
    }

    namespace
    {
        using GradientMath::Stop;

        struct GradientState
        {
            int Selected = 0;
            // Delete removes the selected stop while the editor has the keyboard (clicked last).
            bool Focused = false;
            // The stop being dragged (an index) and where it was grabbed.
            ImGuiID DragId = 0;
            float GrabOffset = 0.f;
            // Dragged off the bar: removed when the button comes up.
            bool Removing = false;
            // A stop added by a press on the bar follows the pointer until the release.
            int Placing = -1;
            // The selected stop's Position field moved it: re-sorted once that field lets go.
            bool SortPending = false;
            ImGuiID PositionId = 0;
        };

        std::unordered_map<ImGuiID, GradientState>& States()
        {
            static std::unordered_map<ImGuiID, GradientState> states;
            return states;
        }

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 U32( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

        ImU32 Display( const GradientMath::Rgb& InRgb, float InAlpha )
        {
            return ImGui::GetColorU32( ImVec4( std::clamp( InRgb[0], 0.f, 1.f ), std::clamp( InRgb[1], 0.f, 1.f ), std::clamp( InRgb[2], 0.f, 1.f ), std::clamp( InAlpha, 0.f, 1.f ) ) );
        }

        std::string StopName( int InIndex )
        {
            return "Stop " + std::to_string( InIndex + 1 );
        }

        // The gradient across [InMin, InMax]: one multi-colour quad per span between stops (the
        // GPU interpolates the 8-bit sRGB values, which is the sRGB blend the gradient means).
        void Band( ImDrawList* InDrawList, const Graph::Mapping& InMap, const std::vector<Stop>& InStops, float InTop, float InBottom, bool InAlpha )
        {
            if( InStops.empty() || InBottom <= InTop )
            {
                return;
            }
            const std::vector<int> order = GradientMath::Order( InStops );
            auto color = [&]( const Stop& InStop ) { return Display( InStop.Color, InAlpha ? InStop.Alpha : 1.f ); };
            const Stop& first = InStops[order.front()];
            const Stop& last = InStops[order.back()];
            const float left = InMap.Min.x;
            const float right = InMap.Max.x;
            const float firstX = InMap.XToScreen( first.Position );
            const float lastX = InMap.XToScreen( last.Position );
            if( firstX > left )
            {
                InDrawList->AddRectFilled( ImVec2( left, InTop ), ImVec2( firstX, InBottom ), color( first ) );
            }
            for( size_t i = 0; i + 1 < order.size(); ++i )
            {
                const Stop& a = InStops[order[i]];
                const Stop& b = InStops[order[i + 1]];
                const float x0 = InMap.XToScreen( a.Position );
                const float x1 = InMap.XToScreen( b.Position );
                if( x1 > x0 )
                {
                    InDrawList->AddRectFilledMultiColor( ImVec2( x0, InTop ), ImVec2( x1, InBottom ), color( a ), color( b ), color( b ), color( a ) );
                }
            }
            if( lastX < right )
            {
                InDrawList->AddRectFilled( ImVec2( lastX, InTop ), ImVec2( right, InBottom ), color( last ) );
            }
        }

        // A stop's marker under the bar: a pin pointing up at the bar, filled with its colour.
        void Marker( ImDrawList* InDrawList, float InX, float InTop, float InWidth, float InHeight, const Stop& InStop, bool InSelected, bool InHovered, float InFade )
        {
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            const float half = InWidth * 0.5f;
            const float tip = std::round( InHeight * 0.35f );
            const ImVec2 points[5] = {
                ImVec2( InX, InTop ),
                ImVec2( InX + half, InTop + tip ),
                ImVec2( InX + half, InTop + InHeight ),
                ImVec2( InX - half, InTop + InHeight ),
                ImVec2( InX - half, InTop + tip ),
            };
            const ImVec4 outline = InSelected ? palette.AccentText : InHovered ? palette.Text : ImGuiTheme::WithAlpha( palette.Text, 0.7f );
            // A window-coloured rim keeps the pin apart from the bar and from its neighbours.
            InDrawList->AddConvexPolyFilled( points, 5, U32( ImGuiTheme::WithAlpha( palette.Window, InFade ) ) );
            const float inset = Thin() * ( InSelected ? 2.5f : 1.5f );
            const ImVec2 inner[5] = {
                ImVec2( InX, InTop + inset * 1.6f ),
                ImVec2( InX + half - inset, InTop + tip + inset * 0.4f ),
                ImVec2( InX + half - inset, InTop + InHeight - inset ),
                ImVec2( InX - half + inset, InTop + InHeight - inset ),
                ImVec2( InX - half + inset, InTop + tip + inset * 0.4f ),
            };
            InDrawList->AddConvexPolyFilled( inner, 5, Display( InStop.Color, InFade ) );
            InDrawList->AddPolyline( points, 5, U32( ImGuiTheme::WithAlpha( outline, InFade ) ), ImDrawFlags_Closed, Thin() * ( InSelected ? 2.f : 1.f ) );
        }
    }

    bool GradientEditor( const char* InCaption, std::vector<Stop>& InOutStops, const GradientOptions& InOptions )
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const int minStops = std::max( InOptions.MinStops, 1 );
        const int maxStops = std::max( InOptions.MaxStops, minStops );
        const bool canAdd = !InOptions.FixedPositions && static_cast<int>( InOutStops.size() ) < maxStops;
        const bool canRemove = !InOptions.FixedPositions && static_cast<int>( InOutStops.size() ) > minStops;
        bool changed = false;

        ImGui::PushID( InCaption );
        const ImGuiID stateId = window->GetID( "##gradient" );
        GradientState& state = States()[stateId];
        if( InOutStops.empty() )
        {
            state.Selected = -1;
        }
        else
        {
            state.Selected = std::clamp( state.Selected, 0, static_cast<int>( InOutStops.size() ) - 1 );
        }

        const float width = HavanaUtils::Label( InCaption );
        ItemLog::PushScope( InCaption );
        const float markerWidth = std::round( ImGuiTheme::Em( 0.7f ) );
        const float markerHeight = std::round( ImGuiTheme::Em( 0.85f ) );
        const float barHeight = std::round( ImGui::GetFrameHeight() * 1.1f );
        const float inset = std::round( markerWidth * 0.5f ) + Thin();
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = inset;
        frame.Below = markerHeight;
        const Graph::Axis xAxis { 0.0, 1.0, false, Unit::Percent };
        // A refused add or delete shakes the whole editor sideways (Juice).
        const float shake = Juice::ShakeOffset( Graph::HandleId( InCaption, "##canvas" ) );
        if( shake != 0.f )
        {
            ImGui::SetCursorScreenPos( ImVec2( ImGui::GetCursorScreenPos().x + shake, ImGui::GetCursorScreenPos().y ) );
        }
        const bool visible = Graph::Begin( InCaption, ImVec2( width, barHeight + markerHeight + inset * 2.f ), xAxis, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float barTop = map.Min.y;
        const float barBottom = map.Max.y;
        const float pixelsPerUnit = std::max( map.Max.x - map.Min.x, 1.f );
        int detentCount = 0;
        const float* detents = GradientMath::Detents( detentCount );

        // The bar: alpha over a checkerboard, then the colour alone (and the variant) underneath.
        if( visible )
        {
            const float rounding = 0.f;
            const float split = std::round( barTop + ( barBottom - barTop ) * ( InOptions.Variant ? 0.58f : 0.66f ) );
            ImGui::RenderColorRectWithAlphaCheckerboard( drawList, map.Min, ImVec2( map.Max.x, split ), IM_COL32( 0, 0, 0, 0 ), std::round( ImGuiTheme::Em( 0.35f ) ), ImVec2( 0.f, 0.f ), rounding );
            Band( drawList, map, InOutStops, barTop, split, true );
            if( InOptions.Variant && !InOutStops.empty() )
            {
                const float variantTop = std::round( split + ( barBottom - split ) * 0.5f );
                Band( drawList, map, InOutStops, split, variantTop, false );
                std::vector<Stop> variant = InOutStops;
                const std::vector<int> order = GradientMath::Order( variant );
                variant[order.front()].Color = { InOptions.Variant[0], InOptions.Variant[1], InOptions.Variant[2] };
                Band( drawList, map, variant, variantTop, barBottom, false );
            }
            else
            {
                Band( drawList, map, InOutStops, split, barBottom, false );
            }
            drawList->AddRect( ImVec2( map.Min.x - Thin(), barTop - Thin() ), ImVec2( map.Max.x + Thin(), barBottom + Thin() ), U32( palette.Separator ), rounding, 0, Thin() );
            // The quarter detents as notches under the bar.
            for( int i = 1; i + 1 < detentCount; ++i )
            {
                const float x = std::round( map.XToScreen( detents[i] ) );
                drawList->AddLine( ImVec2( x, barBottom + Thin() ), ImVec2( x, barBottom + Thin() + std::round( markerHeight * 0.3f ) ), U32( palette.Muted ), Thin() );
            }
        }

        // The stops. Changes to the list (sorting after a drag, removal) wait until all are drawn.
        int removeIndex = -1;
        bool sortAfter = false;
        bool anyHovered = false;
        const int count = static_cast<int>( InOutStops.size() );
        for( int i = 0; i < count; ++i )
        {
            Stop& stop = InOutStops[i];
            const std::string name = StopName( i );
            const ImGuiID id = window->GetID( name.c_str() );
            const float x = std::round( map.XToScreen( stop.Position ) );
            const ImRect bb( ImVec2( x - markerWidth * 0.5f - Thin(), barBottom ), ImVec2( x + markerWidth * 0.5f + Thin(), barBottom + markerHeight + Thin() * 2.f ) );
            bool hovered = false;
            bool held = false;
            bool pressed = false;
            if( ImGui::ItemAdd( bb, id ) )
            {
                pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick );
            }
            IMGUI_TEST_ENGINE_ITEM_INFO( id, name.c_str(), g.LastItemData.StatusFlags );
            anyHovered |= hovered;
            if( pressed )
            {
                state.Selected = i;
                state.Focused = true;
                state.DragId = id;
                state.GrabOffset = x - g.IO.MousePos.x;
                state.Removing = false;
            }
            if( held && state.DragId == id )
            {
                if( !InOptions.FixedPositions && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) && g.IO.MouseDelta.x != 0.f )
                {
                    const bool fine = g.IO.KeyShift;
                    float t = GradientMath::RoundPosition( static_cast<float>( map.XFromScreen( g.IO.MousePos.x + state.GrabOffset ) ), fine );
                    if( !fine )
                    {
                        t = GradientMath::SnapToDetent( t, ImGuiTheme::Em( 0.33f ) / pixelsPerUnit );
                    }
                    if( t != stop.Position )
                    {
                        for( int d = 0; d < detentCount; ++d )
                        {
                            const float detent = detents[d];
                            if( ( stop.Position < detent && t >= detent ) || ( stop.Position > detent && t <= detent ) )
                            {
                                Juice::Tick( id, ImVec2( x, barBottom ) );
                                break;
                            }
                        }
                        stop.Position = t;
                        changed = true;
                        ImGui::MarkItemEdited( id );
                    }
                }
                // Dragged well clear of the bar: it goes when the button comes up.
                const float away = ImGuiTheme::Em( 1.5f );
                state.Removing = canRemove && ( g.IO.MousePos.y > bb.Max.y + away || g.IO.MousePos.y < barTop - away );
            }
            else if( state.DragId == id && !held )
            {
                // Released.
                if( state.Removing )
                {
                    removeIndex = i;
                }
                else
                {
                    sortAfter = true;
                }
                state.DragId = 0;
                state.Removing = false;
            }
            if( hovered || held )
            {
                ImGui::SetMouseCursor( InOptions.FixedPositions ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeEW );
            }
            const std::string value = GradientMath::DescribeStop( stop ) + " at " + FormatValue( Unit::Percent, stop.Position );
            ItemLog::SetItemValue( id, value );
            if( hovered && !held )
            {
                std::string hints = InOptions.FixedPositions ? "Click to edit its colour and alpha below the bar" : "Drag along the bar (Shift: fine, no snapping); drag off or press Delete to remove";
                ImGui::SetTooltip( "%s: %s\n%s", ( InOptions.StopNames ? InOptions.StopNames[i] : name.c_str() ), value.c_str(), hints.c_str() );
            }
            if( visible )
            {
                const float grow = Juice::Spring( id, hovered || held ? 1.15f : 1.f );
                const float fade = held && state.Removing ? 0.35f : 1.f;
                const float shownX = std::round( map.XToScreen( stop.Position ) );
                Marker( drawList, shownX, barBottom + Thin(), std::round( markerWidth * grow ), std::round( markerHeight * grow ), stop, i == state.Selected, hovered || held, fade );
                const float tick = Juice::TickAmount( id );
                if( tick > 0.f )
                {
                    drawList->AddCircle( ImVec2( shownX, barBottom ), markerWidth * ( 0.6f + 0.8f * ( 1.f - tick ) ), U32( ImGuiTheme::WithAlpha( palette.Text, tick ) ), 0, Thin() );
                }
                if( i == state.Selected )
                {
                    // The selected stop's place on the bar.
                    drawList->AddLine( ImVec2( shownX, barTop ), ImVec2( shownX, barBottom ), U32( ImGuiTheme::WithAlpha( palette.Window, 0.9f ) ), Thin() * 3.f );
                    drawList->AddLine( ImVec2( shownX, barTop ), ImVec2( shownX, barBottom ), U32( palette.Text ), Thin() );
                }
            }
        }

        // An added stop flashes the bar's rim.
        const float flash = visible ? Juice::FlashAmount( canvasId ) : 0.f;
        if( flash > 0.f )
        {
            drawList->AddRect( ImVec2( map.Min.x - Thin() * 2.f, barTop - Thin() * 2.f ), ImVec2( map.Max.x + Thin() * 2.f, barBottom + Thin() * 2.f ), U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), 0.f, 0, Thin() * 2.f );
        }

        const Graph::CanvasInput input = Graph::End();
        anyHovered |= input.Hovered;
        // A press on the bar away from the stops adds one there; holding places it.
        if( input.Pressed )
        {
            state.Focused = true;
            if( canAdd )
            {
                float t = GradientMath::RoundPosition( static_cast<float>( input.X ), g.IO.KeyShift );
                if( !g.IO.KeyShift )
                {
                    t = GradientMath::SnapToDetent( t, ImGuiTheme::Em( 0.33f ) / pixelsPerUnit );
                }
                state.Placing = GradientMath::AddStop( InOutStops, t );
                state.Selected = state.Placing;
                changed = true;
                Juice::Flash( canvasId );
                Juice::Play( Juice::Cue::Commit );
                ImGui::MarkItemEdited( canvasId );
            }
            else if( !InOptions.FixedPositions )
            {
                Juice::Shake( canvasId ); // Plays the Refuse cue too.
            }
        }
        else if( input.Held && state.Placing >= 0 && state.Placing < static_cast<int>( InOutStops.size() ) && g.IO.MouseDelta.x != 0.f )
        {
            float t = GradientMath::RoundPosition( static_cast<float>( input.X ), g.IO.KeyShift );
            if( !g.IO.KeyShift )
            {
                t = GradientMath::SnapToDetent( t, ImGuiTheme::Em( 0.33f ) / pixelsPerUnit );
            }
            if( t != InOutStops[state.Placing].Position )
            {
                InOutStops[state.Placing].Position = t;
                changed = true;
                ImGui::MarkItemEdited( canvasId );
            }
        }
        else if( !input.Held && state.Placing >= 0 )
        {
            state.Placing = -1;
            sortAfter = true;
        }
        ItemLog::SetItemValue( canvasId, GradientMath::Describe( InOutStops ) );
        if( input.Hovered && !input.Held )
        {
            const char* hint = InOptions.FixedPositions ? "Click a stop to edit it."
                : canAdd ? "Click the bar to add a stop; drag a stop to move it, off the bar to remove it."
                         : "Drag a stop to move it, off the bar to remove it.";
            ImGui::SetTooltip( "%s%s%s", InOptions.Tooltip ? InOptions.Tooltip : "", InOptions.Tooltip ? "\n" : "", hint );
        }

        if( removeIndex >= 0 && GradientMath::RemoveStop( InOutStops, removeIndex, minStops ) )
        {
            if( visible )
            {
                Juice::Burst( g.IO.MousePos, U32( palette.Muted ) );
            }
            Juice::Play( Juice::Cue::Reset );
            state.Selected = std::min( removeIndex, static_cast<int>( InOutStops.size() ) - 1 );
            changed = true;
        }
        else if( sortAfter )
        {
            state.Selected = GradientMath::Sort( InOutStops, state.Selected );
        }

        // Delete removes the selected stop while the editor has the keyboard.
        if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !anyHovered )
        {
            state.Focused = false;
        }
        if( state.Focused && !g.IO.WantTextInput && ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows )
            && ( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) || ImGui::IsKeyPressed( ImGuiKey_Backspace, false ) ) && state.Selected >= 0 )
        {
            if( canRemove && GradientMath::RemoveStop( InOutStops, state.Selected, minStops ) )
            {
                state.Selected = std::min( state.Selected, static_cast<int>( InOutStops.size() ) - 1 );
                changed = true;
                Juice::Play( Juice::Cue::Reset );
                ImGui::MarkItemEdited( canvasId );
            }
            else
            {
                Juice::Shake( canvasId ); // Plays the Refuse cue too.
            }
        }
        ItemLog::PopScope();

        // The selected stop: its colour, position and alpha.
        if( state.Selected >= 0 && state.Selected < static_cast<int>( InOutStops.size() ) )
        {
            Stop& stop = InOutStops[state.Selected];
            const std::string name = StopName( state.Selected );
            ItemLog::PushScope( InCaption, name );
            const float rowWidth = HavanaUtils::Label( InOptions.StopNames ? InOptions.StopNames[state.Selected] : name.c_str() );
            const float gap = ImGuiTheme::InnerGap();
            const int fields = InOptions.FixedPositions ? 1 : 2;
            const float swatchWidth = std::round( ( rowWidth - gap * fields ) * ( fields == 1 ? 0.55f : 0.4f ) );
            const float fieldWidth = std::floor( ( rowWidth - swatchWidth - gap * fields ) / fields );

            ImGui::PushID( state.Selected );
            const ImGuiID swatchId = window->GetID( "Color##stop" );
            ImGui::SetNextItemWidth( swatchWidth );
            float color[3] = { stop.Color[0], stop.Color[1], stop.Color[2] };
            const std::string popoverCaption = std::string( InCaption ) + " " + ( InOptions.StopNames ? InOptions.StopNames[state.Selected] : name.c_str() );
            if( ColorSwatchPopover( "Color##stop", color, 3, InOptions.Hdr, popoverCaption.c_str() ) )
            {
                stop.Color = { color[0], color[1], color[2] };
                changed = true;
            }
            ItemLog::SetItemLabel( swatchId, "Color" );
            ItemLog::MarkItemRowScoped( swatchId );

            if( !InOptions.FixedPositions )
            {
                ImGui::SameLine( 0.f, gap );
                ImGui::SetNextItemWidth( fieldWidth );
                NumberSpec position;
                position.FieldUnit = Unit::Percent;
                position.HasRange = true;
                position.Min = 0.0;
                position.Max = 1.0;
                double value = stop.Position;
                const ImGuiID positionId = window->GetID( "Position##stop" );
                if( UnitField( "Position##stop", value, position ) )
                {
                    stop.Position = static_cast<float>( value );
                    state.SortPending = true;
                    changed = true;
                }
                // Re-sorted only once the field lets go (below): sorting mid-scrub would move
                // the selection, and this row's ID, out from under the gesture.
                state.PositionId = positionId;
                ItemLog::SetItemLabel( positionId, "Position" );
                ItemLog::MarkItemRowScoped( positionId );
                if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
                {
                    ImGui::SetTooltip( "Where the stop sits along the gradient" );
                }
            }

            ImGui::SameLine( 0.f, gap );
            ImGui::SetNextItemWidth( fieldWidth );
            NumberSpec alphaSpec;
            alphaSpec.FieldUnit = Unit::Percent;
            alphaSpec.HasRange = true;
            alphaSpec.Min = 0.0;
            alphaSpec.Max = 1.0;
            Stop& current = InOutStops[std::clamp( state.Selected, 0, static_cast<int>( InOutStops.size() ) - 1 )];
            double alpha = current.Alpha;
            const ImGuiID alphaId = window->GetID( "Alpha##stop" );
            if( UnitField( "Alpha##stop", alpha, alphaSpec ) )
            {
                current.Alpha = static_cast<float>( alpha );
                changed = true;
            }
            ItemLog::SetItemLabel( alphaId, "Alpha" );
            ItemLog::MarkItemRowScoped( alphaId );
            if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            {
                ImGui::SetTooltip( "The stop's opacity (the checkerboard shows through the bar)" );
            }
            ImGui::PopID();
            ItemLog::PopScope();
        }
        if( state.SortPending && g.ActiveId != state.PositionId )
        {
            state.SortPending = false;
            if( !InOutStops.empty() )
            {
                state.Selected = GradientMath::Sort( InOutStops, state.Selected );
            }
        }
        ImGui::PopID();
        return changed;
    }

    void ResetGradientEditors()
    {
        States().clear();
    }
}
