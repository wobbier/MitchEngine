#include "MaterialWidgets.h"
#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "PostWidgets.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace MeWidgets
{
    // ---- Math ---------------------------------------------------------------------------------

    namespace MaterialMath
    {
        namespace
        {
            double Lerp( double InA, double InB, double InT )
            {
                return InA + ( InB - InA ) * InT;
            }
        }

        double IntensityToEV( double InIntensity )
        {
            if( !( InIntensity > 0.0 ) )
            {
                return kOffEV;
            }
            // A lit intensity under the bottom stays just above Off, so typing "off" or clicking the
            // strip's Off end still changes it to a true 0.
            return std::max( std::log2( InIntensity ), kDimEV );
        }

        double EVToIntensity( double InEV )
        {
            return InEV <= kOffEV ? 0.0 : std::exp2( InEV );
        }

        double MultiplierToEV( double InMultiplier )
        {
            if( InMultiplier == 0.0 )
            {
                return -std::numeric_limits<double>::infinity();
            }
            // Negative (or NaN) multipliers aren't intensities: NaN makes the field refuse them.
            return InMultiplier > 0.0 ? std::log2( InMultiplier ) : std::numeric_limits<double>::quiet_NaN();
        }

        NumberSpec EmissiveSpec()
        {
            NumberSpec spec;
            spec.FieldUnit = Unit::EV;
            spec.HasRange = true;
            spec.Min = kOffEV;
            spec.Max = kMaxEV;
            spec.HasSoftRange = true;
            spec.SoftMin = -3.0;
            spec.SoftMax = 7.0;
            spec.HasDefault = true;
            spec.Default = 0.0;
            spec.Decimals = 1;
            spec.MinText = "Off";
            spec.AlternateUnit = Unit::Multiplier;
            spec.AlternateToStored = &MultiplierToEV;
            return spec;
        }

        double CellValue( int InCell )
        {
            return static_cast<double>( std::clamp( InCell, 0, kSwatchCells - 1 ) ) / ( kSwatchCells - 1 );
        }

        int NearestCell( double InValue )
        {
            return static_cast<int>( std::lround( std::clamp( InValue, 0.0, 1.0 ) * ( kSwatchCells - 1 ) ) );
        }

        double SnapToCell( double InValue )
        {
            return CellValue( NearestCell( InValue ) );
        }

        const char* LooksLike( double InMetallic, double InRoughness )
        {
            if( InMetallic >= 0.5 )
            {
                if( InRoughness < 0.2 ) return "Polished metal";
                if( InRoughness < 0.45 ) return "Satin metal";
                if( InRoughness < 0.7 ) return "Brushed metal";
                return "Cast iron";
            }
            if( InMetallic > 0.15 )
            {
                return "Dusty metal";
            }
            if( InRoughness < 0.15 ) return "Lacquer";
            if( InRoughness < 0.4 ) return "Glossy plastic";
            if( InRoughness < 0.65 ) return "Satin";
            if( InRoughness < 0.85 ) return "Matte";
            return "Chalk";
        }

        SphereShade ShadeSphere( const float* InBaseSrgb, double InMetallic, double InRoughness )
        {
            const double metallic = std::clamp( InMetallic, 0.0, 1.0 );
            const double roughness = std::clamp( InRoughness, 0.0, 1.0 );
            SphereShade shade;
            for( int i = 0; i < 3; ++i )
            {
                const double base = PostMath::SrgbDecode( std::clamp( static_cast<double>( InBaseSrgb[i] ), 0.0, 1.0 ) );
                // Specular reflectance: 4% for dielectrics, the base colour for metals.
                const double f0 = Lerp( 0.04, base, metallic );
                // A dielectric's lit side is its albedo; a metal's is the environment it reflects
                // (a dim grey sky), tinted by f0, blurrier and darker as it roughens.
                const double dielectricBody = base * 0.8 + 0.04 * 0.5;
                const double metalBody = f0 * Lerp( 0.3, 0.42, roughness );
                shade.Body[i] = Lerp( dielectricBody, metalBody, metallic );
                // The edge: dark for dielectrics; smooth metals catch a bright rim of reflected
                // sky in their own colour (Fresnel), which roughness blurs away.
                const double dielectricRim = base * 0.1;
                const double metalRim = Lerp( f0, 1.0, 0.15 * ( 1.0 - roughness ) ) * Lerp( 0.9, 0.12, roughness );
                shade.Rim[i] = Lerp( dielectricRim, metalRim, metallic );
                // The light's reflection: white on dielectrics, the base colour on metals (bright
                // past 1, so it rolls towards white like a real hot spot and outshines the rim).
                shade.Spec[i] = Lerp( 1.0, base * 2.5, metallic );
            }
            // Rough surfaces spread the same light over a wider, dimmer highlight.
            shade.HighlightRadius = 0.12 + 0.6 * roughness;
            shade.HighlightPeak = 0.06 + 0.94 * std::pow( 1.0 - roughness, 1.6 ) * Lerp( 0.75, 1.0, metallic );
            shade.LightOffset = 0.42 - 0.12 * roughness;
            return shade;
        }

        void ToDisplay( const double* InLinear, float* OutSrgb )
        {
            // Reinhard on the part above 1 only: everything 0..1 shows as authored, brighter
            // values roll off towards white (an 8x red still reads as red, then pink).
            for( int i = 0; i < 3; ++i )
            {
                OutSrgb[i] = static_cast<float>( PostMath::SrgbEncode( std::clamp( InLinear[i], 0.0, 1.0 ) ) );
            }
            const double peak = std::max( { InLinear[0], InLinear[1], InLinear[2], 0.0 } );
            if( peak > 1.0 )
            {
                const double over = peak - 1.0;
                // Gentle: 2x is a fifth of the way to white, 16x three quarters.
                const double whiten = over / ( 4.0 + over );
                for( int i = 0; i < 3; ++i )
                {
                    const double c = std::clamp( InLinear[i] / peak, 0.0, 1.0 );
                    OutSrgb[i] = static_cast<float>( PostMath::SrgbEncode( Lerp( c, 1.0, whiten ) ) );
                }
            }
        }

        void EmissiveDisplay( const float* InColorSrgb, double InIntensity, float* OutSrgb )
        {
            double linear[3];
            for( int i = 0; i < 3; ++i )
            {
                linear[i] = PostMath::SrgbDecode( std::clamp( static_cast<double>( InColorSrgb[i] ), 0.0, 1.0 ) ) * std::max( InIntensity, 0.0 );
            }
            ToDisplay( linear, OutSrgb );
        }

        double BumpHeight( double InU, double InV )
        {
            const double du = InU - std::floor( InU ) - 0.5;
            const double dv = InV - std::floor( InV ) - 0.5;
            const double d2 = ( du * du + dv * dv ) / ( 0.42 * 0.42 );
            return d2 >= 1.0 ? 0.0 : ( 1.0 - d2 ) * ( 1.0 - d2 );
        }

        double BumpLight( double InU, double InV, double InStrength )
        {
            // Analytic slope of the dome (1 - d2)^2: dh/du = -4 (1 - d2) du / r^2.
            const double du = InU - std::floor( InU ) - 0.5;
            const double dv = InV - std::floor( InV ) - 0.5;
            constexpr double kRadius2 = 0.42 * 0.42;
            const double d2 = ( du * du + dv * dv ) / kRadius2;
            double slopeU = 0.0;
            double slopeV = 0.0;
            if( d2 < 1.0 )
            {
                slopeU = -4.0 * ( 1.0 - d2 ) * du / kRadius2;
                slopeV = -4.0 * ( 1.0 - d2 ) * dv / kRadius2;
            }
            // Bump depth: a quarter of the cell at strength 1.
            const double depth = 0.25 * std::max( InStrength, 0.0 );
            double n[3] = { -slopeU * depth, -slopeV * depth, 1.0 };
            const double length = std::sqrt( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
            // Light from the upper left and in front (V grows downwards, like the screen).
            const double light[3] = { -0.48, -0.48, 0.73 };
            const double lambert = std::max( 0.0, ( n[0] * light[0] + n[1] * light[1] + n[2] * light[2] ) / length );
            return std::clamp( 0.15 + 0.85 * lambert, 0.0, 1.0 );
        }
    }

    // ---- Shared -------------------------------------------------------------------------------

    namespace
    {
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
            return ImGui::GetColorU32( ImVec4( InSrgb[0], InSrgb[1], InSrgb[2], InAlpha ) );
        }

        ImU32 LinearU32( const double* InLinear, float InAlpha = 1.f )
        {
            float srgb[3];
            MaterialMath::ToDisplay( InLinear, srgb );
            return SrgbU32( srgb, InAlpha );
        }

        double RoundTo( double InValue, double InStep )
        {
            const double rounded = std::round( InValue / InStep ) * InStep;
            return rounded == 0.0 ? 0.0 : rounded;
        }

        bool Crossed( double InFrom, double InTo, double InDetent )
        {
            return ( InFrom < InDetent && InTo >= InDetent ) || ( InFrom > InDetent && InTo <= InDetent );
        }

        // Reports an edit on the item making it (only the active item, or any when none is, may).
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

        bool DoubleClickedThisFrame()
        {
            const ImGuiContext& g = *GImGui;
            return g.IO.MouseClicked[ImGuiMouseButton_Left] && g.IO.MouseClickedCount[ImGuiMouseButton_Left] == 2;
        }

        // A double-click on the last item whose first click landed on it too (a click elsewhere
        // can move the puck under the pointer: that second click is not a reset).
        bool DoubleClickedLastItem()
        {
            const ImGuiContext& g = *GImGui;
            const bool hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID key = ImHashStr( "##FirstClick", 0, g.LastItemData.ID );
            if( g.IO.MouseClicked[ImGuiMouseButton_Left] && g.IO.MouseClickedCount[ImGuiMouseButton_Left] == 1 )
            {
                storage->SetBool( key, hovered );
            }
            return hovered && DoubleClickedThisFrame() && storage->GetBool( key, false );
        }

        void ResetFeedback( ImGuiID InId, ImVec2 InPos )
        {
            Juice::Flash( InId );
            Juice::Burst( InPos, U32( ImGuiTheme::AccentText() ), 10 );
            Juice::Play( Juice::Cue::Reset );
        }

        std::string Hundredths( double InValue )
        {
            return FormatNumber( RoundTo( InValue, 0.01 ), 2 );
        }

        // A shaded sphere: gradient circles from the rim to the lit side, then the highlight.
        void DrawSphere( ImDrawList* InDrawList, ImVec2 InCentre, float InRadius, const MaterialMath::SphereShade& InShade )
        {
            const ImVec2 light( -0.62f, -0.78f );
            constexpr int kBodyRings = 9;
            for( int k = 0; k < kBodyRings; ++k )
            {
                const float t = static_cast<float>( k ) / ( kBodyRings - 1 );
                const float radius = InRadius * ( 1.f - 0.86f * t );
                const ImVec2 centre( InCentre.x + light.x * InRadius * 0.3f * t, InCentre.y + light.y * InRadius * 0.3f * t );
                const double mix = std::pow( static_cast<double>( t ), 0.7 );
                double colour[3];
                for( int i = 0; i < 3; ++i )
                {
                    colour[i] = InShade.Rim[i] + ( InShade.Body[i] - InShade.Rim[i] ) * mix;
                }
                InDrawList->AddCircleFilled( centre, radius, LinearU32( colour ) );
            }
            constexpr int kSpecRings = 7;
            const float highlight = InRadius * static_cast<float>( InShade.HighlightRadius );
            const ImVec2 spot( InCentre.x + light.x * InRadius * static_cast<float>( InShade.LightOffset ), InCentre.y + light.y * InRadius * static_cast<float>( InShade.LightOffset ) );
            // Each ring adds a share of the peak, so the centre reaches it and the edge fades out.
            const float share = 1.f - std::pow( 1.f - std::clamp( static_cast<float>( InShade.HighlightPeak ), 0.f, 0.999f ), 1.f / kSpecRings );
            for( int k = 0; k < kSpecRings; ++k )
            {
                const float t = static_cast<float>( k ) / ( kSpecRings - 1 );
                const float radius = std::max( highlight * ( 1.f - 0.85f * t ), Thin() * 0.5f );
                InDrawList->AddCircleFilled( spot, radius, LinearU32( InShade.Spec, share ) );
            }
        }
    }

    // ---- Metallic x Roughness pad -------------------------------------------------------------

    bool MetalRoughPad( const char* InCaption, double& InOutMetallic, double& InOutRoughness, const float* InBaseSrgb )
    {
        using namespace MaterialMath;
        ImGui::PushID( InCaption );
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        // Square-ish: the cells as wide as they are tall, within a sensible height.
        const float padHeight = std::round( std::clamp( width * 0.8f, ImGuiTheme::Em( 8.f ), ImGuiTheme::Em( 13.f ) ) );
        const float captionHeight = ImGui::GetFontSize() + ImGuiTheme::Em( 0.25f );
        const float height = padHeight + captionHeight;
        // Cell centres sit on quarters: the axes run half a cell past 0 and 1.
        const double half = 0.5 / ( kSwatchCells - 1 );
        const Graph::Axis xAxis { -half, 1.0 + half, false, Unit::None };
        const double span = 1.0 + 2.0 * half;
        const Graph::Axis yAxis { -half - span * captionHeight / padHeight, 1.0 + half, false, Unit::None };
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), xAxis, yAxis, frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect pad( map.Min, ImVec2( map.Max.x, map.Min.y + padHeight ) );
        bool changed = false;

        if( visible )
        {
            drawList->AddRectFilled( pad.Min, pad.Max, U32( palette.Recessed ), ImGui::GetStyle().FrameRounding );
            const float cellWidth = pad.GetWidth() / kSwatchCells;
            const float cellHeight = pad.GetHeight() / kSwatchCells;
            const float radius = 0.42f * std::min( cellWidth, cellHeight );
            const int currentRow = NearestCell( InOutMetallic );
            const int currentColumn = NearestCell( InOutRoughness );
            const bool onCell = std::abs( CellValue( currentRow ) - InOutMetallic ) < 1e-4 && std::abs( CellValue( currentColumn ) - InOutRoughness ) < 1e-4;
            for( int row = 0; row < kSwatchCells; ++row )
            {
                for( int column = 0; column < kSwatchCells; ++column )
                {
                    const ImVec2 centre( std::round( map.XToScreen( CellValue( column ) ) ), std::round( map.YToScreen( CellValue( row ) ) ) );
                    DrawSphere( drawList, centre, radius, ShadeSphere( InBaseSrgb, CellValue( row ), CellValue( column ) ) );
                    if( onCell && row == currentRow && column == currentColumn )
                    {
                        drawList->AddCircle( centre, radius + Thin() * 2.f, U32( palette.AccentText ), 0, Thin() * 1.5f );
                    }
                }
            }
            drawList->AddRect( pad.Min, pad.Max, U32( palette.Separator ), ImGui::GetStyle().FrameRounding, 0, Thin() );
            // Captions: the axes' ends, and what the puck's surface reads as.
            const float y = pad.Max.y + ImGuiTheme::Em( 0.15f );
            const ImU32 muted = U32( palette.Muted );
            drawList->AddText( ImVec2( pad.Min.x, y ), muted, "Smooth" );
            drawList->AddText( ImVec2( pad.Max.x - ImGui::CalcTextSize( "Rough" ).x, y ), muted, "Rough" );
            // The metallic axis' ends, in the label column beside the top and bottom rows (below
            // the row's caption), clipped to the column.
            {
                const float right = pad.Min.x - ImGuiTheme::InnerGap();
                const float fontSize = ImGui::GetFontSize();
                const float captionBottom = rowStart.y + ImGui::GetFrameHeight();
                const float metalY = std::round( std::max( map.YToScreen( 1.0 ) - fontSize * 0.5f, captionBottom ) );
                const float dielectricY = std::round( std::max( map.YToScreen( 0.0 ) - fontSize * 0.5f, metalY + fontSize ) );
                if( right > rowStart.x )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, palette.Muted );
                    ImGui::RenderTextEllipsis( drawList, ImVec2( rowStart.x, metalY ), ImVec2( right, metalY + fontSize ), right, right, "Metal", nullptr, nullptr );
                    ImGui::RenderTextEllipsis( drawList, ImVec2( rowStart.x, dielectricY ), ImVec2( right, dielectricY + fontSize ), right, right, "Dielectric", nullptr, nullptr );
                    ImGui::PopStyleColor();
                }
            }
            const char* looks = LooksLike( InOutMetallic, InOutRoughness );
            drawList->AddText( ImVec2( std::round( pad.GetCenter().x - ImGui::CalcTextSize( looks ).x * 0.5f ), y ), U32( palette.Text ), looks );
        }

        Graph::HandleSpec spec;
        spec.MinX = 0.0;
        spec.MaxX = 1.0;
        spec.MinY = 0.0;
        spec.MaxY = 1.0;
        spec.ShowValue = false;
        double roughness = std::clamp( InOutRoughness, 0.0, 1.0 );
        double metallic = std::clamp( InOutMetallic, 0.0, 1.0 );
        const bool moved = Graph::DragHandle( "Puck", &roughness, &metallic, spec );
        const ImGuiID puckId = ImGui::GetItemID();
        const bool reset = DoubleClickedLastItem();
        const Graph::CanvasInput input = Graph::End();
        const ImGuiContext& g = *GImGui;
        // Each axis follows the pointer only while the pointer moves along it: a drag straight
        // across leaves the other value exactly as typed.
        bool moveX = moved && g.IO.MouseDelta.x != 0.f;
        bool moveY = moved && g.IO.MouseDelta.y != 0.f;
        const bool onPad = input.Y >= yAxis.Min + span * captionHeight / padHeight - 1e-9;
        if( !reset && onPad && ( input.Pressed || ( input.Held && ( g.IO.MouseDelta.x != 0.f || g.IO.MouseDelta.y != 0.f ) ) ) )
        {
            roughness = std::clamp( input.X, 0.0, 1.0 );
            metallic = std::clamp( input.Y, 0.0, 1.0 );
            moveX = input.Pressed || g.IO.MouseDelta.x != 0.f;
            moveY = input.Pressed || g.IO.MouseDelta.y != 0.f;
        }
        if( reset )
        {
            roughness = 0.5;
            metallic = 0.0;
            ResetFeedback( canvasId, map.ToScreen( roughness, metallic ) );
        }
        else
        {
            // Hundredths; Alt snaps to the sphere centres (and ticks on reaching one).
            const bool snap = g.IO.KeyAlt;
            roughness = moveX ? ( snap ? SnapToCell( roughness ) : RoundTo( roughness, 0.01 ) ) : InOutRoughness;
            metallic = moveY ? ( snap ? SnapToCell( metallic ) : RoundTo( metallic, 0.01 ) ) : InOutMetallic;
            if( snap && ( roughness != InOutRoughness || metallic != InOutMetallic ) )
            {
                Juice::Tick( g.ActiveId == puckId ? puckId : canvasId, map.ToScreen( roughness, metallic ) );
            }
        }
        if( roughness != InOutRoughness || metallic != InOutMetallic )
        {
            InOutRoughness = roughness;
            InOutMetallic = metallic;
            changed = true;
            MarkEdited( canvasId );
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Metallic (up: dielectric to metal) by Roughness (right: smooth to rough), as spheres in the base colour.\n"
                               "Dielectrics (plastic, wood, stone) show their colour with a white highlight; metals reflect in their colour.\n"
                               "Drag the puck or click a sphere, in hundredths; Alt snaps to the spheres. Double-click the puck for metallic 0, roughness 0.5." );
        }
        if( visible )
        {
            FlashOutline( drawList, pad, canvasId );
        }
        const std::string value = "metallic " + Hundredths( InOutMetallic ) + ", roughness " + Hundredths( InOutRoughness );
        ItemLog::SetItemValue( puckId, value );
        ItemLog::SetItemValue( canvasId, LooksLike( InOutMetallic, InOutRoughness ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Emissive strip -----------------------------------------------------------------------

    namespace
    {
        std::string IntensityText( double InEV )
        {
            return InEV <= MaterialMath::kOffEV ? std::string( "Off" ) : FormatValue( Unit::EV, InEV, 1 );
        }

        // "1x", "16x", "1/4x": an intensity at a whole stop.
        std::string StopMultiplier( int InEV )
        {
            if( InEV >= 0 )
            {
                return FormatNumber( std::exp2( InEV ), 0 ) + "\xC3\x97";
            }
            return "1/" + FormatNumber( std::exp2( -InEV ), 0 ) + "\xC3\x97";
        }
    }

    bool EmissiveStrip( const char* InCaption, double& InOutIntensity, const float* InColorSrgb )
    {
        using namespace MaterialMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float frameHeight = ImGui::GetFrameHeight();
        const float gap = ImGuiTheme::InnerGap();
        const float swatchSize = std::round( frameHeight * 1.6f );
        const float stripWidth = std::max( width - swatchSize - gap, frameHeight );
        const float stripHeight = std::round( frameHeight * 0.6f );
        const float labelTop = stripHeight + ImGuiTheme::Em( 0.15f );
        const float height = std::max( labelTop + ImGui::GetFontSize(), swatchSize );
        const Graph::Axis xAxis { kOffEV, kMaxEV, false, Unit::EV };
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const bool visible = Graph::Begin( InCaption, ImVec2( stripWidth, height ), xAxis, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect strip( map.Min, ImVec2( map.Max.x, map.Min.y + stripHeight ) );
        const double ev = IntensityToEV( InOutIntensity );
        double next = ev;
        bool changed = false;

        // The value's marker: pressing it doesn't move the value, so a double-click on it resets
        // to 1x in one step (a click on the strip itself jumps to the pointer).
        bool markerScrub = false;
        {
            const float x = std::round( map.XToScreen( ev ) );
            const float reach = ImGuiTheme::Em( 0.4f );
            const ImRect rect( ImVec2( x - reach, strip.Min.y ), ImVec2( x + reach, strip.Max.y ) );
            bool hovered = false;
            Graph::MarkerButton( "Value", rect, &hovered );
            markerScrub = ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) && GImGui->IO.MouseDelta.x != 0.f;
            if( DoubleClickedLastItem() )
            {
                next = 0.0;
                ResetFeedback( canvasId, ImVec2( map.XToScreen( 0.0 ), strip.GetCenter().y ) );
            }
            else if( hovered && !ImGui::IsItemActive() )
            {
                ImGui::SetTooltip( "%s: drag along the strip, double-click for 1\xC3\x97 (0 EV)", IntensityText( ev ).c_str() );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), IntensityText( ev ) );
        }

        const Graph::CanvasInput input = Graph::End();
        const ImGuiContext& g = *GImGui;
        if( next == ev && ( input.Pressed || markerScrub || ( input.Held && g.IO.MouseDelta.x != 0.f ) ) )
        {
            // Third stops; the first half stop is Off; 0 EV holds within a third of an em.
            const double at = std::clamp( map.XFromScreen( g.IO.MousePos.x ), kOffEV, kMaxEV );
            next = at < kOffEV + 0.5 ? kOffEV : std::max( RoundTo( at * 3.0, 1.0 ) / 3.0, kOffEV + 1.0 / 3.0 );
            next = std::round( next * 100.0 ) / 100.0;
            if( std::abs( map.XToScreen( 0.0 ) - g.IO.MousePos.x ) <= ImGuiTheme::Em( 0.35f ) )
            {
                next = 0.0;
            }
            if( !input.Pressed && ( Crossed( ev, next, 0.0 ) || ( next == kOffEV ) != ( ev == kOffEV ) ) )
            {
                Juice::Tick( canvasId, ImVec2( map.XToScreen( next ), strip.Max.y ) );
            }
        }
        if( next != ev )
        {
            InOutIntensity = EVToIntensity( next );
            changed = true;
            MarkEdited( canvasId );
        }
        const double shownEV = IntensityToEV( InOutIntensity );
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The emissive colour times its intensity, in stops: each stop doubles the glow.\n"
                               "Past 1\xC3\x97 it blooms towards white (shown tonemapped). The left end is Off (0).\n"
                               "Click or drag along the strip (third stops); double-click the value's marker for 1\xC3\x97." );
        }

        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( strip.Min, strip.Max, U32( palette.Control ), rounding );
            // The glow at each stop, as the screen shows it.
            const float offEnd = std::round( map.XToScreen( kOffEV + 0.5 ) );
            for( int stop = static_cast<int>( kOffEV ); stop < static_cast<int>( kMaxEV ); ++stop )
            {
                const double a = std::max( static_cast<double>( stop ), kOffEV + 0.5 );
                const double b = stop + 1.0;
                float ca[3];
                float cb[3];
                EmissiveDisplay( InColorSrgb, std::exp2( a ), ca );
                EmissiveDisplay( InColorSrgb, std::exp2( b ), cb );
                const ImU32 left = SrgbU32( ca );
                const ImU32 right = SrgbU32( cb );
                drawList->AddRectFilledMultiColor( ImVec2( std::floor( map.XToScreen( a ) ), strip.Min.y + Thin() ), ImVec2( std::ceil( map.XToScreen( b ) ), strip.Max.y - Thin() ), left, right, right, left );
            }
            // Whole stops as ticks, 0 EV (1x) full height; labels at the Off end, 1x and every 4 stops.
            const ImU32 tickColour = U32( ImGuiTheme::WithAlpha( palette.Text, 0.35f ) );
            drawList->AddLine( ImVec2( offEnd, strip.Min.y ), ImVec2( offEnd, strip.Max.y ), U32( palette.Separator ), Thin() );
            const float textY = map.Min.y + labelTop;
            drawList->AddText( ImVec2( strip.Min.x, textY ), U32( shownEV <= kOffEV ? palette.AccentText : palette.Muted ), "Off" );
            const float offWidth = ImGui::CalcTextSize( "Off" ).x;
            float lastRight = strip.Min.x + offWidth;
            for( int stop = static_cast<int>( kOffEV ) + 1; stop < static_cast<int>( kMaxEV ); ++stop )
            {
                const float x = std::round( map.XToScreen( stop ) );
                const float inset = stop == 0 ? 0.f : stripHeight * 0.3f;
                drawList->AddLine( ImVec2( x, strip.Max.y - inset ), ImVec2( x, strip.Max.y ), tickColour, Thin() );
                if( stop % 4 == 0 )
                {
                    const std::string text = StopMultiplier( stop );
                    const float textWidth = ImGui::CalcTextSize( text.c_str() ).x;
                    const float left = std::clamp( x - textWidth * 0.5f, strip.Min.x, strip.Max.x - textWidth );
                    if( left > lastRight + ImGuiTheme::Em( 0.4f ) )
                    {
                        drawList->AddText( ImVec2( std::round( left ), textY ), U32( std::abs( shownEV - stop ) < 1e-6 ? palette.AccentText : palette.Muted ), text.c_str() );
                        lastRight = left + textWidth;
                    }
                }
            }
            // The value: a line through the strip.
            const float x = std::round( map.XToScreen( shownEV ) );
            drawList->AddLine( ImVec2( x, strip.Min.y - Thin() ), ImVec2( x, strip.Max.y + Thin() ), U32( palette.Text ), Thin() * 2.f );
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddRectFilled( strip.Min, strip.Max, U32( ImGuiTheme::WithAlpha( palette.Hovered, 0.5f * tick ) ), rounding );
            }
            FlashOutline( drawList, strip, canvasId );
        }
        ItemLog::SetItemValue( canvasId, IntensityText( shownEV ) );

        // The swatch: the colour at this intensity, as the screen shows it.
        ImGui::SameLine( 0.f, 0.f );
        ImGui::SetCursorScreenPos( ImVec2( rowStart.x + width - swatchSize, rowStart.y ) );
        ImGui::InvisibleButton( "Swatch", ImVec2( swatchSize, swatchSize ) );
        // Named even while scrolled out of view (the test hook only labels visible buttons).
        ItemLog::SetLastItemLabel( "Swatch" );
        float shown[3];
        EmissiveDisplay( InColorSrgb, InOutIntensity, shown );
        char hex[16];
        std::snprintf( hex, sizeof( hex ), "#%02X%02X%02X", static_cast<int>( std::lround( shown[0] * 255.f ) ), static_cast<int>( std::lround( shown[1] * 255.f ) ), static_cast<int>( std::lround( shown[2] * 255.f ) ) );
        ItemLog::SetLastItemValue( hex );
        if( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "The glow as the screen shows it: the emissive colour at %s, tonemapped (%s)", IntensityText( shownEV ).c_str(), hex );
        }
        if( ImGui::IsItemVisible() )
        {
            const ImRect box( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( box.Min, box.Max, SrgbU32( shown ), rounding );
            drawList->AddRect( box.Min, box.Max, U32( palette.Separator ), rounding, 0, Thin() );
        }
        ImGui::PopID();
        return changed;
    }

    // ---- Bump preview -------------------------------------------------------------------------

    bool BumpPreview( const char* InCaption, double& InOutStrength, const NumberSpec& InSpec, const float* InBaseSrgb )
    {
        using namespace MaterialMath;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const float height = std::round( ImGuiTheme::Em( 2.6f ) );
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImRect bb( start, ImVec2( start.x + width, start.y + height ) );
        ImGui::ItemSize( bb, 0.f );
        const ImGuiID id = window->GetID( "Tile" );
        const bool visible = ImGui::ItemAdd( bb, id );
        {
            ImGuiContext& g = *GImGui;
            IMGUI_TEST_ENGINE_ITEM_INFO( id, "Tile", g.LastItemData.StatusFlags );
        }
        const bool changed = ScrubLastItem( InOutStrength, InSpec, width );
        const std::string value = FormatValue( Unit::Multiplier, InOutStrength, 1 );
        ItemLog::SetItemValue( id, value );
        if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) && !ImGui::IsItemActive() )
        {
            ImGui::SetTooltip( "The normal map's bumps at %s: 0 is flat, 1 as authored, 2 twice as deep.\n"
                               "Drag across to change it (Shift fine); double-click for 1\xC3\x97.", value.c_str() );
        }
        if( visible )
        {
            ImDrawList* drawList = window->DrawList;
            const float rounding = ImGui::GetStyle().FrameRounding;
            // A neutral tile tinted by the base colour, so dark or saturated bases still show relief.
            double tint[3];
            for( int i = 0; i < 3; ++i )
            {
                tint[i] = 0.5 * PostMath::SrgbDecode( 0.72 ) + 0.5 * PostMath::SrgbDecode( std::clamp( static_cast<double>( InBaseSrgb[i] ), 0.0, 1.0 ) );
            }
            // Bumps a little under the tile's height apart; shaded per vertex of a fine grid.
            const float bumpSize = height / 1.25f;
            const float cell = std::max( ImGuiTheme::Em( 0.18f ), 1.f );
            const int columns = std::max( 1, static_cast<int>( std::ceil( bb.GetWidth() / cell ) ) );
            const int rows = std::max( 1, static_cast<int>( std::ceil( bb.GetHeight() / cell ) ) );
            const float stepX = bb.GetWidth() / columns;
            const float stepY = bb.GetHeight() / rows;
            const double strength = std::max( InOutStrength, 0.0 );
            auto colourAt = [&]( int InColumn, int InRow ) {
                const double u = ( InColumn * stepX ) / bumpSize;
                const double v = ( InRow * stepY ) / bumpSize + 0.1;
                const double light = BumpLight( u, v, strength );
                double linear[3];
                for( int i = 0; i < 3; ++i )
                {
                    linear[i] = tint[i] * light;
                }
                return LinearU32( linear );
            };
            drawList->PushClipRect( bb.Min, bb.Max, true );
            for( int row = 0; row < rows; ++row )
            {
                for( int column = 0; column < columns; ++column )
                {
                    const ImVec2 a( bb.Min.x + column * stepX, bb.Min.y + row * stepY );
                    const ImVec2 b( bb.Min.x + ( column + 1 ) * stepX, bb.Min.y + ( row + 1 ) * stepY );
                    drawList->AddRectFilledMultiColor( a, b, colourAt( column, row ), colourAt( column + 1, row ), colourAt( column + 1, row + 1 ), colourAt( column, row + 1 ) );
                }
            }
            drawList->PopClipRect();
            const bool active = ImGui::IsItemActive();
            drawList->AddRect( bb.Min, bb.Max, U32( active || ImGui::IsItemHovered() ? palette.AccentText : palette.Separator ), rounding, 0, Thin() );
            // The strength, in a chip at the corner.
            const ImVec2 textSize = ImGui::CalcTextSize( value.c_str() );
            const float pad = ImGuiTheme::Em( 0.2f );
            const ImVec2 chipMin( bb.Max.x - textSize.x - pad * 3.f, bb.Max.y - textSize.y - pad * 2.f );
            drawList->AddRectFilled( chipMin, ImVec2( bb.Max.x - pad, bb.Max.y - pad ), U32( ImGuiTheme::WithAlpha( palette.Window, 0.85f ) ), rounding );
            drawList->AddText( ImVec2( chipMin.x + pad, chipMin.y + pad * 0.5f ), U32( palette.Text ), value.c_str() );
            const ImGuiID feedback = ImGui::GetID( "##value" );
            const float flash = Juice::FlashAmount( feedback );
            if( flash > 0.f )
            {
                drawList->AddRect( bb.Min, bb.Max, U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), rounding, 0, Thin() * 2.f );
            }
        }
        ImGui::PopID();
        return changed;
    }
}
