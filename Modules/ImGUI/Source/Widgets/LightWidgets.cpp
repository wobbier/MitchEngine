#include "LightWidgets.h"
#include "Color.h"
#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace MeWidgets
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 U32( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

        ImU32 SrgbU32( const float* InSrgb, float InAlpha = 1.f )
        {
            return ImGui::GetColorU32( ImVec4( std::clamp( InSrgb[0], 0.f, 1.f ), std::clamp( InSrgb[1], 0.f, 1.f ), std::clamp( InSrgb[2], 0.f, 1.f ), InAlpha ) );
        }

        // A light's colour drawn on the panel: on light themes it is pulled towards the text
        // colour, or a white beam would vanish into the background.
        ImU32 LightOnPanel( const float* InSrgb, float InAlpha )
        {
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            ImVec4 color( std::clamp( InSrgb[0], 0.f, 1.f ), std::clamp( InSrgb[1], 0.f, 1.f ), std::clamp( InSrgb[2], 0.f, 1.f ), InAlpha );
            if( ImGuiTheme::Luminance( palette.Window ) > 0.5f )
            {
                color = ImGuiTheme::Mix( color, ImGuiTheme::WithAlpha( palette.Muted, InAlpha ), 0.45f );
            }
            return ImGui::GetColorU32( color );
        }

        // Three significant digits: values set by pointing read as round numbers.
        double Significant( double InValue, int InDigits = 3 )
        {
            if( !( std::abs( InValue ) > 0.0 ) )
            {
                return 0.0;
            }
            const double power = std::pow( 10.0, std::floor( std::log10( std::abs( InValue ) ) ) - ( InDigits - 1 ) );
            return std::round( InValue / power ) * power;
        }

        bool Crossed( double InFrom, double InTo, double InDetent )
        {
            return ( InFrom < InDetent && InTo >= InDetent ) || ( InFrom > InDetent && InTo <= InDetent );
        }

        // Text with a soft halo, readable on any colour under it.
        void HaloText( ImDrawList* InDrawList, ImVec2 InPos, ImU32 InColor, ImU32 InHalo, const char* InText )
        {
            const float o = Thin();
            InDrawList->AddText( ImVec2( InPos.x - o, InPos.y ), InHalo, InText );
            InDrawList->AddText( ImVec2( InPos.x + o, InPos.y ), InHalo, InText );
            InDrawList->AddText( ImVec2( InPos.x, InPos.y - o ), InHalo, InText );
            InDrawList->AddText( ImVec2( InPos.x, InPos.y + o ), InHalo, InText );
            InDrawList->AddText( InPos, InColor, InText );
        }

        // A marker's spring: follows a value this widget just set (flash), snaps to anything else.
        float Follow( ImGuiID InId, float InTarget )
        {
            if( Juice::FlashAmount( InId ) <= 0.f )
            {
                Juice::Snap( InId, InTarget );
            }
            return Juice::Spring( InId, InTarget );
        }
    }

    // ---- Temperature --------------------------------------------------------------------

    bool KelvinBar( const char* InCaption, float* InOutSrgb )
    {
        using namespace ColorTemperature;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float barHeight = std::round( ImGui::GetFrameHeight() * 0.85f );
        const float stripHeight = ImGuiTheme::Em( 0.7f );
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const Graph::Axis xAxis { kMinKelvin, kMaxKelvin, true, Unit::Kelvin };
        const bool visible = Graph::Begin( InCaption, ImVec2( width, barHeight + stripHeight ), xAxis, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float barBottom = map.Min.y + barHeight;

        double kelvin = 0.0;
        bool onCurve = FromSrgb( { InOutSrgb[0], InOutSrgb[1], InOutSrgb[2] }, kelvin );
        bool changed = false;
        auto apply = [&]( double InKelvin ) {
            const std::array<float, 3> color = ToSrgb( InKelvin );
            std::copy( color.begin(), color.end(), InOutSrgb );
            kelvin = InKelvin;
            onCurve = true;
            changed = true;
        };

        if( visible )
        {
            constexpr int kSegments = 48;
            for( int i = 0; i < kSegments; ++i )
            {
                const double k0 = xAxis.FromNormalized( static_cast<double>( i ) / kSegments );
                const double k1 = xAxis.FromNormalized( static_cast<double>( i + 1 ) / kSegments );
                const ImU32 c0 = SrgbU32( ToSrgb( k0 ).data() );
                const ImU32 c1 = SrgbU32( ToSrgb( k1 ).data() );
                drawList->AddRectFilledMultiColor( ImVec2( std::floor( map.XToScreen( k0 ) ), map.Min.y ), ImVec2( std::ceil( map.XToScreen( k1 ) ), barBottom ), c0, c1, c1, c0 );
            }
            drawList->AddRect( map.Min, ImVec2( map.Max.x, barBottom ), U32( palette.Separator ), 0.f, 0, Thin() );
        }

        // Presets: little arrows under the bar, clickable, named in their tooltip.
        int presetCount = 0;
        const Preset* presets = Presets( presetCount );
        const float reach = ImGuiTheme::Em( 0.35f );
        for( int i = 0; i < presetCount; ++i )
        {
            const float x = std::round( map.XToScreen( presets[i].Kelvin ) );
            const ImRect rect( ImVec2( x - reach, barBottom ), ImVec2( x + reach, barBottom + stripHeight ) );
            bool hovered = false;
            if( Graph::MarkerButton( presets[i].Name, rect, &hovered ) )
            {
                apply( presets[i].Kelvin );
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, barBottom ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::Kelvin, presets[i].Kelvin ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s", presets[i].Name, FormatValue( Unit::Kelvin, presets[i].Kelvin ).c_str() );
            }
            if( visible )
            {
                const bool current = onCurve && std::abs( kelvin - presets[i].Kelvin ) < 25.0;
                const ImU32 color = U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted );
                const float size = std::round( stripHeight * 0.55f );
                const float top = barBottom + Thin() * 2.f;
                drawList->AddTriangleFilled( ImVec2( x, top ), ImVec2( x - size * 0.6f, top + size ), ImVec2( x + size * 0.6f, top + size ), color );
            }
        }

        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Click or drag for a blackbody colour (%s..%s, log scale).\nThe arrows are presets: candle, tungsten, halogen, daylight, overcast, shade, blue sky.",
                FormatValue( Unit::Kelvin, kMinKelvin ).c_str(), FormatValue( Unit::Kelvin, kMaxKelvin ).c_str() );
        }
        if( input.Pressed || ( input.Held && ( GImGui->IO.MouseDelta.x != 0.f ) ) )
        {
            double next = std::clamp( std::round( input.X / 10.0 ) * 10.0, kMinKelvin, kMaxKelvin );
            const float pointer = GImGui->IO.MousePos.x;
            for( int i = 0; i < presetCount; ++i )
            {
                if( std::abs( map.XToScreen( presets[i].Kelvin ) - pointer ) <= ImGuiTheme::Em( 0.3f ) )
                {
                    next = presets[i].Kelvin;
                }
            }
            for( int i = 0; i < presetCount && onCurve; ++i )
            {
                if( Crossed( kelvin, next, presets[i].Kelvin ) )
                {
                    Juice::Tick( canvasId, ImVec2( map.XToScreen( presets[i].Kelvin ), barBottom ) );
                    break;
                }
            }
            if( !onCurve || next != kelvin || input.Pressed )
            {
                apply( next );
            }
        }
        if( changed )
        {
            ImGui::MarkItemEdited( canvasId );
        }

        // The colour's temperature: a marker through the bar and its reading, when it is one.
        const double shownKelvin = std::round( kelvin / 10.0 ) * 10.0;
        if( visible && onCurve )
        {
            const float t = Follow( canvasId, static_cast<float>( xAxis.ToNormalized( kelvin ) ) );
            const float x = std::round( map.Min.x + t * ( map.Max.x - map.Min.x ) );
            const ImU32 dark = U32( ImGuiTheme::Gray( 0.06f ) );
            const ImU32 light = U32( ImGuiTheme::Gray( 1.f, 0.85f ) );
            drawList->AddLine( ImVec2( x, map.Min.y ), ImVec2( x, barBottom ), light, Thin() * 4.f );
            drawList->AddLine( ImVec2( x, map.Min.y ), ImVec2( x, barBottom ), dark, Thin() * 2.f );
            std::string reading = FormatValue( Unit::Kelvin, shownKelvin );
            const float gap = ImGuiTheme::Em( 0.3f );
            const float room = map.Max.x - map.Min.x - gap * 2.f;
            for( int i = 0; i < presetCount; ++i )
            {
                if( std::abs( kelvin - presets[i].Kelvin ) < 25.0 )
                {
                    // The preset name only when it fits beside the value in a narrow inspector.
                    const std::string named = reading + "  " + presets[i].Name;
                    if( ImGui::CalcTextSize( named.c_str() ).x <= room )
                    {
                        reading = named;
                    }
                }
            }
            const ImVec2 size = ImGui::CalcTextSize( reading.c_str() );
            // Beside the marker, on the side with room.
            float left = x + gap;
            if( left + size.x > map.Max.x - gap )
            {
                left = x - gap - size.x;
            }
            left = std::max( left, map.Min.x + gap );
            // Kept inside the bar even when the value alone is wider than it.
            drawList->PushClipRect( map.Min, ImVec2( map.Max.x, barBottom ), true );
            HaloText( drawList, ImVec2( left, map.Min.y + ( barHeight - size.y ) * 0.5f ), dark, light, reading.c_str() );
            drawList->PopClipRect();
            const float flash = Juice::FlashAmount( canvasId );
            if( flash > 0.f )
            {
                drawList->AddRect( ImVec2( map.Min.x, map.Min.y ), ImVec2( map.Max.x, barBottom ), U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), 0.f, 0, Thin() * 2.f );
            }
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddLine( ImVec2( x, map.Min.y - Thin() * 2.f ), ImVec2( x, barBottom + Thin() * 2.f ), U32( ImGuiTheme::WithAlpha( palette.AccentText, tick ) ), Thin() * 3.f );
            }
        }
        ItemLog::SetItemValue( canvasId, onCurve ? FormatValue( Unit::Kelvin, shownKelvin ) : std::string( "off curve" ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Reference ladder ---------------------------------------------------------------

    bool ReferenceLadder( const char* InCaption, double& InOutValue, const NumberSpec& InSpec, const Reference* InRungs, int InRungCount, const char* InTooltip )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        double low = 0.0;
        double high = 1.0;
        if( !InSpec.ScaleRange( low, high ) )
        {
            low = 0.0;
            high = std::max( InOutValue * 2.0, 1.0 );
        }
        const float stripHeight = std::round( ImGui::GetFrameHeight() * 0.5f );
        const float labelTop = stripHeight + ImGuiTheme::Em( 0.15f );
        const float height = labelTop + ImGui::GetFontSize() + ImGuiTheme::Em( 0.1f );
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        // Keep the end rungs' labels inside: the strip runs half a label in from each side.
        const Graph::Axis xAxis { low, high, InSpec.Log, InSpec.FieldUnit };
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), xAxis, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect strip( map.Min, ImVec2( map.Max.x, map.Min.y + stripHeight ) );
        bool changed = false;

        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( strip.Min, strip.Max, U32( palette.Control ), rounding );
            const float t = std::clamp( Follow( canvasId, static_cast<float>( xAxis.ToNormalized( std::clamp( InOutValue, low, high ) ) ) ), 0.f, 1.f );
            if( t > 0.f )
            {
                drawList->AddRectFilled( strip.Min, ImVec2( std::max( strip.Min.x + t * strip.GetWidth(), strip.Min.x + 1.f ), strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) ), rounding, ImDrawFlags_RoundCornersLeft );
            }
            // The value: a bar across the strip.
            const float x = std::round( strip.Min.x + t * strip.GetWidth() );
            drawList->AddLine( ImVec2( x, strip.Min.y - Thin() ), ImVec2( x, strip.Max.y + Thin() ), U32( palette.Text ), Thin() * 2.f );
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddRectFilled( strip.Min, strip.Max, U32( ImGuiTheme::WithAlpha( palette.Hovered, 0.5f * tick ) ), rounding );
            }
        }

        float lastRight = -1e9f;
        for( int i = 0; i < InRungCount; ++i )
        {
            const Reference& rung = InRungs[i];
            if( rung.Value < low || rung.Value > high )
            {
                continue;
            }
            const float x = std::round( map.XToScreen( rung.Value ) );
            const ImVec2 size = ImGui::CalcTextSize( rung.Name );
            const float left = std::clamp( x - size.x * 0.5f, map.Min.x, map.Max.x - size.x );
            const bool fits = left > lastRight + ImGuiTheme::Em( 0.3f );
            const ImRect rect = fits ? ImRect( ImVec2( left, map.Min.y + labelTop ), ImVec2( left + size.x, map.Min.y + height ) )
                                     : ImRect( ImVec2( x - ImGuiTheme::Em( 0.3f ), map.Min.y + labelTop ), ImVec2( x + ImGuiTheme::Em( 0.3f ), map.Min.y + height ) );
            bool hovered = false;
            if( Graph::MarkerButton( rung.Name, rect, &hovered ) )
            {
                InOutValue = Constrain( rung.Value, InSpec );
                changed = true;
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, strip.Max.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( InSpec.FieldUnit, rung.Value ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s\n%s", rung.Name, FormatValue( InSpec.FieldUnit, rung.Value ).c_str(), rung.Note ? rung.Note : "" );
            }
            if( visible )
            {
                const bool current = std::abs( InOutValue - rung.Value ) <= std::abs( rung.Value ) * 1e-3;
                const ImU32 color = U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted );
                drawList->AddLine( ImVec2( x, strip.Min.y ), ImVec2( x, strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.5f ) ), Thin() );
                if( fits )
                {
                    drawList->AddText( rect.Min, color, rung.Name );
                    lastRight = rect.Max.x;
                }
                else
                {
                    drawList->AddCircleFilled( ImVec2( x, rect.Min.y + Thin() * 3.f ), Thin() * 2.f, color );
                }
            }
        }

        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held && InTooltip )
        {
            ImGui::SetTooltip( "%s", InTooltip );
        }
        if( input.Pressed || ( input.Held && GImGui->IO.MouseDelta.x != 0.f ) )
        {
            double next = Constrain( Significant( std::clamp( input.X, low, high ), 2 ), InSpec );
            const float pointer = GImGui->IO.MousePos.x;
            for( int i = 0; i < InRungCount; ++i )
            {
                if( std::abs( map.XToScreen( InRungs[i].Value ) - pointer ) <= ImGuiTheme::Em( 0.3f ) )
                {
                    next = InRungs[i].Value;
                }
                if( Crossed( InOutValue, next, InRungs[i].Value ) )
                {
                    Juice::Tick( canvasId, ImVec2( map.XToScreen( InRungs[i].Value ), strip.Max.y ) );
                }
            }
            if( next != InOutValue )
            {
                InOutValue = next;
                changed = true;
            }
        }
        if( changed )
        {
            ImGui::MarkItemEdited( canvasId );
        }
        ItemLog::SetItemValue( canvasId, FormatValue( InSpec.FieldUnit, InOutValue ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Cone -----------------------------------------------------------------------------

    bool ConeWedge( const char* InCaption, double& InOutInner, double& InOutOuter, const float* InSrgb, double InMax )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        Graph::Frame frame;
        frame.XLabels = false;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 6.f ) ), Graph::Axis(), Graph::Axis(), frame );
        const ImRect canvas = Graph::CanvasRect();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float pad = ImGuiTheme::Em( 0.7f );
        const ImVec2 apex( std::round( canvas.GetCenter().x ), canvas.Min.y + pad );
        const float radius = std::max( std::min( canvas.GetHeight() - pad * 2.f, canvas.GetWidth() * 0.5f - pad ), ImGuiTheme::Em( 1.f ) );
        const float innerRadius = std::round( radius * 0.62f );
        const float down = kPi * 0.5f;
        // Handles sit on the right of the beam (screen angles grow clockwise, so that's negative).
        const float side = -1.f;
        auto rad = []( double InDegrees ) { return static_cast<float>( InDegrees ) * kPi / 180.f; };

        if( visible )
        {
            drawList->PushClipRect( canvas.Min, canvas.Max, true );
            Graph::Wedge( drawList, apex, down, rad( InOutInner ), rad( InOutOuter ), radius, LightOnPanel( InSrgb, 0.9f ) );
            const ImU32 edge = U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) );
            const ImU32 soft = U32( ImGuiTheme::WithAlpha( palette.Text, 0.3f ) );
            for( float s : { -1.f, 1.f } )
            {
                drawList->AddLine( apex, Graph::ArcPoint( apex, down, s * rad( InOutOuter ), radius ), edge, Thin() );
                drawList->AddLine( apex, Graph::ArcPoint( apex, down, s * rad( InOutInner ), innerRadius ), soft, Thin() );
            }
            drawList->PathArcTo( apex, radius, down - rad( InOutOuter ), down + rad( InOutOuter ), 32 );
            drawList->PathStroke( edge, 0, Thin() );
            drawList->PathArcTo( apex, innerRadius, down - rad( InOutInner ), down + rad( InOutInner ), 24 );
            drawList->PathStroke( soft, 0, Thin() );
            drawList->AddLine( ImVec2( apex.x, apex.y ), ImVec2( apex.x, apex.y + radius ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.2f ) ), Thin() );
            drawList->AddCircleFilled( apex, Thin() * 2.5f, U32( palette.Text ) );
            drawList->PopClipRect();
        }

        bool changed = false;
        if( Graph::AngleHandle( "Inner Cone", apex, down, side, innerRadius, InOutInner, 0.0, InMax, U32( palette.AccentText ) ) )
        {
            changed = true;
            InOutOuter = std::max( InOutOuter, InOutInner );
        }
        if( Graph::AngleHandle( "Outer Cone", apex, down, side, radius, InOutOuter, 0.0, InMax, U32( palette.Text ) ) )
        {
            changed = true;
            InOutInner = std::min( InOutInner, InOutOuter );
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The beam from the side. Drag Inner Cone (full brightness inside) and Outer Cone (no light outside);\nthe band between them is the feathered edge. Shift drags in tenths of a degree." );
        }
        ItemLog::SetItemValue( canvasId, FormatValue( Unit::Degrees, InOutInner ) + " / " + FormatValue( Unit::Degrees, InOutOuter ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Falloff ---------------------------------------------------------------------------

    double LightAttenuation( double InDistance, double InRange, double InNearField )
    {
        const double range = std::max( InRange, 1e-4 );
        const double distanceSq = InDistance * InDistance;
        const double ratio = distanceSq / ( range * range );
        const double window = std::clamp( 1.0 - ratio * ratio, 0.0, 1.0 );
        return window * window / std::max( distanceSq + InNearField, 1e-4 );
    }

    bool FalloffGraph( const char* InCaption, double& InOutRange, const NumberSpec& InSpec, const float* InSrgb, double InNearField )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();

        // The axis spans 1.2x the range; it holds still under a dragged handle, then eases over.
        static ImGuiID heldFor = 0;
        static double heldSpan = 0.0;
        // The span each graph drew last frame, by handle.
        static std::unordered_map<ImGuiID, double> lastSpans;
        const ImGuiID handleId = Graph::HandleId( InCaption, "Range" );
        const ImGuiID spanId = ImGui::GetID( "##span" );
        const double target = std::max( InOutRange, 1e-3 ) * 1.2;
        double span = target;
        if( GImGui->ActiveId == handleId )
        {
            if( heldFor != handleId )
            {
                // The span the press frame drew (and froze the drag mapping with): stepping the
                // spring again here would put the axis a step away from the pointer.
                heldFor = handleId;
                const auto drawn = lastSpans.find( handleId );
                heldSpan = drawn != lastSpans.end() ? drawn->second : target;
            }
            span = heldSpan;
            Juice::Snap( spanId, static_cast<float>( span ) );
        }
        else
        {
            if( heldFor == handleId )
            {
                heldFor = 0;
            }
            span = Juice::Spring( spanId, static_cast<float>( target ), 5.f );
        }

        lastSpans[handleId] = span;
        const Graph::Axis xAxis { 0.0, std::max( span, 1e-3 ), false, Unit::Meters };
        // Log brightness: plain inverse square is a gentle slope, the window a dive to zero.
        const Graph::Axis yAxis { 1e-3, 1.5, true, Unit::None };
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 6.f ) ), xAxis, yAxis );
        // Brightness relative to 15% of the range: the near field is clipped, the window shows.
        const double reference = std::max( InOutRange * 0.15, 1e-3 );
        const double norm = std::max( LightAttenuation( reference, InOutRange, InNearField ), 1e-12 );
        if( visible )
        {
            Graph::Grid( true, false );
            const double plainNorm = 1.0 / std::max( reference * reference + InNearField, 1e-4 );
            Graph::Plot( [&]( double d ) { return 1.0 / std::max( d * d + InNearField, 1e-4 ) / plainNorm; }, U32( ImGuiTheme::WithAlpha( palette.Muted, 0.8f ) ), false, 64, Thin() );
            Graph::Plot( [&]( double d ) { return LightAttenuation( d, InOutRange, InNearField ) / norm; }, LightOnPanel( InSrgb, 0.9f ), true, 128, Thin() * 2.f );
            // The curve again in the accent so it reads on light themes whatever the light colour.
            Graph::Plot( [&]( double d ) { return LightAttenuation( d, InOutRange, InNearField ) / norm; }, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.9f ) ), false, 128, Thin() * 1.5f );
            // The range as a dashed line up from its handle.
            const Graph::Mapping& map = Graph::Current();
            const float x = std::round( map.XToScreen( InOutRange ) );
            const float dash = ImGuiTheme::Em( 0.3f );
            for( float y = map.Max.y; y > map.Min.y; y -= dash * 2.f )
            {
                ImGui::GetWindowDrawList()->AddLine( ImVec2( x, y ), ImVec2( x, std::max( y - dash, map.Min.y ) ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.45f ) ), Thin() );
            }
        }

        double ticks[32];
        Graph::HandleSpec spec;
        spec.MinX = InSpec.HasRange ? InSpec.Min : 1e-3;
        spec.MaxX = InSpec.HasRange ? InSpec.Max : 1e6;
        spec.Detents = ticks;
        spec.DetentCount = Graph::Ticks( xAxis, ticks, 32 );
        spec.FreezeAxis = true;
        double range = InOutRange;
        bool changed = false;
        if( Graph::DragHandle( "Range", &range, nullptr, spec ) )
        {
            // A tenth of a grid step: dragged ranges read as round numbers.
            const double step = NiceStep( xAxis.Max ) * 0.1;
            range = std::clamp( std::round( range / step ) * step, spec.MinX, spec.MaxX );
            if( range != InOutRange )
            {
                InOutRange = range;
                changed = true;
            }
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The engine's falloff (Lighting.sh distanceAttenuation): inverse square, windowed to reach zero at Range.\n"
                               "Brightness (log scale) relative to 15%% of Range; the faint curve is plain inverse square.\n"
                               "Bar, box and rectangle lights measure distance from their surface. Drag the Range handle." );
        }
        ItemLog::SetItemValue( Graph::CanvasId(), FormatValue( Unit::Meters, InOutRange ) );
        ImGui::PopID();
        return changed;
    }
}
