#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "ImGuiTheme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace MeWidgets::Graph
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        struct State
        {
            Mapping Map;
            ImRect Canvas;
            ImGuiID CanvasId = 0;
            std::string Name;
            bool Visible = false;
            bool Open = false;
            CanvasInput Input;
        };

        State& Get()
        {
            static State state;
            return state;
        }

        // The handle being dragged: where it was grabbed and the mapping when the drag started.
        struct Grab
        {
            ImGuiID Id = 0;
            ImVec2 Offset;
            Mapping Frozen;
        };

        Grab& Grabbed()
        {
            static Grab grab;
            return grab;
        }

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 Color( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

        struct HandleInput
        {
            bool Visible = false;
            bool Hovered = false;
            bool Held = false;
            bool Moved = false;     // dragged this frame
            ImVec2 Target;          // where the pointer puts the handle's center
        };

        // A round handle item at InPos: hover, press and drag, with the grab offset kept so a
        // press doesn't jump the value.
        HandleInput HandleItem( ImGuiID InId, ImVec2 InPos, const Mapping* InMapping )
        {
            ImGuiContext& g = *GImGui;
            HandleInput input;
            const float reach = ImGuiTheme::Em( 0.55f );
            const ImRect bb( ImVec2( InPos.x - reach, InPos.y - reach ), ImVec2( InPos.x + reach, InPos.y + reach ) );
            input.Visible = ImGui::ItemAdd( bb, InId );
            if( !input.Visible )
            {
                return input;
            }
            ImGui::ButtonBehavior( bb, InId, &input.Hovered, &input.Held, ImGuiButtonFlags_PressedOnClick );
            Grab& grab = Grabbed();
            if( g.ActiveId == InId && ( g.ActiveIdIsJustActivated || grab.Id != InId ) )
            {
                grab.Id = InId;
                grab.Offset = ImVec2( InPos.x - g.IO.MousePos.x, InPos.y - g.IO.MousePos.y );
                if( InMapping )
                {
                    grab.Frozen = *InMapping;
                }
            }
            if( input.Held && grab.Id == InId )
            {
                input.Target = ImVec2( g.IO.MousePos.x + grab.Offset.x, g.IO.MousePos.y + grab.Offset.y );
                // Only once the press becomes a drag: a click (or the pointer settling by a
                // sub-pixel after it) leaves the value alone.
                input.Moved = ( g.IO.MouseDelta.x != 0.f || g.IO.MouseDelta.y != 0.f ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left );
            }
            if( input.Hovered || input.Held )
            {
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
            }
            return input;
        }

        // The handle disc: grows on hover (Juice spring), a ring for contrast in both themes, and
        // its value in a little tag while hovered or dragged.
        void DrawHandle( ImGuiID InId, ImVec2 InPos, ImU32 InColor, const HandleInput& InInput, const std::string& InTag )
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            const float grow = Juice::Spring( InId, InInput.Hovered || InInput.Held ? 1.4f : 1.f );
            const float radius = ImGuiTheme::Em( 0.3f ) * grow;
            const ImU32 color = InColor ? InColor : Color( palette.AccentText );
            drawList->AddCircleFilled( InPos, radius + Thin(), Color( palette.Window ) );
            drawList->AddCircleFilled( InPos, radius, color );
            drawList->AddCircle( InPos, radius + Thin(), Color( ImGuiTheme::WithAlpha( palette.Text, InInput.Held ? 0.95f : 0.6f ) ), 0, Thin() );
            const float tick = Juice::TickAmount( InId );
            if( tick > 0.f )
            {
                drawList->AddCircle( InPos, radius * ( 1.f + 1.2f * ( 1.f - tick ) ), Color( ImGuiTheme::WithAlpha( palette.Text, tick ) ), 0, Thin() );
            }
            if( !InTag.empty() && ( InInput.Hovered || InInput.Held ) )
            {
                // Above the handle, kept inside the window.
                const ImVec2 size = ImGui::CalcTextSize( InTag.c_str() );
                const ImVec2 pad( ImGuiTheme::Em( 0.25f ), ImGuiTheme::Em( 0.1f ) );
                const ImRect clip( ImGui::GetWindowPos(), ImVec2( ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y ) );
                float x = InPos.x - size.x * 0.5f;
                x = std::clamp( x, clip.Min.x + pad.x, std::max( clip.Min.x + pad.x, clip.Max.x - size.x - pad.x * 2.f ) );
                const float y = InPos.y - radius - size.y - pad.y * 2.f - Thin() * 2.f;
                drawList->AddRectFilled( ImVec2( x - pad.x, y - pad.y ), ImVec2( x + size.x + pad.x, y + size.y + pad.y ), Color( palette.Popup ), ImGui::GetStyle().FrameRounding );
                drawList->AddRect( ImVec2( x - pad.x, y - pad.y ), ImVec2( x + size.x + pad.x, y + size.y + pad.y ), Color( palette.Separator ), ImGui::GetStyle().FrameRounding );
                drawList->AddText( ImVec2( x, y ), Color( palette.Text ), InTag.c_str() );
            }
        }

        // Ticks when a value moved across (or onto) a detent.
        bool CrossedDetent( double InFrom, double InTo, const double* InDetents, int InCount )
        {
            if( InFrom == InTo )
            {
                return false;
            }
            for( int i = 0; i < InCount; ++i )
            {
                const double d = InDetents[i];
                if( ( InFrom < d && InTo >= d ) || ( InFrom > d && InTo <= d ) )
                {
                    return true;
                }
            }
            return false;
        }

        void Describe( ImGuiID InId, const char* InLabel, const std::string& InValue )
        {
            ImGuiContext& g = *GImGui;
            IMGUI_TEST_ENGINE_ITEM_INFO( InId, InLabel, g.LastItemData.StatusFlags );
            if( !InValue.empty() )
            {
                ItemLog::SetItemValue( InId, InValue );
            }
            if( Get().Open )
            {
                ItemLog::SetLastItemRow( Get().Name );
            }
        }
    }

    double Axis::ToNormalized( double InValue ) const
    {
        if( Max == Min )
        {
            return 0.0;
        }
        if( Log )
        {
            return LogMap::ToNormalized( InValue, Min, Max );
        }
        return ( InValue - Min ) / ( Max - Min );
    }

    double Axis::FromNormalized( double InT ) const
    {
        if( Log )
        {
            return LogMap::FromNormalized( std::clamp( InT, 0.0, 1.0 ), Min, Max );
        }
        return Min + InT * ( Max - Min );
    }

    float Mapping::XToScreen( double InX ) const
    {
        return Min.x + static_cast<float>( X.ToNormalized( InX ) ) * ( Max.x - Min.x );
    }

    float Mapping::YToScreen( double InY ) const
    {
        return Max.y - static_cast<float>( Y.ToNormalized( InY ) ) * ( Max.y - Min.y );
    }

    double Mapping::XFromScreen( float InScreenX ) const
    {
        const float width = Max.x - Min.x;
        return X.FromNormalized( width > 0.f ? ( InScreenX - Min.x ) / width : 0.0 );
    }

    double Mapping::YFromScreen( float InScreenY ) const
    {
        const float height = Max.y - Min.y;
        return Y.FromNormalized( height > 0.f ? ( Max.y - InScreenY ) / height : 0.0 );
    }

    int Ticks( const Axis& InAxis, double* OutValues, int InMaxTicks )
    {
        const double low = std::min( InAxis.Min, InAxis.Max );
        const double high = std::max( InAxis.Min, InAxis.Max );
        if( InMaxTicks <= 0 || !( high > low ) )
        {
            return 0;
        }
        std::vector<double> ticks;
        if( InAxis.Log )
        {
            // 1-2-5 per decade; thinned to whole decades, then every other decade, until they fit.
            const double first = low > 0.0 ? low : std::max( high * 1e-3, 1e-12 );
            const int decadeLow = static_cast<int>( std::floor( std::log10( first ) ) );
            const int decadeHigh = static_cast<int>( std::ceil( std::log10( high ) ) );
            const double mantissas[3][3] = { { 1.0, 2.0, 5.0 }, { 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } };
            for( int pass = 0; pass < 3; ++pass )
            {
                ticks.clear();
                if( low <= 0.0 )
                {
                    ticks.push_back( 0.0 );
                }
                for( int decade = decadeLow; decade <= decadeHigh; ++decade )
                {
                    if( pass == 2 && ( decade - decadeLow ) % 2 != 0 )
                    {
                        continue;
                    }
                    for( double mantissa : mantissas[pass] )
                    {
                        const double value = mantissa * std::pow( 10.0, decade );
                        if( mantissa > 0.0 && value >= first * ( 1.0 - 1e-9 ) && value <= high * ( 1.0 + 1e-9 ) )
                        {
                            ticks.push_back( value );
                        }
                    }
                }
                if( static_cast<int>( ticks.size() ) <= InMaxTicks )
                {
                    break;
                }
            }
        }
        else
        {
            double step = NiceStep( high - low );
            const double grow[] = { 2.0, 2.5, 2.0 };
            for( int i = 0; ( high - low ) / step + 1.0 > InMaxTicks && i < 60; ++i )
            {
                step *= grow[i % 3];
            }
            for( double value = std::ceil( low / step - 1e-9 ) * step; value <= high + step * 1e-9; value += step )
            {
                ticks.push_back( std::abs( value ) < step * 1e-9 ? 0.0 : value );
            }
        }
        const int count = std::min( static_cast<int>( ticks.size() ), InMaxTicks );
        std::copy_n( ticks.begin(), count, OutValues );
        return count;
    }

    bool Begin( const char* InId, ImVec2 InSize, const Axis& InX, const Axis& InY, const Frame& InFrame )
    {
        State& state = Get();
        IM_ASSERT( !state.Open && "Graph::Begin without End" );
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImVec2 start = window->DC.CursorPos;
        const ImVec2 size( InSize.x > 0.f ? InSize.x : ImGui::CalcItemWidth(), InSize.y > 0.f ? InSize.y : ImGuiTheme::Em( 5.f ) );
        state.Canvas = ImRect( start, ImVec2( start.x + size.x, start.y + size.y ) );
        ImGui::ItemSize( state.Canvas );
        ImGui::PushID( InId );
        state.Name = ItemLog::VisibleLabel( InId );
        state.CanvasId = window->GetID( "##canvas" );
        state.Open = true;

        const float inset = InFrame.Inset >= 0.f ? InFrame.Inset : ImGuiTheme::Em( 0.25f );
        const float labels = ( InFrame.XLabels ? ImGui::GetFontSize() + ImGuiTheme::Em( 0.2f ) : 0.f ) + std::max( InFrame.Below, 0.f );
        state.Map.Min = ImVec2( state.Canvas.Min.x + inset, state.Canvas.Min.y + inset );
        state.Map.Max = ImVec2( state.Canvas.Max.x - inset, std::max( state.Canvas.Max.y - inset - labels, state.Canvas.Min.y + inset + 1.f ) );
        state.Map.X = InX;
        state.Map.Y = InY;
        // The canvas is an item that lets the handles and markers drawn over it take the pointer.
        ImGuiContext& g = *GImGui;
        state.Input = CanvasInput();
        ImGui::SetNextItemAllowOverlap();
        if( ImGui::ItemAdd( state.Canvas, state.CanvasId ) )
        {
            state.Input.Pressed = ImGui::ButtonBehavior( state.Canvas, state.CanvasId, &state.Input.Hovered, &state.Input.Held, ImGuiButtonFlags_PressedOnClick );
            state.Input.X = state.Map.XFromScreen( g.IO.MousePos.x );
            state.Input.Y = state.Map.YFromScreen( g.IO.MousePos.y );
        }
        Describe( state.CanvasId, state.Name.c_str(), std::string() );
        state.Visible = !window->SkipItems && ImGui::IsRectVisible( state.Canvas.Min, state.Canvas.Max );
        if( state.Visible && InFrame.Background )
        {
            window->DrawList->AddRectFilled( state.Canvas.Min, state.Canvas.Max, Color( ImGuiTheme::ActivePalette().Recessed ), ImGui::GetStyle().FrameRounding );
        }
        return state.Visible;
    }

    const Mapping& Current()
    {
        return Get().Map;
    }

    ImRect CanvasRect()
    {
        return Get().Canvas;
    }

    ImGuiID CanvasId()
    {
        return Get().CanvasId;
    }

    ImDrawList* DrawList()
    {
        return ImGui::GetWindowDrawList();
    }

    void Grid( bool InXLabels, bool InYLabels )
    {
        State& state = Get();
        if( !state.Visible )
        {
            return;
        }
        ImDrawList* drawList = DrawList();
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const Mapping& map = state.Map;
        const ImU32 line = Color( ImGuiTheme::WithAlpha( palette.Separator, 0.6f ) );
        const ImU32 text = Color( palette.Muted );
        double ticks[32];
        const int maxX = std::clamp( static_cast<int>( ( map.Max.x - map.Min.x ) / ImGuiTheme::Em( 3.5f ) ) + 1, 2, 32 );
        const int countX = Ticks( map.X, ticks, maxX );
        float lastRight = -1e9f;
        for( int i = 0; i < countX; ++i )
        {
            const float x = std::round( map.XToScreen( ticks[i] ) );
            drawList->AddLine( ImVec2( x, map.Min.y ), ImVec2( x, map.Max.y ), line, Thin() );
            if( InXLabels )
            {
                const std::string label = FormatValue( map.X.AxisUnit, ticks[i] );
                const ImVec2 size = ImGui::CalcTextSize( label.c_str() );
                const float left = std::clamp( x - size.x * 0.5f, state.Canvas.Min.x, state.Canvas.Max.x - size.x );
                if( left > lastRight + ImGuiTheme::Em( 0.4f ) )
                {
                    drawList->AddText( ImVec2( left, map.Max.y + ImGuiTheme::Em( 0.1f ) ), text, label.c_str() );
                    lastRight = left + size.x;
                }
            }
        }
        const int maxY = std::clamp( static_cast<int>( ( map.Max.y - map.Min.y ) / ImGuiTheme::Em( 1.5f ) ) + 1, 2, 32 );
        const int countY = Ticks( map.Y, ticks, maxY );
        for( int i = 0; i < countY; ++i )
        {
            const float y = std::round( map.YToScreen( ticks[i] ) );
            drawList->AddLine( ImVec2( map.Min.x, y ), ImVec2( map.Max.x, y ), line, Thin() );
            if( InYLabels )
            {
                const std::string label = FormatValue( map.Y.AxisUnit, ticks[i] );
                drawList->AddText( ImVec2( map.Min.x + ImGuiTheme::Em( 0.15f ), y - ImGui::GetFontSize() ), text, label.c_str() );
            }
        }
    }

    void PlotSamples( const double* InX, const double* InY, int InCount, ImU32 InColor, bool InFill, float InThickness )
    {
        State& state = Get();
        if( !state.Visible || InCount < 2 )
        {
            return;
        }
        const Mapping& map = state.Map;
        ImDrawList* drawList = DrawList();
        drawList->PushClipRect( map.Min, map.Max, true );
        std::vector<ImVec2> points( InCount );
        for( int i = 0; i < InCount; ++i )
        {
            points[i] = ImVec2( map.XToScreen( InX[i] ), std::clamp( map.YToScreen( InY[i] ), map.Min.y - 1.f, map.Max.y ) );
        }
        if( InFill )
        {
            const ImU32 fill = ( InColor & ~IM_COL32_A_MASK ) | ( static_cast<ImU32>( ( ( InColor >> IM_COL32_A_SHIFT ) & 0xFF ) * 0.3f ) << IM_COL32_A_SHIFT );
            // One strip of plain triangles: anti-aliased quads would show seams between them.
            const ImVec2 uv = drawList->_Data->TexUvWhitePixel;
            drawList->PrimReserve( ( InCount - 1 ) * 6, InCount * 2 );
            const ImDrawIdx base = static_cast<ImDrawIdx>( drawList->_VtxCurrentIdx );
            for( int i = 0; i < InCount; ++i )
            {
                drawList->PrimWriteVtx( points[i], uv, fill );
                drawList->PrimWriteVtx( ImVec2( points[i].x, map.Max.y ), uv, fill );
            }
            for( int i = 0; i + 1 < InCount; ++i )
            {
                const ImDrawIdx a = static_cast<ImDrawIdx>( base + i * 2 );
                drawList->PrimWriteIdx( a );
                drawList->PrimWriteIdx( static_cast<ImDrawIdx>( a + 1 ) );
                drawList->PrimWriteIdx( static_cast<ImDrawIdx>( a + 3 ) );
                drawList->PrimWriteIdx( a );
                drawList->PrimWriteIdx( static_cast<ImDrawIdx>( a + 3 ) );
                drawList->PrimWriteIdx( static_cast<ImDrawIdx>( a + 2 ) );
            }
        }
        drawList->AddPolyline( points.data(), InCount, InColor, ImDrawFlags_None, InThickness > 0.f ? InThickness : Thin() * 2.f );
        drawList->PopClipRect();
    }

    void Plot( const std::function<double( double )>& InFunction, ImU32 InColor, bool InFill, int InSamples, float InThickness )
    {
        if( !Get().Visible || InSamples < 2 )
        {
            return;
        }
        const Axis& axis = Get().Map.X;
        std::vector<double> xs( InSamples );
        std::vector<double> ys( InSamples );
        for( int i = 0; i < InSamples; ++i )
        {
            xs[i] = axis.FromNormalized( static_cast<double>( i ) / ( InSamples - 1 ) );
            ys[i] = InFunction( xs[i] );
        }
        PlotSamples( xs.data(), ys.data(), InSamples, InColor, InFill, InThickness );
    }

    bool DragHandle( const char* InLabel, double* InOutX, double* InOutY, const HandleSpec& InSpec )
    {
        State& state = Get();
        IM_ASSERT( state.Open && InOutX );
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID id = window->GetID( InLabel );
        const Mapping& map = state.Map;
        const ImVec2 pos( map.XToScreen( *InOutX ), InOutY ? map.YToScreen( *InOutY ) : map.Max.y );
        const std::string value = FormatValue( map.X.AxisUnit, *InOutX );
        const HandleInput input = HandleItem( id, pos, &map );
        if( !input.Visible )
        {
            // Scrolled out of view: nothing to draw, but automation can still find it.
            Describe( id, InLabel, value );
            return false;
        }
        bool changed = false;
        if( input.Held && input.Moved )
        {
            const Mapping& drag = InSpec.FreezeAxis ? Grabbed().Frozen : map;
            const double x = std::clamp( drag.XFromScreen( input.Target.x ), InSpec.MinX, InSpec.MaxX );
            if( x != *InOutX )
            {
                if( CrossedDetent( *InOutX, x, InSpec.Detents, InSpec.DetentCount ) )
                {
                    Juice::Tick( id, pos );
                }
                *InOutX = x;
                changed = true;
            }
            if( InOutY )
            {
                const double y = std::clamp( drag.YFromScreen( input.Target.y ), InSpec.MinY, InSpec.MaxY );
                changed |= y != *InOutY;
                *InOutY = y;
            }
        }
        if( changed )
        {
            ImGui::MarkItemEdited( id );
        }
        const std::string shown = FormatValue( map.X.AxisUnit, *InOutX );
        const ImVec2 drawn( map.XToScreen( *InOutX ), InOutY ? map.YToScreen( *InOutY ) : map.Max.y );
        DrawHandle( id, drawn, InSpec.Color, input, InSpec.ShowValue ? std::string( ItemLog::VisibleLabel( InLabel ) ) + " " + shown : std::string() );
        Describe( id, InLabel, shown );
        return changed;
    }

    CanvasInput End()
    {
        State& state = Get();
        IM_ASSERT( state.Open );
        const CanvasInput input = state.Input;
        ImGui::PopID();
        state.Open = false;
        return input;
    }

    ImVec2 ArcPoint( ImVec2 InApex, float InDirection, float InAngle, float InRadius )
    {
        const float angle = InDirection + InAngle;
        return ImVec2( InApex.x + std::cos( angle ) * InRadius, InApex.y + std::sin( angle ) * InRadius );
    }

    float AngleOf( ImVec2 InApex, float InDirection, ImVec2 InPoint )
    {
        float angle = std::atan2( InPoint.y - InApex.y, InPoint.x - InApex.x ) - InDirection;
        while( angle > kPi ) angle -= 2.f * kPi;
        while( angle < -kPi ) angle += 2.f * kPi;
        return angle;
    }

    void Wedge( ImDrawList* InDrawList, ImVec2 InApex, float InDirection, float InInnerHalf, float InOuterHalf, float InRadius, ImU32 InColor )
    {
        const float outer = std::max( InOuterHalf, 0.f );
        const float inner = std::clamp( InInnerHalf, 0.f, outer );
        if( InRadius <= 0.f || outer <= 0.f )
        {
            InDrawList->AddLine( InApex, ArcPoint( InApex, InDirection, 0.f, InRadius ), InColor, Thin() * 2.f );
            return;
        }
        const float cosOuter = std::cos( outer );
        const float cosInner = std::cos( inner );
        const float alpha = static_cast<float>( ( InColor >> IM_COL32_A_SHIFT ) & 0xFF );
        // Engine cone: squared ramp from the outer to the inner cosine; a gentle radial fade.
        auto shade = [&]( float InAngle, float InR ) {
            const float c = std::cos( InAngle );
            const float ramp = cosInner - cosOuter > 1e-4f ? std::clamp( ( c - cosOuter ) / ( cosInner - cosOuter ), 0.f, 1.f ) : ( std::abs( InAngle ) <= outer ? 1.f : 0.f );
            const float a = alpha * ramp * ramp * ( 1.f - 0.55f * InR );
            return ( InColor & ~IM_COL32_A_MASK ) | ( static_cast<ImU32>( std::clamp( a, 0.f, 255.f ) ) << IM_COL32_A_SHIFT );
        };
        const int angular = std::clamp( static_cast<int>( outer * 40.f ), 8, 72 );
        const int radial = 6;
        const ImVec2 uv = InDrawList->_Data->TexUvWhitePixel;
        InDrawList->PrimReserve( angular * radial * 6, angular * radial * 4 );
        for( int i = 0; i < angular; ++i )
        {
            const float a0 = -outer + 2.f * outer * i / angular;
            const float a1 = -outer + 2.f * outer * ( i + 1 ) / angular;
            for( int j = 0; j < radial; ++j )
            {
                const float r0 = static_cast<float>( j ) / radial;
                const float r1 = static_cast<float>( j + 1 ) / radial;
                const ImDrawIdx base = static_cast<ImDrawIdx>( InDrawList->_VtxCurrentIdx );
                InDrawList->PrimWriteVtx( ArcPoint( InApex, InDirection, a0, r0 * InRadius ), uv, shade( a0, r0 ) );
                InDrawList->PrimWriteVtx( ArcPoint( InApex, InDirection, a0, r1 * InRadius ), uv, shade( a0, r1 ) );
                InDrawList->PrimWriteVtx( ArcPoint( InApex, InDirection, a1, r1 * InRadius ), uv, shade( a1, r1 ) );
                InDrawList->PrimWriteVtx( ArcPoint( InApex, InDirection, a1, r0 * InRadius ), uv, shade( a1, r0 ) );
                InDrawList->PrimWriteIdx( base );
                InDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( base + 1 ) );
                InDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( base + 2 ) );
                InDrawList->PrimWriteIdx( base );
                InDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( base + 2 ) );
                InDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( base + 3 ) );
            }
        }
    }

    bool AngleHandle( const char* InLabel, ImVec2 InApex, float InDirection, float InSide, float InRadius, double& InOutDegrees, double InMin, double InMax, ImU32 InColor, double InShownScale )
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID id = window->GetID( InLabel );
        const float side = InSide < 0.f ? -1.f : 1.f;
        const float radians = static_cast<float>( InOutDegrees ) * kPi / 180.f;
        const ImVec2 pos = ArcPoint( InApex, InDirection, side * radians, InRadius );
        const double scale = InShownScale > 0.0 ? InShownScale : 1.0;
        const HandleInput input = HandleItem( id, pos, nullptr );
        if( !input.Visible )
        {
            Describe( id, InLabel, FormatValue( Unit::Degrees, InOutDegrees * scale ) );
            return false;
        }
        bool changed = false;
        if( input.Held && input.Moved )
        {
            const double raw = static_cast<double>( side * AngleOf( InApex, InDirection, input.Target ) ) * 180.0 / kPi;
            // Behind the apex the wrapped angle flips sign over the top. Past a right angle on the
            // handle's side is its upper limit; past one on the other side keeps whichever bound the
            // handle is nearest, so dragging over the top stays at the limit instead of snapping.
            double degrees = std::clamp( raw, InMin, InMax );
            if( raw > 90.0 )
            {
                degrees = InMax;
            }
            else if( raw < -90.0 )
            {
                degrees = InOutDegrees >= ( InMin + InMax ) * 0.5 ? InMax : InMin;
            }
            // Whole (shown) degrees unless Shift asks for finer.
            const double steps = GImGui->IO.KeyShift ? 10.0 : 1.0;
            const double value = std::clamp( std::round( degrees * scale * steps ) / ( steps * scale ), InMin, InMax );
            if( value != InOutDegrees )
            {
                const bool limit = ( value <= InMin || value >= InMax ) && InOutDegrees > InMin && InOutDegrees < InMax;
                const double detents[] = { 15.0, 30.0, 45.0, 60.0, 75.0 };
                if( limit || CrossedDetent( InOutDegrees * scale, value * scale, detents, 5 ) )
                {
                    Juice::Tick( id, pos );
                }
                InOutDegrees = value;
                changed = true;
                ImGui::MarkItemEdited( id );
            }
        }
        const std::string shown = FormatValue( Unit::Degrees, InOutDegrees * scale );
        const ImVec2 drawn = ArcPoint( InApex, InDirection, side * static_cast<float>( InOutDegrees ) * kPi / 180.f, InRadius );
        DrawHandle( id, drawn, InColor, input, std::string( ItemLog::VisibleLabel( InLabel ) ) + " " + shown );
        Describe( id, InLabel, shown );
        return changed;
    }

    ImGuiID HandleId( const char* InGraphId, const char* InLabel )
    {
        ImGui::PushID( InGraphId );
        const ImGuiID id = ImGui::GetID( InLabel );
        ImGui::PopID();
        return id;
    }

    void SetRow( const char* InRow )
    {
        ItemLog::SetLastItemRow( InRow ? InRow : "" );
    }

    bool MarkerButton( const char* InLabel, const ImRect& InRect, bool* OutHovered )
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID id = window->GetID( InLabel );
        bool hovered = false;
        bool held = false;
        bool pressed = false;
        if( ImGui::ItemAdd( InRect, id ) )
        {
            pressed = ImGui::ButtonBehavior( InRect, id, &hovered, &held );
            if( hovered )
            {
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
            }
        }
        Describe( id, InLabel, std::string() );
        if( OutHovered )
        {
            *OutHovered = hovered;
        }
        if( pressed )
        {
            ImGui::MarkItemEdited( id );
        }
        return pressed;
    }
}
