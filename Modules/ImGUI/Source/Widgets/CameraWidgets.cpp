#include "CameraWidgets.h"
#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace MeWidgets
{
    // ---- Lens math -------------------------------------------------------------------------

    namespace Lens
    {
        namespace
        {
            constexpr double kPi = 3.14159265358979323846;

            const double kThirdStops[] = { 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2, 2.5, 2.8, 3.2, 3.5, 4.0, 4.5,
                5.0, 5.6, 6.3, 7.1, 8.0, 9.0, 10.0, 11.0, 13.0, 14.0, 16.0, 18.0, 20.0, 22.0, 25.0, 29.0, 32.0 };
            const double kFullStops[] = { 1.4, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0, 22.0 };
        }

        double FocalLength( double InVerticalFov, double InSensorHeight )
        {
            const double half = std::clamp( InVerticalFov, 1e-3, 179.999 ) * 0.5 * kPi / 180.0;
            return InSensorHeight / ( 2.0 * std::tan( half ) );
        }

        double VerticalFov( double InFocalLength, double InSensorHeight )
        {
            return 2.0 * std::atan( InSensorHeight / ( 2.0 * std::max( InFocalLength, 1e-6 ) ) ) * 180.0 / kPi;
        }

        double CircleOfConfusion( double InSensorHeight )
        {
            return 0.03 * InSensorHeight / kFullFrameHeight;
        }

        FocusRange DepthOfField( double InFocalLength, double InFStop, double InFocus, double InCircleOfConfusion )
        {
            // Millimetres throughout, metres out.
            const double f = std::max( InFocalLength, 1e-3 );
            const double n = std::max( InFStop, 1e-3 );
            const double c = std::max( InCircleOfConfusion, 1e-6 );
            const double s = std::max( InFocus * 1000.0, f * ( 1.0 + 1e-9 ) );
            const double h = f * f / ( n * c ) + f;
            FocusRange range;
            range.Hyperfocal = h / 1000.0;
            range.Near = s * ( h - f ) / ( h + s - 2.0 * f ) / 1000.0;
            range.Far = s < h ? s * ( h - f ) / ( h - s ) / 1000.0 : std::numeric_limits<double>::infinity();
            return range;
        }

        const double* ThirdStops( int& OutCount )
        {
            OutCount = static_cast<int>( std::size( kThirdStops ) );
            return kThirdStops;
        }

        const double* FullStops( int& OutCount )
        {
            OutCount = static_cast<int>( std::size( kFullStops ) );
            return kFullStops;
        }

        double SnapThirdStop( double InFStop )
        {
            const double target = std::log( std::max( InFStop, 1e-3 ) );
            double best = kThirdStops[0];
            for( double stop : kThirdStops )
            {
                if( std::abs( std::log( stop ) - target ) < std::abs( std::log( best ) - target ) )
                {
                    best = stop;
                }
            }
            return best;
        }
    }

    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        // The classic primes on the lens ring.
        const double kPrimes[] = { 14.0, 24.0, 35.0, 50.0, 85.0, 135.0, 200.0 };
        const double kScaleDetents[] = { 0.5, 0.67, 0.75, 1.0 };

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 U32( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

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

        // A marker's spring: follows a value this widget just set (flash), snaps to anything else.
        float Follow( ImGuiID InId, float InTarget, ImGuiID InFlashId )
        {
            if( Juice::FlashAmount( InFlashId ) <= 0.f && !Juice::IsMoving( InId ) )
            {
                Juice::Snap( InId, InTarget );
            }
            return Juice::Spring( InId, InTarget );
        }

        // A marker pressed and dragged scrubs the strip it sits on, like the strip itself.
        bool DraggingLastItem()
        {
            return ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) && GImGui->IO.MouseDelta.x != 0.f;
        }

        // Reports an edit on the item making it: the canvas, or the handle or marker over it being
        // dragged (ImGui only lets the active item, or any item when none is, report one).
        void MarkEdited( ImGuiID InCanvasId )
        {
            const ImGuiContext& g = *GImGui;
            ImGui::MarkItemEdited( g.ActiveId != 0 ? g.ActiveId : InCanvasId );
        }

        void FlashOutline( ImDrawList* InDrawList, const ImRect& InRect, ImGuiID InId )
        {
            const float flash = Juice::FlashAmount( InId );
            if( flash > 0.f )
            {
                InDrawList->AddRect( InRect.Min, InRect.Max, U32( ImGuiTheme::WithAlpha( ImGuiTheme::ActivePalette().Text, flash ) ), ImGui::GetStyle().FrameRounding, 0, Thin() * 2.f );
            }
        }

        // Labels for markers along a strip, left to right, in one row or staggered over two when
        // one row is too crowded: a label that still overlaps the one before it becomes a dot
        // (still clickable).
        struct MarkerLayout
        {
            float LastRight[2] = { -1e9f, -1e9f };
            int Rows = 1;
            float RowStep = 0.f;

            ImRect Place( float InX, float InTop, const char* InText, float InMinX, float InMaxX, bool& OutFits )
            {
                const ImVec2 size = ImGui::CalcTextSize( InText );
                const float left = std::clamp( InX - size.x * 0.5f, InMinX, InMaxX - size.x );
                for( int row = 0; row < Rows; ++row )
                {
                    if( left > LastRight[row] + ImGuiTheme::Em( 0.3f ) )
                    {
                        OutFits = true;
                        LastRight[row] = left + size.x;
                        const float top = InTop + RowStep * row;
                        return ImRect( ImVec2( left, top ), ImVec2( left + size.x, top + size.y ) );
                    }
                }
                OutFits = false;
                const float reach = ImGuiTheme::Em( 0.3f );
                return ImRect( ImVec2( InX - reach, InTop ), ImVec2( InX + reach, InTop + size.y ) );
            }
        };

        // How many label rows markers at InT (0..1 along a strip InWidth wide) need: 1 when they
        // all fit side by side, else 2.
        int MarkerRows( const double* InT, const std::string* InTexts, int InCount, float InWidth )
        {
            MarkerLayout layout;
            for( int i = 0; i < InCount; ++i )
            {
                bool fits = false;
                layout.Place( static_cast<float>( InT[i] ) * InWidth, 0.f, InTexts[i].c_str(), 0.f, InWidth, fits );
                if( !fits )
                {
                    return 2;
                }
            }
            return 1;
        }

        void DrawMarker( ImDrawList* InDrawList, const ImRect& InRect, bool InFits, const char* InText, ImU32 InColor )
        {
            if( InFits )
            {
                InDrawList->AddText( InRect.Min, InColor, InText );
            }
            else
            {
                InDrawList->AddCircleFilled( ImVec2( InRect.GetCenter().x, InRect.Min.y + Thin() * 3.f ), Thin() * 2.f, InColor );
            }
        }

        // Full-frame equivalent focal lengths people name lenses by.
        const char* LensClass( double InEquivalent )
        {
            if( InEquivalent < 21.0 ) return "Ultra wide";
            if( InEquivalent < 35.0 ) return "Wide";
            if( InEquivalent < 70.0 ) return "Normal";
            if( InEquivalent < 135.0 ) return "Short tele";
            if( InEquivalent < 300.0 ) return "Telephoto";
            return "Super tele";
        }

        // The widget a canvas press started a drag in (ring vs. wedge), by canvas.
        ImGuiID& PressedRing()
        {
            static ImGuiID id = 0;
            return id;
        }
    }

    // ---- Focal length -------------------------------------------------------------------

    bool FocalLengthRow( const char* InCaption, double& InOutFov, double InSensorHeight, const char* InSensorNote )
    {
        const double sensor = std::max( InSensorHeight, 0.1 );
        NumberSpec spec;
        spec.FieldUnit = Unit::Millimeters;
        spec.HasRange = true;
        spec.Min = Lens::FocalLength( 179.0, sensor );
        spec.Max = Lens::FocalLength( 1.0, sensor );
        // A scrub across the field covers ultra wide to long telephoto (full-frame equivalents).
        spec.HasSoftRange = true;
        spec.SoftMin = 10.0 * sensor / Lens::kFullFrameHeight;
        spec.SoftMax = 400.0 * sensor / Lens::kFullFrameHeight;
        spec.Log = true;
        spec.HasDefault = true;
        spec.Default = Lens::FocalLength( 60.0, sensor );
        double focal = Lens::FocalLength( InOutFov, sensor );
        const double before = focal;
        const bool changed = UnitRow( InCaption, focal, spec );
        if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) && !ImGui::IsItemActive() )
        {
            ImGui::SetTooltip( "The focal length giving this vertical field of view on a %s sensor height (%s).\n"
                               "Type one (\"35 mm\") to set the field of view: fov = 2 atan(h / 2f).",
                FormatValue( Unit::Millimeters, sensor ).c_str(), InSensorNote ? InSensorNote : "" );
        }
        if( !changed || focal == before )
        {
            return false;
        }
        InOutFov = std::clamp( Lens::VerticalFov( focal, sensor ), 1.0, 179.0 );
        return true;
    }

    // ---- Lens wedge ---------------------------------------------------------------------------

    bool LensWedge( const char* InCaption, double& InOutFov, double InSensorHeight, const char* InSensorNote )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const double sensor = std::max( InSensorHeight, 0.1 );
        const float wedgeHeight = ImGuiTheme::Em( 4.5f );
        const float ringHeight = ImGui::GetFontSize() + ImGuiTheme::Em( 0.65f );
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Inset = 0.f;
        const Graph::Axis ringAxis { 10.0, 300.0, true, Unit::Millimeters };
        const bool visible = Graph::Begin( InCaption, ImVec2( width, wedgeHeight + ringHeight ), ringAxis, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImRect canvas = Graph::CanvasRect();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect wedgeRect( canvas.Min, ImVec2( canvas.Max.x, canvas.Min.y + wedgeHeight ) );
        const ImRect ring( ImVec2( canvas.Min.x, wedgeRect.Max.y ), canvas.Max );
        const float pad = ImGuiTheme::Em( 0.7f );
        const ImVec2 apex( wedgeRect.Min.x + pad, std::round( wedgeRect.GetCenter().y ) );
        const float reach = std::max( wedgeRect.GetWidth() - pad * 2.f, ImGuiTheme::Em( 2.f ) );
        const float halfRoom = wedgeRect.GetHeight() * 0.5f - ImGuiTheme::Em( 0.5f );
        auto rad = []( double InDegrees ) { return static_cast<float>( InDegrees ) * kPi / 180.f; };

        bool changed = false;
        double fov = std::clamp( InOutFov, 1.0, 179.0 );
        auto apply = [&]( double InFov ) {
            const double next = std::clamp( InFov, 1.0, 179.0 );
            const double from = Lens::FocalLength( fov, sensor );
            const double to = Lens::FocalLength( next, sensor );
            for( double prime : kPrimes )
            {
                if( Crossed( from, to, prime ) )
                {
                    Juice::Tick( canvasId, ImVec2( map.XToScreen( prime ), ring.Min.y ) );
                    break;
                }
            }
            if( next != fov )
            {
                fov = next;
                changed = true;
            }
        };

        if( visible )
        {
            const float half = rad( fov * 0.5 );
            drawList->PushClipRect( wedgeRect.Min, wedgeRect.Max, true );
            Graph::Wedge( drawList, apex, 0.f, half, half, reach, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.55f ) ) );
            const ImU32 edge = U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) );
            for( float s : { -1.f, 1.f } )
            {
                drawList->AddLine( apex, Graph::ArcPoint( apex, 0.f, s * half, reach * 2.f ), edge, Thin() );
            }
            drawList->AddLine( apex, ImVec2( wedgeRect.Max.x, apex.y ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.2f ) ), Thin() );
            // The image plane behind the lens: a little filmback.
            const float film = ImGuiTheme::Em( 0.45f );
            drawList->AddLine( ImVec2( apex.x - ImGuiTheme::Em( 0.3f ), apex.y - film ), ImVec2( apex.x - ImGuiTheme::Em( 0.3f ), apex.y + film ), U32( palette.Text ), Thin() * 2.f );
            drawList->AddCircleFilled( apex, Thin() * 2.5f, U32( palette.Text ) );
            drawList->PopClipRect();
        }

        double halfAngle = fov * 0.5;
        const float handleRadius = std::min( reach * 0.62f, halfRoom / std::max( std::sin( rad( halfAngle ) ), 1e-3f ) );
        if( Graph::AngleHandle( "FOV", apex, 0.f, -1.f, handleRadius, halfAngle, 0.5, 89.5, U32( palette.AccentText ), 2.0 ) )
        {
            apply( halfAngle * 2.0 );
        }

        // The lens ring: the primes to click, the current focal length marked.
        const float labelTop = ring.Min.y + ImGuiTheme::Em( 0.35f );
        bool ringScrub = false;
        MarkerLayout layout;
        for( double prime : kPrimes )
        {
            const float x = std::round( map.XToScreen( prime ) );
            const std::string name = FormatValue( Unit::Millimeters, prime );
            const std::string shown = FormatNumber( prime );
            bool fits = false;
            const ImRect rect = layout.Place( x, labelTop, shown.c_str(), ring.Min.x + Thin(), ring.Max.x - Thin(), fits );
            bool hovered = false;
            const bool clicked = Graph::MarkerButton( name.c_str(), rect, &hovered );
            ringScrub |= DraggingLastItem();
            if( clicked )
            {
                apply( Lens::VerticalFov( prime, sensor ) );
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, ring.Min.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::Degrees, Lens::VerticalFov( prime, sensor ) ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s lens: %s vertical field of view on this sensor", name.c_str(), FormatValue( Unit::Degrees, Significant( Lens::VerticalFov( prime, sensor ) ) ).c_str() );
            }
            if( visible )
            {
                const bool current = std::abs( Lens::FocalLength( fov, sensor ) - prime ) <= prime * 5e-3;
                const ImU32 color = U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted );
                drawList->AddLine( ImVec2( x, ring.Min.y ), ImVec2( x, ring.Min.y + ImGuiTheme::Em( 0.25f ) ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.5f ) ), Thin() );
                DrawMarker( drawList, rect, fits, shown.c_str(), color );
            }
        }

        const Graph::CanvasInput input = Graph::End();
        const ImGuiContext& g = *GImGui;
        if( input.Pressed || ringScrub )
        {
            PressedRing() = ringScrub || g.IO.MousePos.y >= ring.Min.y ? canvasId : 0;
        }
        if( input.Pressed || ringScrub || ( input.Held && ( g.IO.MouseDelta.x != 0.f || g.IO.MouseDelta.y != 0.f ) ) )
        {
            if( PressedRing() == canvasId )
            {
                // Zoom along the ring: three significant digits of focal length, snapping to primes.
                double next = Significant( std::clamp( map.XFromScreen( g.IO.MousePos.x ), ringAxis.Min, ringAxis.Max ), 3 );
                for( double prime : kPrimes )
                {
                    if( std::abs( map.XToScreen( prime ) - g.IO.MousePos.x ) <= ImGuiTheme::Em( 0.3f ) )
                    {
                        next = prime;
                    }
                }
                apply( Lens::VerticalFov( next, sensor ) );
            }
            else
            {
                // In the wedge: the edge follows the pointer, in whole degrees of field of view.
                const double degrees = std::abs( Graph::AngleOf( apex, 0.f, g.IO.MousePos ) ) * 180.0 / kPi;
                apply( std::round( std::clamp( degrees, 0.5, 89.5 ) * 2.0 ) );
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The view from the side: the vertical field of view, with the focal length it equals on a %s sensor height\n(%s).\n"
                               "Drag FOV (or anywhere in the wedge); click a lens on the ring, or drag along it to zoom.",
                FormatValue( Unit::Millimeters, sensor ).c_str(), InSensorNote ? InSensorNote : "" );
        }
        if( changed )
        {
            MarkEdited( canvasId );
            InOutFov = fov;
        }

        // The current focal length: a pointer on the ring and a reading in the wedge.
        const double shownFocal = Lens::FocalLength( fov, sensor );
        if( visible )
        {
            const float t = std::clamp( Follow( ImGui::GetID( "##ring" ), static_cast<float>( ringAxis.ToNormalized( std::clamp( shownFocal, ringAxis.Min, ringAxis.Max ) ) ), canvasId ), 0.f, 1.f );
            const float x = std::round( map.Min.x + t * ( map.Max.x - map.Min.x ) );
            const float size = ImGuiTheme::Em( 0.3f );
            drawList->AddTriangleFilled( ImVec2( x, ring.Min.y + size ), ImVec2( x - size, ring.Min.y - Thin() ), ImVec2( x + size, ring.Min.y - Thin() ), U32( palette.AccentText ) );
            drawList->AddLine( ring.Min, ImVec2( ring.Max.x, ring.Min.y ), U32( palette.Separator ), Thin() );
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddRectFilled( ring.Min, ring.Max, U32( ImGuiTheme::WithAlpha( palette.Hovered, 0.35f * tick ) ) );
            }
            const std::string reading = FormatValue( Unit::Millimeters, Significant( shownFocal ) );
            const char* kind = LensClass( shownFocal * Lens::kFullFrameHeight / sensor );
            const ImVec2 readingSize = ImGui::CalcTextSize( reading.c_str() );
            const ImVec2 kindSize = ImGui::CalcTextSize( kind );
            const float right = wedgeRect.Max.x - ImGuiTheme::Em( 0.4f );
            const float top = wedgeRect.Min.y + ImGuiTheme::Em( 0.25f );
            drawList->AddText( ImVec2( right - readingSize.x, top ), U32( palette.Text ), reading.c_str() );
            drawList->AddText( ImVec2( right - kindSize.x, top + readingSize.y ), U32( palette.Muted ), kind );
            FlashOutline( drawList, canvas, canvasId );
        }
        ItemLog::SetItemValue( canvasId, FormatValue( Unit::Millimeters, Significant( shownFocal, 4 ) ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Render scale ----------------------------------------------------------------------

    bool RenderScaleBar( const char* InCaption, double& InOutScale, int InOutputWidth, int InOutputHeight, const RenderSizeFunction& InInternalSize, const char* InOutputNote )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float lineHeight = ImGui::GetTextLineHeight();
        const float stripTop = lineHeight + ImGuiTheme::Em( 0.25f );
        const float stripHeight = std::round( ImGui::GetFrameHeight() * 0.45f );
        const float labelTop = stripTop + stripHeight + ImGuiTheme::Em( 0.15f );
        // From a quarter: the detents get room to be read.
        const Graph::Axis xAxis { 0.25, 1.0, false, Unit::Percent };
        constexpr int kDetentCount = static_cast<int>( std::size( kScaleDetents ) );
        double detentT[kDetentCount];
        std::string detentNames[kDetentCount];
        for( int i = 0; i < kDetentCount; ++i )
        {
            detentT[i] = xAxis.ToNormalized( kScaleDetents[i] );
            detentNames[i] = FormatValue( Unit::Percent, kScaleDetents[i] );
        }
        MarkerLayout layout;
        layout.Rows = MarkerRows( detentT, detentNames, kDetentCount, width );
        layout.RowStep = ImGui::GetFontSize() + ImGuiTheme::Em( 0.05f );
        const float height = labelTop + ImGui::GetFontSize() + layout.RowStep * ( layout.Rows - 1 ) + ImGuiTheme::Em( 0.1f );
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), xAxis, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect strip( ImVec2( map.Min.x, map.Min.y + stripTop ), ImVec2( map.Max.x, map.Min.y + stripTop + stripHeight ) );
        bool changed = false;

        auto readout = [&]( double InScale ) {
            int w = 0;
            int h = 0;
            if( InInternalSize )
            {
                InInternalSize( InScale, w, h );
            }
            else
            {
                w = std::max( 1, static_cast<int>( std::round( InOutputWidth * InScale ) ) );
                h = std::max( 1, static_cast<int>( std::round( InOutputHeight * InScale ) ) );
            }
            return std::to_string( w ) + " x " + std::to_string( h ) + " of " + std::to_string( InOutputWidth ) + " x " + std::to_string( InOutputHeight );
        };

        bool markerScrub = false;
        for( double detent : kScaleDetents )
        {
            const float x = std::round( map.XToScreen( detent ) );
            const std::string name = FormatValue( Unit::Percent, detent );
            bool fits = false;
            const ImRect rect = layout.Place( x, map.Min.y + labelTop, name.c_str(), map.Min.x, map.Max.x, fits );
            bool hovered = false;
            const bool clicked = Graph::MarkerButton( name.c_str(), rect, &hovered );
            markerScrub |= DraggingLastItem();
            if( clicked )
            {
                if( InOutScale != detent )
                {
                    InOutScale = detent;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, strip.Max.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), readout( detent ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s", name.c_str(), readout( detent ).c_str() );
            }
            if( visible )
            {
                const bool current = std::abs( InOutScale - detent ) < 5e-4;
                drawList->AddLine( ImVec2( x, strip.Min.y ), ImVec2( x, strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.5f ) ), Thin() );
                DrawMarker( drawList, rect, fits, name.c_str(), U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted ) );
            }
        }

        const Graph::CanvasInput input = Graph::End();
        const ImGuiContext& g = *GImGui;
        if( input.Pressed || markerScrub || ( input.Held && g.IO.MouseDelta.x != 0.f ) )
        {
            // Whole percents, snapping to the detents near the pointer.
            double next = std::clamp( std::round( std::clamp( map.XFromScreen( g.IO.MousePos.x ), xAxis.Min, xAxis.Max ) * 100.0 ) / 100.0, 0.1, 1.0 );
            for( double detent : kScaleDetents )
            {
                if( std::abs( map.XToScreen( detent ) - g.IO.MousePos.x ) <= ImGuiTheme::Em( 0.3f ) )
                {
                    next = detent;
                }
                if( Crossed( InOutScale, next, detent ) )
                {
                    Juice::Tick( canvasId, ImVec2( map.XToScreen( detent ), strip.Max.y ) );
                }
            }
            if( next != InOutScale )
            {
                InOutScale = next;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The resolution the scene renders at, then upscaled to the output (%s).\n"
                               "Click a detent or drag along the strip; half the scale is a quarter of the pixels.",
                InOutputNote ? InOutputNote : "" );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }

        const std::string text = readout( InOutScale );
        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( strip.Min, strip.Max, U32( palette.Control ), rounding );
            const float t = std::clamp( Follow( ImGui::GetID( "##fill" ), static_cast<float>( xAxis.ToNormalized( std::clamp( InOutScale, xAxis.Min, xAxis.Max ) ) ), canvasId ), 0.f, 1.f );
            if( t > 0.f )
            {
                drawList->AddRectFilled( strip.Min, ImVec2( std::max( strip.Min.x + t * strip.GetWidth(), strip.Min.x + Thin() ), strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) ), rounding, ImDrawFlags_RoundCornersLeft );
            }
            const float x = std::round( strip.Min.x + t * strip.GetWidth() );
            drawList->AddLine( ImVec2( x, strip.Min.y - Thin() ), ImVec2( x, strip.Max.y + Thin() ), U32( palette.Text ), Thin() * 2.f );
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddRectFilled( strip.Min, strip.Max, U32( ImGuiTheme::WithAlpha( palette.Hovered, 0.5f * tick ) ), rounding );
            }
            // The readout, with the share of the output's pixels when it fits.
            const double pixels = InOutputWidth > 0 && InOutputHeight > 0 ? InOutScale * InOutScale : 0.0;
            const std::string share = "  " + FormatValue( Unit::Percent, Significant( pixels, 2 ) ) + " of the pixels";
            drawList->AddText( map.Min, U32( palette.Text ), text.c_str() );
            const float used = ImGui::CalcTextSize( text.c_str() ).x;
            if( pixels > 0.0 && used + ImGui::CalcTextSize( share.c_str() ).x <= map.Max.x - map.Min.x )
            {
                drawList->AddText( ImVec2( map.Min.x + used, map.Min.y ), U32( palette.Muted ), share.c_str() );
            }
            FlashOutline( drawList, strip, canvasId );
        }
        ItemLog::SetItemValue( canvasId, text );
        ImGui::PopID();
        return changed;
    }

    // ---- Aperture ---------------------------------------------------------------------------

    namespace
    {
        // A lens iris: seven blades closing as the f-number grows. The opening's radius follows
        // 1 / sqrt(N) (the true 1 / N would vanish by f/16 at this size).
        void DrawIris( ImDrawList* InDrawList, ImVec2 InCenter, float InRadius, double InFStop )
        {
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            constexpr int kBlades = 7;
            const float opening = InRadius * 0.92f * static_cast<float>( 1.0 / std::sqrt( std::max( InFStop, 0.5 ) ) );
            const float turn = static_cast<float>( std::log2( std::max( InFStop, 0.5 ) ) ) * 0.35f;
            const ImU32 blade = U32( ImGuiTheme::Mix( palette.Muted, palette.Window, 0.55f ) );
            const ImU32 light = U32( ImGuiTheme::Mix( palette.Window, palette.AccentText, 0.55f ) );
            const ImU32 line = U32( ImGuiTheme::WithAlpha( palette.Window, 0.85f ) );
            InDrawList->AddCircleFilled( InCenter, InRadius, blade, 40 );
            ImVec2 points[kBlades];
            for( int i = 0; i < kBlades; ++i )
            {
                const float a = turn + 2.f * kPi * i / kBlades;
                points[i] = ImVec2( InCenter.x + std::cos( a ) * opening, InCenter.y + std::sin( a ) * opening );
            }
            InDrawList->AddConvexPolyFilled( points, kBlades, light );
            // Blade edges: from each corner of the opening, along the next side, out to the rim.
            for( int i = 0; i < kBlades; ++i )
            {
                const ImVec2 a = points[i];
                const ImVec2 b = points[( i + 1 ) % kBlades];
                const ImVec2 d( b.x - a.x, b.y - a.y );
                const float length = std::max( std::sqrt( d.x * d.x + d.y * d.y ), 1e-3f );
                // Where the side's line leaves the barrel.
                const ImVec2 u( d.x / length, d.y / length );
                const ImVec2 rel( a.x - InCenter.x, a.y - InCenter.y );
                const float along = -( rel.x * u.x + rel.y * u.y );
                const float off = rel.x * rel.x + rel.y * rel.y - along * along;
                const float reach = along + std::sqrt( std::max( InRadius * InRadius - off, 0.f ) );
                InDrawList->AddLine( a, ImVec2( a.x + u.x * reach, a.y + u.y * reach ), line, Thin() );
            }
            InDrawList->AddCircle( InCenter, InRadius, U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), 40, Thin() );
        }
    }

    bool ApertureDial( const char* InCaption, double& InOutFStop )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float trackTop = ImGuiTheme::Em( 0.75f );
        const float labelTop = trackTop + ImGuiTheme::Em( 0.4f );
        const Graph::Axis xAxis { 1.0, 32.0, true, Unit::FStop };
        int fullCount = 0;
        const double* fulls = Lens::FullStops( fullCount );
        // The labels' rows, from the strip's width with the iris at its smallest (one row high).
        double stopT[16];
        std::string stopNames[16];
        fullCount = std::min( fullCount, 16 );
        for( int i = 0; i < fullCount; ++i )
        {
            stopT[i] = xAxis.ToNormalized( fulls[i] );
            stopNames[i] = FormatNumber( fulls[i] );
        }
        const float baseHeight = labelTop + ImGui::GetFontSize() + ImGuiTheme::Em( 0.15f );
        MarkerLayout layout;
        layout.Rows = MarkerRows( stopT, stopNames, fullCount, width - baseHeight - ImGuiTheme::Em( 0.7f ) );
        layout.RowStep = ImGui::GetFontSize() + ImGuiTheme::Em( 0.05f );
        const float height = baseHeight + layout.RowStep * ( layout.Rows - 1 );
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), xAxis, Graph::Axis(), frame );
        const ImRect canvas = Graph::CanvasRect();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        // The iris on the left; the stops run to its right.
        const float icon = baseHeight;
        const float iconTop = std::round( canvas.Min.y + ( height - icon ) * 0.5f );
        Graph::Mapping map;
        map.Min = ImVec2( canvas.Min.x + icon + ImGuiTheme::Em( 0.5f ), canvas.Min.y );
        map.Max = ImVec2( canvas.Max.x - ImGuiTheme::Em( 0.2f ), canvas.Max.y );
        map.X = xAxis;
        const float track = map.Min.y + trackTop;
        bool changed = false;
        int thirdCount = 0;
        const double* thirds = Lens::ThirdStops( thirdCount );

        if( visible )
        {
            const ImU32 soft = U32( ImGuiTheme::WithAlpha( palette.Text, 0.35f ) );
            drawList->AddLine( ImVec2( map.Min.x, track ), ImVec2( map.Max.x, track ), U32( palette.Separator ), Thin() );
            for( int i = 0; i < thirdCount; ++i )
            {
                if( thirds[i] < xAxis.Min || thirds[i] > xAxis.Max )
                {
                    continue;
                }
                const float x = std::round( map.XToScreen( thirds[i] ) );
                drawList->AddLine( ImVec2( x, track - ImGuiTheme::Em( 0.15f ) ), ImVec2( x, track ), soft, Thin() );
            }
        }

        bool markerScrub = false;
        for( int i = 0; i < fullCount; ++i )
        {
            const double stop = fulls[i];
            const float x = std::round( map.XToScreen( stop ) );
            const std::string shown = FormatNumber( stop );
            bool fits = false;
            const ImRect rect = layout.Place( x, map.Min.y + labelTop, shown.c_str(), map.Min.x, canvas.Max.x, fits );
            bool hovered = false;
            const bool clicked = Graph::MarkerButton( shown.c_str(), rect, &hovered );
            markerScrub |= DraggingLastItem();
            if( clicked )
            {
                if( InOutFStop != stop )
                {
                    InOutFStop = stop;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, track ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::FStop, stop ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s the light of %s", FormatValue( Unit::FStop, stop ).c_str(), i == 0 ? "twice" : "half", i == 0 ? "f/2" : FormatValue( Unit::FStop, fulls[i - 1] ).c_str() );
            }
            if( visible )
            {
                const bool current = std::abs( InOutFStop - stop ) <= stop * 0.01;
                drawList->AddLine( ImVec2( x, track - ImGuiTheme::Em( 0.3f ) ), ImVec2( x, track ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), Thin() );
                DrawMarker( drawList, rect, fits, shown.c_str(), U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted ) );
            }
        }

        const Graph::CanvasInput input = Graph::End();
        const ImGuiContext& g = *GImGui;
        // The iris icon only shows the stop: a press on it doesn't scrub (nor its drag after).
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID scrubKey = ImGui::GetID( "##irisScrub" );
        if( input.Pressed )
        {
            storage->SetBool( scrubKey, g.IO.MousePos.x >= map.Min.x );
        }
        const bool rowScrub = storage->GetBool( scrubKey, false );
        if( ( input.Pressed && rowScrub ) || markerScrub || ( input.Held && rowScrub && g.IO.MouseDelta.x != 0.f ) )
        {
            // Absolute along the row (log scale), in third stops; a tick at each new stop.
            const double next = Lens::SnapThirdStop( std::clamp( map.XFromScreen( g.IO.MousePos.x ), xAxis.Min, xAxis.Max ) );
            if( next != InOutFStop )
            {
                if( !input.Pressed )
                {
                    Juice::Tick( canvasId, ImVec2( map.XToScreen( next ), track ) );
                }
                InOutFStop = next;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The iris opens as the f-number falls: smaller numbers blur more outside the focus band\n"
                               "(depth of field only; exposure is unchanged). Click a full stop, or drag along the row\n"
                               "to scrub in third stops." );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }

        if( visible )
        {
            // The iris and the pointer follow the stop on one spring (log f-number).
            const float shown = Follow( ImGui::GetID( "##iris" ), static_cast<float>( std::log2( std::max( InOutFStop, 0.5 ) ) ), canvasId );
            const double stop = std::exp2( static_cast<double>( shown ) );
            DrawIris( drawList, ImVec2( canvas.Min.x + icon * 0.5f, iconTop + icon * 0.5f ), icon * 0.5f - Thin(), stop );
            const float x = std::round( map.XToScreen( std::clamp( stop, xAxis.Min, xAxis.Max ) ) );
            const float size = ImGuiTheme::Em( 0.3f );
            drawList->AddTriangleFilled( ImVec2( x, track ), ImVec2( x - size, track - size * 1.6f ), ImVec2( x + size, track - size * 1.6f ), U32( palette.AccentText ) );
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddCircle( ImVec2( x, track ), size * ( 1.f + 1.5f * ( 1.f - tick ) ), U32( ImGuiTheme::WithAlpha( palette.Text, tick ) ), 0, Thin() );
            }
            FlashOutline( drawList, ImRect( ImVec2( canvas.Min.x, iconTop ), ImVec2( canvas.Min.x + icon, iconTop + icon ) ), canvasId );
        }
        ItemLog::SetItemValue( canvasId, FormatValue( Unit::FStop, InOutFStop ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Focus ------------------------------------------------------------------------------

    bool FocusBar( const char* InCaption, double& InOutFocus, double InFocalLength, double InFStop, double InSensorHeight, const char* InLensNote )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const double coc = Lens::CircleOfConfusion( std::max( InSensorHeight, 0.1 ) );
        const Graph::Axis xAxis { 0.1, 1000.0, true, Unit::Meters };
        const float plotHeight = ImGuiTheme::Em( 2.5f );
        const float height = plotHeight + ImGui::GetFontSize() + ImGuiTheme::Em( 0.7f );
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), xAxis, Graph::Axis() );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        bool changed = false;
        const Lens::FocusRange range = Lens::DepthOfField( InFocalLength, InFStop, InOutFocus, coc );
        auto describe = [&]( const Lens::FocusRange& InRange ) {
            const std::string near = FormatValue( Unit::Meters, Significant( InRange.Near ) );
            return std::isfinite( InRange.Far ) ? near + " to " + FormatValue( Unit::Meters, Significant( InRange.Far ) ) : near + " to infinity";
        };

        if( visible )
        {
            Graph::Grid( true, false );
            // The sharp band.
            const float left = map.XToScreen( std::clamp( range.Near, xAxis.Min, xAxis.Max ) );
            const bool endless = !std::isfinite( range.Far ) || range.Far >= xAxis.Max;
            const float right = endless ? map.Max.x : map.XToScreen( std::clamp( range.Far, xAxis.Min, xAxis.Max ) );
            const ImU32 edge = U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.9f ) );
            drawList->AddRectFilled( ImVec2( left, map.Min.y ), ImVec2( std::max( right, left + Thin() ), map.Max.y ), U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.3f ) ) );
            drawList->AddLine( ImVec2( left, map.Min.y ), ImVec2( left, map.Max.y ), edge, Thin() );
            if( endless )
            {
                // Sharp to infinity: an arrow off the end.
                const float size = ImGuiTheme::Em( 0.3f );
                const float y = ( map.Min.y + map.Max.y ) * 0.5f;
                drawList->AddTriangleFilled( ImVec2( map.Max.x, y ), ImVec2( map.Max.x - size * 1.4f, y - size ), ImVec2( map.Max.x - size * 1.4f, y + size ), edge );
            }
            else
            {
                drawList->AddLine( ImVec2( right, map.Min.y ), ImVec2( right, map.Max.y ), edge, Thin() );
            }
            const float focus = std::round( map.XToScreen( InOutFocus ) );
            drawList->AddLine( ImVec2( focus, map.Min.y ), ImVec2( focus, map.Max.y ), U32( palette.Text ), Thin() * 2.f );
            // The near and far limits, on whichever side of the focus has room.
            const std::string text = describe( range );
            const ImVec2 size = ImGui::CalcTextSize( text.c_str() );
            const float gap = ImGuiTheme::Em( 0.3f );
            float x = focus + gap;
            if( x + size.x > map.Max.x - gap )
            {
                x = std::max( focus - gap - size.x, map.Min.x + gap );
            }
            // On a backing so grid lines and the band's edges don't cut through it.
            const ImVec2 at( x, map.Min.y + Thin() * 2.f );
            const float pad = ImGuiTheme::Em( 0.15f );
            drawList->AddRectFilled( ImVec2( at.x - pad, at.y ), ImVec2( at.x + size.x + pad, at.y + size.y ), U32( ImGuiTheme::WithAlpha( palette.Recessed, 0.8f ) ), ImGui::GetStyle().FrameRounding );
            drawList->AddText( at, U32( palette.Text ), text.c_str() );
        }

        // The hyperfocal distance: click to focus there (sharp from half of it to infinity).
        if( range.Hyperfocal >= xAxis.Min && range.Hyperfocal <= xAxis.Max )
        {
            const float x = std::round( map.XToScreen( range.Hyperfocal ) );
            const ImVec2 size = ImGui::CalcTextSize( "H" );
            const float pad = ImGuiTheme::Em( 0.15f );
            const ImRect rect( ImVec2( x - size.x * 0.5f - pad, map.Max.y - size.y - pad ), ImVec2( x + size.x * 0.5f + pad, map.Max.y ) );
            bool hovered = false;
            if( Graph::MarkerButton( "Hyperfocal", rect, &hovered ) )
            {
                // Rounded up, so the far limit really is infinity.
                double focus = Significant( range.Hyperfocal );
                if( focus < range.Hyperfocal )
                {
                    focus += std::pow( 10.0, std::floor( std::log10( range.Hyperfocal ) ) - 2.0 );
                }
                if( focus != InOutFocus )
                {
                    InOutFocus = focus;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, map.Max.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::Meters, Significant( range.Hyperfocal ) ) );
            if( hovered )
            {
                ImGui::SetTooltip( "Hyperfocal distance %s: focused here, everything from %s to infinity is sharp",
                    FormatValue( Unit::Meters, Significant( range.Hyperfocal ) ).c_str(), FormatValue( Unit::Meters, Significant( range.Hyperfocal * 0.5 ) ).c_str() );
            }
            if( visible )
            {
                const float dash = ImGuiTheme::Em( 0.2f );
                for( float y = map.Min.y; y < rect.Min.y; y += dash * 2.f )
                {
                    drawList->AddLine( ImVec2( x, y ), ImVec2( x, std::min( y + dash, rect.Min.y ) ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.4f ) ), Thin() );
                }
                drawList->AddText( ImVec2( x - size.x * 0.5f, rect.Min.y + pad * 0.5f ), U32( hovered ? palette.Text : palette.Muted ), "H" );
            }
        }

        double ticks[32];
        Graph::HandleSpec spec;
        spec.MinX = xAxis.Min;
        spec.MaxX = xAxis.Max;
        spec.Detents = ticks;
        spec.DetentCount = Graph::Ticks( xAxis, ticks, 32 );
        double focus = InOutFocus;
        if( Graph::DragHandle( "Focus", &focus, nullptr, spec ) )
        {
            focus = Significant( std::clamp( focus, xAxis.Min, xAxis.Max ) );
            if( focus != InOutFocus )
            {
                InOutFocus = focus;
                changed = true;
            }
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Pressed || ( input.Held && GImGui->IO.MouseDelta.x != 0.f ) )
        {
            const double next = Significant( std::clamp( input.X, xAxis.Min, xAxis.Max ), 2 );
            if( next != InOutFocus )
            {
                InOutFocus = next;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "What stays sharp, for a %s lens at %s (%s), circle of confusion %s.\n"
                               "Drag Focus or click the bar to focus (log scale); H focuses at the hyperfocal distance.",
                FormatValue( Unit::Millimeters, Significant( InFocalLength ) ).c_str(), FormatValue( Unit::FStop, InFStop ).c_str(),
                InLensNote ? InLensNote : "", FormatValue( Unit::Millimeters, Significant( coc, 2 ) ).c_str() );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        ItemLog::SetItemValue( canvasId, describe( Lens::DepthOfField( InFocalLength, InFStop, InOutFocus, coc ) ) );
        ImGui::PopID();
        return changed;
    }
}
