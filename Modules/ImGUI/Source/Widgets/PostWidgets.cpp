#include "PostWidgets.h"
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
#include <vector>

namespace MeWidgets
{
    // ---- Math ---------------------------------------------------------------------------------

    namespace PostMath
    {
        namespace
        {
            using Mat3 = double[3][3];

            void Mul( const Mat3& InM, const double* InV, double* OutV )
            {
                double r[3];
                for( int i = 0; i < 3; ++i )
                {
                    r[i] = InM[i][0] * InV[0] + InM[i][1] * InV[1] + InM[i][2] * InV[2];
                }
                std::copy( r, r + 3, OutV );
            }

            double Luma( const double* InRgb )
            {
                return InRgb[0] * 0.2126 + InRgb[1] * 0.7152 + InRgb[2] * 0.0722;
            }

            // PostCommon.sh tonemapAcesFitted (Stephen Hill).
            void Aces( const double* InRgb, double* OutRgb )
            {
                static const Mat3 inputMat = { { 0.59719, 0.35458, 0.04823 }, { 0.07600, 0.90834, 0.01566 }, { 0.02840, 0.13383, 0.83777 } };
                static const Mat3 outputMat = { { 1.60475, -0.53108, -0.07367 }, { -0.10208, 1.10813, -0.00605 }, { -0.00327, -0.07276, 1.07602 } };
                double v[3];
                Mul( inputMat, InRgb, v );
                for( double& c : v )
                {
                    const double a = c * ( c + 0.0245786 ) - 0.000090537;
                    const double b = c * ( 0.983729 * c + 0.4329510 ) + 0.238081;
                    c = a / b;
                }
                Mul( outputMat, v, OutRgb );
                for( int i = 0; i < 3; ++i )
                {
                    OutRgb[i] = std::clamp( OutRgb[i], 0.0, 1.0 );
                }
            }

            // PostCommon.sh tonemapAgX (Sobotka, Wrensch's fit), back to linear for the shared encode.
            void AgX( const double* InRgb, double* OutRgb )
            {
                static const Mat3 agxMat = { { 0.842479062253094, 0.0784335999999992, 0.0792237451477643 },
                    { 0.0423282422610123, 0.878468636469772, 0.0791661274605434 },
                    { 0.0423756549057051, 0.0784336, 0.879142973793104 } };
                static const Mat3 agxInvMat = { { 1.19687900512017, -0.0980208811401368, -0.0990297440797205 },
                    { -0.0528968517574562, 1.15190312990417, -0.0989611768448433 },
                    { -0.0529716355144438, -0.0980434501171241, 1.15107367264116 } };
                constexpr double kMinEv = -12.47393;
                constexpr double kMaxEv = 4.026069;
                double x[3] = { std::max( InRgb[0], 1e-10 ), std::max( InRgb[1], 1e-10 ), std::max( InRgb[2], 1e-10 ) };
                Mul( agxMat, x, x );
                for( double& c : x )
                {
                    c = std::clamp( ( std::log2( c ) - kMinEv ) / ( kMaxEv - kMinEv ), 0.0, 1.0 );
                    const double x2 = c * c;
                    const double x4 = x2 * x2;
                    c = 15.5 * x4 * x2 - 40.14 * x4 * c + 31.96 * x4 - 6.868 * x2 * c + 0.4298 * x2 + 0.1191 * c - 0.00232;
                }
                Mul( agxInvMat, x, x );
                for( int i = 0; i < 3; ++i )
                {
                    OutRgb[i] = std::pow( std::max( x[i], 0.0 ), 2.2 );
                }
            }

            double SignedPow( double InValue, double InPower )
            {
                return InValue < 0.0 ? -std::pow( -InValue, InPower ) : std::pow( InValue, InPower );
            }

            double Contrasted( const Grade& InGrade, double InLinear )
            {
                const double c = ( InLinear - 0.18 ) * InGrade.Contrast + 0.18;
                return c * InGrade.Gain + InGrade.Lift * ( 1.0 - c );
            }

            double InverseGamma( const Grade& InGrade )
            {
                return 1.0 / std::max( InGrade.Gamma, 0.01 );
            }
        }

        const char* TonemapperName( int InOperator )
        {
            static const char* names[kTonemapperCount] = { "None", "ACES", "AgX", "Reinhard" };
            return names[std::clamp( InOperator, 0, kTonemapperCount - 1 )];
        }

        void Tonemap( int InOperator, const double* InRgb, double* OutRgb )
        {
            // Tonemap.frag: op > 2.5 Reinhard, > 1.5 AgX, > 0.5 ACES, else none; then clamp.
            double out[3] = { InRgb[0], InRgb[1], InRgb[2] };
            if( InOperator >= 3 )
            {
                const double l = Luma( InRgb );
                for( double& c : out )
                {
                    c = c / ( 1.0 + l );
                }
            }
            else if( InOperator == 2 )
            {
                AgX( InRgb, out );
            }
            else if( InOperator == 1 )
            {
                Aces( InRgb, out );
            }
            for( int i = 0; i < 3; ++i )
            {
                OutRgb[i] = std::clamp( out[i], 0.0, 1.0 );
            }
        }

        double TonemapGrey( int InOperator, double InLuminance )
        {
            const double grey[3] = { InLuminance, InLuminance, InLuminance };
            double out[3];
            Tonemap( InOperator, grey, out );
            return Luma( out );
        }

        double SrgbEncode( double InLinear )
        {
            return InLinear <= 0.0031308 ? InLinear * 12.92 : std::pow( std::abs( InLinear ), 1.0 / 2.4 ) * 1.055 - 0.055;
        }

        double SrgbDecode( double InEncoded )
        {
            return InEncoded <= 0.04045 ? InEncoded / 12.92 : std::pow( ( InEncoded + 0.055 ) / 1.055, 2.4 );
        }

        double DisplayGrey( int InOperator, double InLuminance, double InExposureEV )
        {
            return SrgbEncode( std::clamp( TonemapGrey( InOperator, InLuminance * std::exp2( InExposureEV ) ), 0.0, 1.0 ) );
        }

        double GradeSigned( const Grade& InGrade, double InLinear )
        {
            return SignedPow( Contrasted( InGrade, InLinear ), InverseGamma( InGrade ) );
        }

        double GradeChannel( const Grade& InGrade, double InLinear )
        {
            return std::clamp( std::pow( std::max( Contrasted( InGrade, InLinear ), 0.0 ), InverseGamma( InGrade ) ), 0.0, 1.0 );
        }

        bool SolveLift( const Grade& InGrade, double InTarget, double& OutLift )
        {
            // At black: c = b gain + lift (1 - b), b = 0.18 (1 - contrast).
            const double b = 0.18 * ( 1.0 - InGrade.Contrast );
            if( std::abs( 1.0 - b ) < 1e-6 )
            {
                return false;
            }
            const double c = SignedPow( InTarget, std::max( InGrade.Gamma, 0.01 ) );
            OutLift = ( c - b * InGrade.Gain ) / ( 1.0 - b );
            return std::isfinite( OutLift );
        }

        bool SolveGain( const Grade& InGrade, double InTarget, double& OutGain )
        {
            // At white: c = b gain + lift (1 - b), b = 0.82 contrast + 0.18.
            const double b = 0.82 * InGrade.Contrast + 0.18;
            if( std::abs( b ) < 1e-6 )
            {
                return false;
            }
            const double c = SignedPow( InTarget, std::max( InGrade.Gamma, 0.01 ) );
            OutGain = ( c - InGrade.Lift * ( 1.0 - b ) ) / b;
            return std::isfinite( OutGain );
        }

        bool SolveGamma( const Grade& InGrade, double InAt, double InTarget, double& OutGamma )
        {
            // c^(1 / gamma) = target, for c and target strictly inside 0..1.
            const double c = Contrasted( InGrade, InAt );
            if( !( c > 1e-6 && c < 1.0 - 1e-6 && InTarget > 1e-6 && InTarget < 1.0 - 1e-6 ) )
            {
                return false;
            }
            OutGamma = std::log( c ) / std::log( InTarget );
            return std::isfinite( OutGamma ) && OutGamma > 0.0;
        }

        void WhiteBalanceScale( double InTemperature, double InTint, double* OutScale )
        {
            OutScale[0] = 1.0 + 0.2 * InTemperature;
            OutScale[1] = 1.0 - 0.1 * InTint;
            OutScale[2] = 1.0 - 0.2 * InTemperature;
        }

        double FogOpacity( double InDistance, double InDensity, double InStart, double InMaxOpacity, double InHeightFactor )
        {
            if( InDistance <= InStart )
            {
                return 0.0;
            }
            const double opacity = 1.0 - std::exp( -std::max( InDensity * InHeightFactor, 0.0 ) * ( InDistance - InStart ) );
            return std::min( opacity, std::clamp( InMaxOpacity, 0.0, 1.0 ) );
        }

        double FogVisibility( double InDensity, double InStart, double InMaxOpacity, double InHeightFactor )
        {
            const double density = InDensity * InHeightFactor;
            if( !( density > 0.0 ) || InMaxOpacity < 0.95 )
            {
                return std::numeric_limits<double>::infinity();
            }
            return std::max( InStart, 0.0 ) + std::log( 20.0 ) / density;
        }

        bool FogDensityFor( double InVisibility, double InStart, double& OutDensity, double InHeightFactor )
        {
            if( !std::isfinite( InVisibility ) )
            {
                OutDensity = 0.0;
                return true;
            }
            const double span = InVisibility - std::max( InStart, 0.0 );
            if( !( span > 0.0 ) || !( InHeightFactor > 0.0 ) )
            {
                return false;
            }
            OutDensity = std::log( 20.0 ) / ( span * InHeightFactor );
            return true;
        }

        double FogHeightFactor( double InY, double InFogHeight, double InFalloff )
        {
            return std::exp( std::min( -InFalloff * ( InY - InFogHeight ), 8.0 ) );
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

        double Significant( double InValue, int InDigits = 3 )
        {
            if( !( std::abs( InValue ) > 0.0 ) )
            {
                return 0.0;
            }
            const double power = std::pow( 10.0, std::floor( std::log10( std::abs( InValue ) ) ) - ( InDigits - 1 ) );
            return std::round( InValue / power ) * power;
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

        // A marker's spring: follows a value this widget just set (flash), snaps to anything else.
        float Follow( ImGuiID InId, float InTarget, ImGuiID InFlashId )
        {
            if( Juice::FlashAmount( InFlashId ) <= 0.f && !Juice::IsMoving( InId ) )
            {
                Juice::Snap( InId, InTarget );
            }
            return Juice::Spring( InId, InTarget );
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

        // The second press of a double-click, whoever owns the mouse button (a handle's own
        // ButtonBehavior takes it, which ImGui::IsMouseDoubleClicked would respect).
        bool DoubleClickedThisFrame()
        {
            const ImGuiContext& g = *GImGui;
            return g.IO.MouseClicked[ImGuiMouseButton_Left] && g.IO.MouseClickedCount[ImGuiMouseButton_Left] == 2;
        }

        // A double-click on the last item whose first click landed on it too. A click elsewhere
        // can move an item under the pointer (the pad's puck, the strip's marker): the second
        // click of that double-click is not a reset.
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

        // A detent at InDetent: the value snaps to it when a change crosses it, and holds there
        // until the pointer leaves InReach (in value units). Every other value stays reachable.
        double Detent( double InPrevious, double InNext, double InDetent, double InReach )
        {
            const bool held = InPrevious == InDetent;
            if( ( held || Crossed( InPrevious, InNext, InDetent ) ) && std::abs( InNext - InDetent ) < InReach )
            {
                return InDetent;
            }
            return InNext;
        }

        // A value reset by double-click: flash, sparks and the reset cue.
        void ResetFeedback( ImGuiID InId, ImVec2 InPos )
        {
            Juice::Flash( InId );
            Juice::Burst( InPos, U32( ImGuiTheme::AccentText() ), 10 );
            Juice::Play( Juice::Cue::Reset );
        }

        // Labels along a strip, left to right, in one row or staggered over two when one row is
        // too crowded; a label that still overlaps becomes a dot (still clickable).
        struct MarkerLayout
        {
            float LastRight[2] = { -1e9f, -1e9f };
            int Rows = 1;
            float RowStep = 0.f;

            ImRect Place( float InX, float InTop, const char* InText, float InMinX, float InMaxX, bool& OutFits )
            {
                const ImVec2 size = ImGui::CalcTextSize( InText );
                const float left = std::clamp( InX - size.x * 0.5f, InMinX, std::max( InMinX, InMaxX - size.x ) );
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

        void DashedVertical( ImDrawList* InDrawList, float InX, float InTop, float InBottom, ImU32 InColor )
        {
            const float dash = ImGuiTheme::Em( 0.25f );
            for( float y = InTop; y < InBottom; y += dash * 2.f )
            {
                InDrawList->AddLine( ImVec2( InX, y ), ImVec2( InX, std::min( y + dash, InBottom ) ), InColor, Thin() );
            }
        }

        void DashedHorizontal( ImDrawList* InDrawList, float InLeft, float InRight, float InY, ImU32 InColor )
        {
            const float dash = ImGuiTheme::Em( 0.25f );
            for( float x = InLeft; x < InRight; x += dash * 2.f )
            {
                InDrawList->AddLine( ImVec2( x, InY ), ImVec2( std::min( x + dash, InRight ), InY ), InColor, Thin() );
            }
        }

        ImU32 LinearToU32( const double* InLinear, float InAlpha = 1.f )
        {
            return ImGui::GetColorU32( ImVec4( static_cast<float>( PostMath::SrgbEncode( std::clamp( InLinear[0], 0.0, 1.0 ) ) ),
                static_cast<float>( PostMath::SrgbEncode( std::clamp( InLinear[1], 0.0, 1.0 ) ) ),
                static_cast<float>( PostMath::SrgbEncode( std::clamp( InLinear[2], 0.0, 1.0 ) ) ), InAlpha ) );
        }
    }

    // ---- Exposure ---------------------------------------------------------------------------

    namespace
    {
        struct Rung
        {
            const char* Name;
            double EV;
            const char* Note;
        };
        // Looks-like references for the engine's (non-photometric) lights: the Light inspector's
        // Sun rung is 5x and Overcast 1x, so a sunlit scene wants about 2 to 3 stops less.
        const Rung kExposureRungs[] = {
            { "Sunlight", -3.0, "Hard midday sun (a Sun light at 5x or more): stop down" },
            { "Overcast", 0.0, "A cloudy day, or the default 1x lights: no compensation" },
            { "Indoor", 2.0, "A room lit by bulbs and windows" },
            { "Night street", 5.0, "Street lamps and moonlight: open right up" },
        };
        constexpr double kLadderMin = -6.0;
        constexpr double kLadderMax = 6.0;
    }

    bool ExposureLadder( const char* InCaption, double& InOutEV, const NumberSpec& InSpec )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const Graph::Axis xAxis { kLadderMin, kLadderMax, false, Unit::EV };
        const float stripHeight = std::round( ImGui::GetFrameHeight() * 0.5f );
        const float labelTop = stripHeight + ImGuiTheme::Em( 0.2f );

        // One row of rung names when they fit side by side, else two.
        MarkerLayout probe;
        int rows = 1;
        for( const Rung& rung : kExposureRungs )
        {
            bool fits = false;
            probe.Place( static_cast<float>( xAxis.ToNormalized( rung.EV ) ) * width, 0.f, rung.Name, 0.f, width, fits );
            rows = fits ? rows : 2;
        }
        MarkerLayout layout;
        layout.Rows = rows;
        layout.RowStep = ImGui::GetFontSize() + ImGuiTheme::Em( 0.05f );
        const float height = labelTop + ImGui::GetFontSize() + layout.RowStep * ( rows - 1 ) + ImGuiTheme::Em( 0.1f );

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), xAxis, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect strip( map.Min, ImVec2( map.Max.x, map.Min.y + stripHeight ) );
        bool changed = false;

        bool markerScrub = false;
        for( const Rung& rung : kExposureRungs )
        {
            const float x = std::round( map.XToScreen( rung.EV ) );
            bool fits = false;
            const ImRect rect = layout.Place( x, map.Min.y + labelTop, rung.Name, map.Min.x, map.Max.x, fits );
            bool hovered = false;
            // A press dragged past the threshold is a scrub, not a click (it may be let go over the rung).
            const bool clicked = Graph::MarkerButton( rung.Name, rect, &hovered ) && GImGui->IO.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < GImGui->IO.MouseDragThreshold * GImGui->IO.MouseDragThreshold;
            // A rung pressed and dragged scrubs the strip, like the strip itself.
            markerScrub |= ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) && GImGui->IO.MouseDelta.x != 0.f;
            if( clicked )
            {
                const double next = Constrain( rung.EV, InSpec );
                if( next != InOutEV )
                {
                    InOutEV = next;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, strip.Max.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::EV, rung.EV ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s\n%s", rung.Name, FormatValue( Unit::EV, rung.EV ).c_str(), rung.Note );
            }
            if( visible )
            {
                const bool current = std::abs( InOutEV - rung.EV ) < 1e-3;
                drawList->AddLine( ImVec2( x, strip.Max.y ), ImVec2( x, rect.Min.y ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.25f ) ), Thin() );
                DrawMarker( drawList, rect, fits, rung.Name, U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted ) );
            }
        }

        // The value's marker on the strip: pressing it doesn't move the value, so a double-click
        // on it resets to 0 in one step (a click on the strip itself jumps to the pointer).
        {
            const float x = std::round( map.XToScreen( std::clamp( InOutEV, kLadderMin, kLadderMax ) ) );
            const float reach = ImGuiTheme::Em( 0.4f );
            const ImRect rect( ImVec2( x - reach, strip.Min.y ), ImVec2( x + reach, strip.Max.y ) );
            bool hovered = false;
            Graph::MarkerButton( "Value", rect, &hovered );
            markerScrub |= ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) && GImGui->IO.MouseDelta.x != 0.f;
            if( DoubleClickedLastItem() )
            {
                if( InOutEV != 0.0 )
                {
                    InOutEV = Constrain( 0.0, InSpec );
                    changed = true;
                }
                ResetFeedback( canvasId, ImVec2( map.XToScreen( 0.0 ), strip.GetCenter().y ) );
            }
            else if( hovered && !ImGui::IsItemActive() )
            {
                ImGui::SetTooltip( "%s: drag along the strip, double-click for 0", FormatValue( Unit::EV, InOutEV ).c_str() );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::EV, InOutEV ) );
        }

        const Graph::CanvasInput input = Graph::End();
        const ImGuiContext& g = *GImGui;
        if( input.Pressed || markerScrub || ( input.Held && g.IO.MouseDelta.x != 0.f ) )
        {
            // Third stops, snapping to 0 and the rungs near the pointer.
            double next = RoundTo( std::clamp( map.XFromScreen( g.IO.MousePos.x ), kLadderMin, kLadderMax ) * 3.0, 1.0 ) / 3.0;
            next = std::round( next * 100.0 ) / 100.0;
            const float reach = ImGuiTheme::Em( 0.35f );
            if( std::abs( map.XToScreen( 0.0 ) - g.IO.MousePos.x ) <= reach )
            {
                next = 0.0;
            }
            for( const Rung& rung : kExposureRungs )
            {
                if( std::abs( map.XToScreen( rung.EV ) - g.IO.MousePos.x ) <= reach )
                {
                    next = rung.EV;
                }
            }
            next = Constrain( next, InSpec );
            bool ticked = Crossed( InOutEV, next, 0.0 );
            for( const Rung& rung : kExposureRungs )
            {
                ticked |= Crossed( InOutEV, next, rung.EV );
            }
            if( ticked && !input.Pressed )
            {
                Juice::Tick( canvasId, ImVec2( map.XToScreen( next ), strip.Max.y ) );
            }
            if( next != InOutEV )
            {
                InOutEV = next;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Exposure compensation, in stops: each is twice (or half) the light.\n"
                               "The rungs are looks-like references for the engine's lights (Sun 5x, Overcast 1x), not metered EV:\n"
                               "a sunlit scene wants less exposure, a night street more.\n"
                               "Click a rung or the strip, drag along it (third stops); double-click the value's marker for 0." );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }

        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( strip.Min, strip.Max, U32( palette.Control ), rounding );
            // Whole stops as small ticks, the centre detent as a full one.
            for( int ev = static_cast<int>( kLadderMin ) + 1; ev < static_cast<int>( kLadderMax ); ++ev )
            {
                const float x = std::round( map.XToScreen( ev ) );
                const float inset = ev == 0 ? 0.f : stripHeight * 0.3f;
                drawList->AddLine( ImVec2( x, strip.Min.y + inset ), ImVec2( x, strip.Max.y - inset ), U32( ImGuiTheme::WithAlpha( palette.Text, ev == 0 ? 0.7f : 0.25f ) ), Thin() );
            }
            const float t = std::clamp( Follow( ImGui::GetID( "##fill" ), static_cast<float>( xAxis.ToNormalized( std::clamp( InOutEV, kLadderMin, kLadderMax ) ) ), canvasId ), 0.f, 1.f );
            const float centre = std::round( map.XToScreen( 0.0 ) );
            const float x = std::round( strip.Min.x + t * strip.GetWidth() );
            if( x != centre )
            {
                drawList->AddRectFilled( ImVec2( std::min( x, centre ), strip.Min.y ), ImVec2( std::max( x, centre ), strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) ) );
            }
            drawList->AddLine( ImVec2( x, strip.Min.y - Thin() ), ImVec2( x, strip.Max.y + Thin() ), U32( palette.Text ), Thin() * 2.f );
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddRectFilled( strip.Min, strip.Max, U32( ImGuiTheme::WithAlpha( palette.Hovered, 0.5f * tick ) ), rounding );
            }
            FlashOutline( drawList, strip, canvasId );
        }
        ItemLog::SetItemValue( canvasId, FormatValue( Unit::EV, InOutEV ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Tonemap curve ------------------------------------------------------------------------

    bool TonemapCurve( const char* InCaption, int& InOutOperator, double InExposureEV, bool InAutoExposure )
    {
        using namespace PostMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const int active = std::clamp( InOutOperator, 0, kTonemapperCount - 1 );
        const Graph::Axis xAxis { 0.001, 100.0, true, Unit::None };
        const Graph::Axis yAxis { 0.0, 1.0, false, Unit::None };
        // The legend: one row with line swatches when it fits, else without them, else two rows.
        float swatch = ImGuiTheme::Em( 0.7f );
        float gap = ImGuiTheme::Em( 0.8f );
        const float inner = ImGuiTheme::Em( 0.25f );
        auto legendWidth = [&]( float InSwatch, float InGap ) {
            float total = 0.f;
            for( int op = 0; op < kTonemapperCount; ++op )
            {
                total += ( InSwatch > 0.f ? InSwatch + inner : 0.f ) + ImGui::CalcTextSize( TonemapperName( op ) ).x + ( op + 1 < kTonemapperCount ? InGap : 0.f );
            }
            return total;
        };
        const float room = width - ImGuiTheme::Em( 0.5f );
        int legendRows = 1;
        if( legendWidth( swatch, gap ) > room )
        {
            swatch = 0.f;
            gap = ImGuiTheme::Em( 0.6f );
            legendRows = legendWidth( swatch, gap ) > room ? 2 : 1;
        }
        const float rowStep = ImGui::GetFontSize() + ImGuiTheme::Em( 0.1f );
        Graph::Frame frame;
        frame.Below = rowStep * legendRows + ImGuiTheme::Em( 0.15f );
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 6.f ) + frame.Below ), xAxis, yAxis, frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        bool changed = false;
        constexpr double kGrey = 0.18;
        const double greyShown = DisplayGrey( active, kGrey, InExposureEV );

        if( visible )
        {
            Graph::Grid( true, false );
            for( int op = 0; op < kTonemapperCount; ++op )
            {
                if( op != active )
                {
                    Graph::Plot( [&]( double x ) { return DisplayGrey( op, x, InExposureEV ); }, U32( ImGuiTheme::WithAlpha( palette.Muted, 0.55f ) ), false, 96, Thin() );
                }
            }
            Graph::Plot( [&]( double x ) { return DisplayGrey( active, x, InExposureEV ); }, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.95f ) ), true, 128, Thin() * 2.f );
            // Mid grey: where it lands on screen.
            const float gx = std::round( map.XToScreen( kGrey ) );
            const float gy = std::round( map.YToScreen( greyShown ) );
            DashedVertical( drawList, gx, map.Min.y, map.Max.y, U32( ImGuiTheme::WithAlpha( palette.Text, 0.4f ) ) );
            DashedHorizontal( drawList, map.Min.x, gx, gy, U32( ImGuiTheme::WithAlpha( palette.Text, 0.4f ) ) );
            drawList->AddCircleFilled( ImVec2( gx, gy ), Thin() * 3.f, U32( palette.Text ) );
            const std::string reading = FormatNumber( greyShown, 2 );
            const ImVec2 size = ImGui::CalcTextSize( reading.c_str() );
            const ImVec2 at( map.Min.x + ImGuiTheme::Em( 0.2f ), std::clamp( gy - size.y - Thin(), map.Min.y, map.Max.y - size.y ) );
            const float pad = ImGuiTheme::Em( 0.15f );
            drawList->AddRectFilled( ImVec2( at.x - pad, at.y ), ImVec2( at.x + size.x + pad, at.y + size.y ), U32( ImGuiTheme::WithAlpha( palette.Recessed, 0.85f ) ), ImGui::GetStyle().FrameRounding );
            drawList->AddText( at, U32( palette.Text ), reading.c_str() );
        }

        // The operators, to switch between them.
        const float legendTop = map.Max.y + ( ImGui::GetFontSize() + ImGuiTheme::Em( 0.2f ) ) + ImGuiTheme::Em( 0.1f );
        float left = map.Min.x;
        const float textOffset = swatch > 0.f ? swatch + inner : 0.f;
        for( int op = 0; op < kTonemapperCount; ++op )
        {
            const char* name = TonemapperName( op );
            const ImVec2 size = ImGui::CalcTextSize( name );
            // Two rows: the second half of the operators under the first.
            const int row = legendRows > 1 && op >= ( kTonemapperCount + 1 ) / 2 ? 1 : 0;
            if( legendRows > 1 && op == ( kTonemapperCount + 1 ) / 2 )
            {
                left = map.Min.x;
            }
            const float top = legendTop + rowStep * row;
            const ImRect rect( ImVec2( left, top ), ImVec2( left + textOffset + size.x, top + size.y ) );
            bool hovered = false;
            if( Graph::MarkerButton( name, rect, &hovered ) )
            {
                if( op != InOutOperator )
                {
                    InOutOperator = op;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( rect.GetCenter().x, rect.Min.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), std::string( name ) + ": 0.18 -> " + FormatNumber( DisplayGrey( op, kGrey, InExposureEV ), 2 ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: mid grey (0.18) shows as %s", name, FormatNumber( DisplayGrey( op, kGrey, InExposureEV ), 2 ).c_str() );
            }
            if( visible )
            {
                const bool current = op == active;
                const float y = std::round( rect.GetCenter().y );
                if( swatch > 0.f )
                {
                    drawList->AddLine( ImVec2( left, y ), ImVec2( left + swatch, y ), U32( current ? ImGuiTheme::WithAlpha( palette.AccentText, 0.95f ) : ImGuiTheme::WithAlpha( palette.Muted, 0.7f ) ), current ? Thin() * 2.f : Thin() );
                }
                drawList->AddText( ImVec2( left + textOffset, rect.Min.y ), U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted ), name );
            }
            left = rect.Max.x + gap;
        }

        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "What the screen shows (sRGB, 0..1) for a grey of each scene luminance (log scale) at %s,\n"
                               "before grading; the faint curves are the other tonemappers. The dot is mid grey (0.18).%s\n"
                               "Click a name below to switch tonemapper.",
                FormatValue( Unit::EV, InExposureEV ).c_str(),
                InAutoExposure ? "\nWith auto exposure, luminance is relative to the adapted average (shown as 0.18)." : "" );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        if( visible )
        {
            FlashOutline( drawList, Graph::CanvasRect(), canvasId );
        }
        ItemLog::SetItemValue( canvasId, std::string( TonemapperName( InOutOperator ) ) + ": 0.18 -> " + FormatNumber( DisplayGrey( std::clamp( InOutOperator, 0, kTonemapperCount - 1 ), kGrey, InExposureEV ), 2 ) );
        ImGui::PopID();
        return changed;
    }

    // ---- White balance ------------------------------------------------------------------------

    bool WhiteBalancePad( const char* InCaption, double& InOutTemperature, double& InOutTint )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const Graph::Axis axis { -1.0, 1.0, false, Unit::None };
        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 5.5f ) ), axis, axis, frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        bool changed = false;
        // A light neutral grey through the white balance (linear), as the shader scales it.
        auto shifted = []( double InT, double InTint ) {
            double scale[3];
            PostMath::WhiteBalanceScale( InT, InTint, scale );
            const double grey = 0.42;
            const double rgb[3] = { grey * scale[0], grey * scale[1], grey * scale[2] };
            return LinearToU32( rgb );
        };

        if( visible )
        {
            constexpr int kColumns = 12;
            constexpr int kRows = 6;
            for( int i = 0; i < kColumns; ++i )
            {
                const double t0 = axis.FromNormalized( static_cast<double>( i ) / kColumns );
                const double t1 = axis.FromNormalized( static_cast<double>( i + 1 ) / kColumns );
                for( int j = 0; j < kRows; ++j )
                {
                    const double y0 = axis.FromNormalized( static_cast<double>( j ) / kRows );
                    const double y1 = axis.FromNormalized( static_cast<double>( j + 1 ) / kRows );
                    drawList->AddRectFilledMultiColor( ImVec2( std::floor( map.XToScreen( t0 ) ), std::floor( map.YToScreen( y1 ) ) ), ImVec2( std::ceil( map.XToScreen( t1 ) ), std::ceil( map.YToScreen( y0 ) ) ),
                        shifted( t0, y1 ), shifted( t1, y1 ), shifted( t1, y0 ), shifted( t0, y0 ) );
                }
            }
            drawList->AddRect( map.Min, map.Max, U32( palette.Separator ), 0.f, 0, Thin() );
            // The crosshair at neutral.
            const ImU32 cross = U32( ImGuiTheme::Gray( 0.1f, 0.45f ) );
            const ImVec2 centre( std::round( map.XToScreen( 0.0 ) ), std::round( map.YToScreen( 0.0 ) ) );
            drawList->AddLine( ImVec2( map.Min.x, centre.y ), ImVec2( map.Max.x, centre.y ), cross, Thin() );
            drawList->AddLine( ImVec2( centre.x, map.Min.y ), ImVec2( centre.x, map.Max.y ), cross, Thin() );
            drawList->AddCircle( centre, ImGuiTheme::Em( 0.45f ), cross, 0, Thin() );
            // What each edge does, in dark text on the light swatch (it reads in both themes).
            const ImU32 caption = U32( ImGuiTheme::Gray( 0.08f, 0.8f ) );
            const float pad = ImGuiTheme::Em( 0.2f );
            const float line = ImGui::GetFontSize();
            drawList->AddText( ImVec2( map.Min.x + pad, centre.y - line - Thin() ), caption, "Cool" );
            drawList->AddText( ImVec2( map.Max.x - pad - ImGui::CalcTextSize( "Warm" ).x, centre.y - line - Thin() ), caption, "Warm" );
            drawList->AddText( ImVec2( centre.x + pad, map.Min.y + Thin() ), caption, "Magenta" );
            drawList->AddText( ImVec2( centre.x + pad, map.Max.y - line - Thin() ), caption, "Green" );
        }

        Graph::HandleSpec spec;
        spec.MinX = -1.0;
        spec.MaxX = 1.0;
        spec.MinY = -1.0;
        spec.MaxY = 1.0;
        spec.ShowValue = false;
        double t = std::clamp( InOutTemperature, -1.0, 1.0 );
        double tint = std::clamp( InOutTint, -1.0, 1.0 );
        const double startT = t;
        const double startTint = tint;
        bool moved = Graph::DragHandle( "Puck", &t, &tint, spec );
        const ImGuiID puckId = ImGui::GetItemID();
        // Only the puck resets on a double-click: it doesn't move on a press, so the gesture is one
        // undo step (a click on the pad jumps the puck to the pointer).
        const bool reset = DoubleClickedLastItem();
        const Graph::CanvasInput input = Graph::End();
        const ImGuiContext& g = *GImGui;
        // Each axis follows the pointer only while the pointer moves along it: a drag straight
        // across leaves the other value exactly as it was (a typed 0.123 stays 0.123).
        bool moveX = moved && g.IO.MouseDelta.x != 0.f;
        bool moveY = moved && g.IO.MouseDelta.y != 0.f;
        if( !reset && ( input.Pressed || ( input.Held && ( g.IO.MouseDelta.x != 0.f || g.IO.MouseDelta.y != 0.f ) ) ) )
        {
            t = std::clamp( input.X, -1.0, 1.0 );
            tint = std::clamp( input.Y, -1.0, 1.0 );
            moveX = input.Pressed || g.IO.MouseDelta.x != 0.f;
            moveY = input.Pressed || g.IO.MouseDelta.y != 0.f;
        }
        t = moveX ? t : InOutTemperature;
        tint = moveY ? tint : InOutTint;
        if( reset )
        {
            t = 0.0;
            tint = 0.0;
            ResetFeedback( canvasId, ImVec2( map.XToScreen( 0.0 ), map.YToScreen( 0.0 ) ) );
        }
        else if( moveX || moveY )
        {
            // Hundredths, with a detent on each axis at neutral: crossing the crosshair snaps to
            // 0 and it holds within a third of an em; any other hundredth stays reachable.
            const float reach = ImGuiTheme::Em( 0.35f );
            const double reachX = std::abs( map.XFromScreen( map.XToScreen( 0.0 ) + reach ) );
            const double reachY = std::abs( map.YFromScreen( map.YToScreen( 0.0 ) + reach ) );
            if( moveX )
            {
                t = Detent( startT, RoundTo( t, 0.01 ), 0.0, reachX );
            }
            if( moveY )
            {
                tint = Detent( startTint, RoundTo( tint, 0.01 ), 0.0, reachY );
            }
            if( ( t == 0.0 && startT != 0.0 ) || ( tint == 0.0 && startTint != 0.0 ) || Crossed( startT, t, 0.0 ) || Crossed( startTint, tint, 0.0 ) )
            {
                Juice::Tick( g.ActiveId == puckId ? puckId : canvasId, ImVec2( map.XToScreen( t ), map.YToScreen( tint ) ) );
            }
        }
        if( t != InOutTemperature || tint != InOutTint )
        {
            InOutTemperature = t;
            InOutTint = tint;
            changed = true;
            MarkEdited( canvasId );
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "White balance before tonemapping: Temperature (left cool, right warm) scales red up and blue down by 20%%,\n"
                               "Tint (up magenta, down green) scales green down by 10%%. The pad shows a light grey through it.\n"
                               "Drag the puck or click the pad, in hundredths; crossing the crosshair snaps to neutral. Double-click the puck to reset." );
        }
        if( visible )
        {
            FlashOutline( drawList, ImRect( map.Min, map.Max ), canvasId );
        }
        const std::string value = FormatNumber( RoundTo( InOutTemperature, 0.01 ) ) + ", " + FormatNumber( RoundTo( InOutTint, 0.01 ) );
        ItemLog::SetItemValue( puckId, value );
        ItemLog::SetItemValue( canvasId, value );
        ImGui::PopID();
        return changed;
    }

    // ---- Lift / gamma / gain ----------------------------------------------------------------

    bool LiftGammaGainCurve( const char* InCaption, double& InOutLift, double& InOutGamma, double& InOutGain, double InContrast )
    {
        using namespace PostMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const Graph::Axis xAxis { 0.0, 1.0, false, Unit::None };
        const Graph::Axis yAxis { -0.25, 1.5, false, Unit::None };
        Graph::Frame frame;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 7.f ) ), xAxis, yAxis, frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        Grade grade;
        grade.Contrast = InContrast;
        grade.Lift = InOutLift;
        grade.Gamma = InOutGamma;
        grade.Gain = InOutGain;
        auto encodeSigned = []( double InLinear ) { return InLinear < 0.0 ? -SrgbEncode( -InLinear ) : SrgbEncode( InLinear ); };
        auto decodeSigned = []( double InEncoded ) { return InEncoded < 0.0 ? -SrgbDecode( -InEncoded ) : SrgbDecode( InEncoded ); };
        constexpr double kMid = 0.5;    // the Gamma handle's input, as seen (sRGB)

        if( visible )
        {
            // What clips: below black and above white.
            const ImU32 clipped = U32( ImGuiTheme::WithAlpha( palette.Text, 0.07f ) );
            drawList->AddRectFilled( ImVec2( map.Min.x, map.YToScreen( 0.0 ) ), map.Max, clipped );
            drawList->AddRectFilled( map.Min, ImVec2( map.Max.x, map.YToScreen( 1.0 ) ), clipped );
            const ImU32 rule = U32( ImGuiTheme::WithAlpha( palette.Separator, 0.8f ) );
            drawList->AddLine( ImVec2( map.Min.x, map.YToScreen( 0.0 ) ), ImVec2( map.Max.x, map.YToScreen( 0.0 ) ), rule, Thin() );
            drawList->AddLine( ImVec2( map.Min.x, map.YToScreen( 1.0 ) ), ImVec2( map.Max.x, map.YToScreen( 1.0 ) ), rule, Thin() );
            // Untouched: the diagonal.
            drawList->AddLine( map.ToScreen( 0.0, 0.0 ), map.ToScreen( 1.0, 1.0 ), U32( ImGuiTheme::WithAlpha( palette.Muted, 0.6f ) ), Thin() );
            Graph::Plot( [&]( double x ) { return SrgbEncode( GradeChannel( grade, SrgbDecode( x ) ) ); }, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.95f ) ), false, 96, Thin() * 2.f );
            // The handles' inputs, named under the plot.
            const char* names[3] = { "Black", "Mid", "White" };
            const double at[3] = { 0.0, kMid, 1.0 };
            for( int i = 0; i < 3; ++i )
            {
                const ImVec2 size = ImGui::CalcTextSize( names[i] );
                const float x = std::clamp( map.XToScreen( at[i] ) - size.x * 0.5f, Graph::CanvasRect().Min.x, Graph::CanvasRect().Max.x - size.x );
                drawList->AddText( ImVec2( x, map.Max.y + ImGuiTheme::Em( 0.1f ) ), U32( palette.Muted ), names[i] );
            }
        }

        bool changed = false;
        Graph::HandleSpec spec;
        spec.MinY = yAxis.Min;
        spec.MaxY = yAxis.Max;
        spec.ShowValue = false;
        const ImU32 colors[3] = { U32( palette.Text ), U32( palette.AccentText ), U32( palette.Text ) };
        auto handle = [&]( const char* InLabel, double InAt, int InColor, double& InOutParam, double InNeutral, double InMin, double InMax, bool InLogDrag, auto&& InSolve ) {
            double x = InAt;
            double y = std::clamp( encodeSigned( GradeSigned( grade, SrgbDecode( InAt ) ) ), yAxis.Min, yAxis.Max );
            spec.MinX = InAt;
            spec.MaxX = InAt;
            // A log-drag handle stays on the curve; the pointer's travel sets the value.
            spec.MinY = InLogDrag ? y : yAxis.Min;
            spec.MaxY = InLogDrag ? y : yAxis.Max;
            spec.Color = colors[InColor];
            const bool dragged = Graph::DragHandle( InLabel, &x, &y, spec );
            const ImGuiID id = ImGui::GetItemID();
            double next = InOutParam;
            if( DoubleClickedLastItem() )
            {
                next = InNeutral;
                ResetFeedback( id, map.ToScreen( InAt, y ) );
            }
            else if( InLogDrag )
            {
                // One doubling (or halving) per 3 em of vertical travel, ten times finer with
                // Shift. The travel adds up unrounded, so fine steps still get somewhere.
                const ImGuiContext& g = *GImGui;
                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID startKey = ImHashStr( "##LogStart", 0, id );
                const ImGuiID travelKey = ImHashStr( "##LogTravel", 0, id );
                if( ImGui::IsItemActivated() )
                {
                    storage->SetFloat( startKey, static_cast<float>( std::clamp( InOutParam, InMin, InMax ) ) );
                    storage->SetFloat( travelKey, 0.f );
                }
                if( ImGui::IsItemActive() && g.IO.MouseDelta.y != 0.f )
                {
                    const double start = std::max( static_cast<double>( storage->GetFloat( startKey, static_cast<float>( InOutParam ) ) ), 1e-3 );
                    const double scale = ( g.IO.KeyShift ? 0.1 : 1.0 ) / ImGuiTheme::Em( 3.f );
                    const double travel = std::clamp( storage->GetFloat( travelKey, 0.f ) - g.IO.MouseDelta.y * scale, std::log2( InMin / start ), std::log2( InMax / start ) );
                    storage->SetFloat( travelKey, static_cast<float>( travel ) );
                    next = Detent( InOutParam, RoundTo( std::clamp( start * std::exp2( travel ), InMin, InMax ), 0.01 ), InNeutral, 0.02 );
                    if( Crossed( InOutParam, next, InNeutral ) )
                    {
                        Juice::Tick( id, map.ToScreen( InAt, y ) );
                    }
                }
            }
            else if( dragged )
            {
                double solved = 0.0;
                if( InSolve( decodeSigned( y ), solved ) )
                {
                    next = RoundTo( std::clamp( solved, InMin, InMax ), 0.01 );
                    // A detent at neutral.
                    if( std::abs( next - InNeutral ) < 0.015 )
                    {
                        next = InNeutral;
                    }
                    if( Crossed( InOutParam, next, InNeutral ) )
                    {
                        Juice::Tick( id, map.ToScreen( InAt, y ) );
                    }
                }
            }
            ItemLog::SetItemValue( id, FormatNumber( RoundTo( next, 0.01 ) ) );
            if( ImGui::IsItemHovered() && !ImGui::IsItemActive() )
            {
                ImGui::SetTooltip( "%s %s: drag up or down%s; double-click for %s", InLabel, FormatNumber( RoundTo( next, 0.01 ) ).c_str(),
                    InLogDrag ? " (3 em doubles or halves it, Shift for fine)" : "", FormatNumber( InNeutral ).c_str() );
            }
            if( next != InOutParam )
            {
                InOutParam = next;
                changed = true;
            }
            grade.Lift = InOutLift;
            grade.Gamma = InOutGamma;
            grade.Gain = InOutGain;
        };
        handle( "Lift", 0.0, 0, InOutLift, 0.0, -0.5, 0.5, false, [&]( double InTarget, double& Out ) { return SolveLift( grade, InTarget, Out ); } );
        handle( "Gamma", kMid, 1, InOutGamma, 1.0, 0.2, 3.0, true, [&]( double InTarget, double& Out ) { return SolveGamma( grade, SrgbDecode( kMid ), InTarget, Out ); } );
        handle( "Gain", 1.0, 2, InOutGain, 1.0, 0.0, 2.0, false, [&]( double InTarget, double& Out ) { return SolveGain( grade, InTarget, Out ); } );

        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The grade after tonemapping: output against input, as seen (sRGB). The faint diagonal changes nothing;\n"
                               "shaded bands clip. Lift raises (or crushes) black, Gamma bends the mids, Gain scales white;\n"
                               "Contrast (%s) pivots around mid grey. Drag a handle up or down; double-click it to reset.\n"
                               "Lift, Gamma and Gain are single values here (no colour wheels: the engine grades luminance only).",
                FormatNumber( InContrast, 2 ).c_str() );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        ItemLog::SetItemValue( canvasId, "Lift " + FormatNumber( RoundTo( InOutLift, 0.01 ) ) + ", Gamma " + FormatNumber( RoundTo( InOutGamma, 0.01 ) ) + ", Gain " + FormatNumber( RoundTo( InOutGain, 0.01 ) ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Fog visibility -------------------------------------------------------------------------

    bool FogVisibilityBar( const char* InCaption, double& InOutDensity, const NumberSpec& InDensitySpec, double InStart, double InMaxOpacity, const float* InFogSrgb, double InFalloff )
    {
        using namespace PostMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const Graph::Axis xAxis { 1.0, 10000.0, true, Unit::Meters };
        const Graph::Axis yAxis { 0.0, 1.0, false, Unit::None };
        const float lineHeight = ImGui::GetTextLineHeight();
        Graph::Frame frame;
        const float textRoom = lineHeight + ImGuiTheme::Em( 0.2f );
        const bool visible = Graph::Begin( InCaption, ImVec2( width, textRoom + ImGuiTheme::Em( 3.4f ) ), xAxis, yAxis, frame );
        // The plot sits under the readout: a mapping of its own, in the same canvas.
        Graph::Mapping map = Graph::Current();
        map.Min.y += textRoom;
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const double visibility = FogVisibility( InOutDensity, InStart, InMaxOpacity );
        // Under 95% max opacity the fog never hides anything; the handle still sits where the
        // density would put 95% without the cap, so dragging it moves something you can see.
        const bool capped = InMaxOpacity < 0.95;
        const double uncapped = FogVisibility( InOutDensity, InStart, 1.0 );
        auto describe = [&]( double InVisibility, double InUncapped ) {
            if( capped )
            {
                // Short: the row is narrow. The tooltip spells it out.
                const std::string cap = "unlimited: " + FormatNumber( std::round( std::max( InMaxOpacity, 0.0 ) * 100.0 ) ) + "% cap";
                return std::isfinite( InUncapped ) ? cap + " (" + FormatValue( Unit::Meters, Significant( InUncapped, 2 ) ) + " uncapped)" : cap;
            }
            if( !std::isfinite( InVisibility ) )
            {
                return std::string( "visibility unlimited" );
            }
            return "visibility " + FormatValue( Unit::Meters, Significant( InVisibility, 2 ) );
        };
        // The fog colour, lifted so a dark fog still shows over the panel.
        ImVec4 fog( std::clamp( InFogSrgb[0], 0.f, 1.f ), std::clamp( InFogSrgb[1], 0.f, 1.f ), std::clamp( InFogSrgb[2], 0.f, 1.f ), 1.f );
        const float brightest = std::max( { fog.x, fog.y, fog.z, 1e-3f } );
        const float lift = brightest < 0.7f ? 0.7f / brightest : 1.f;
        fog = ImVec4( std::min( fog.x * lift, 1.f ), std::min( fog.y * lift, 1.f ), std::min( fog.z * lift, 1.f ), 1.f );

        if( visible )
        {
            // The opacity with distance, filled in the fog's colour.
            constexpr int kSegments = 64;
            const ImVec4 clear = palette.Recessed;
            for( int i = 0; i < kSegments; ++i )
            {
                const double d0 = xAxis.FromNormalized( static_cast<double>( i ) / kSegments );
                const double d1 = xAxis.FromNormalized( static_cast<double>( i + 1 ) / kSegments );
                const ImU32 c0 = U32( ImGuiTheme::Mix( clear, fog, static_cast<float>( FogOpacity( d0, InOutDensity, InStart, InMaxOpacity ) ) ) );
                const ImU32 c1 = U32( ImGuiTheme::Mix( clear, fog, static_cast<float>( FogOpacity( d1, InOutDensity, InStart, InMaxOpacity ) ) ) );
                drawList->AddRectFilledMultiColor( ImVec2( std::floor( map.XToScreen( d0 ) ), map.Min.y ), ImVec2( std::ceil( map.XToScreen( d1 ) ), map.Max.y ), c0, c1, c1, c0 );
            }
            // Grid and labels by hand: this plot's mapping is the canvas's minus the readout.
            double ticks[16];
            const int count = Graph::Ticks( xAxis, ticks, 6 );
            float lastRight = -1e9f;
            for( int i = 0; i < count; ++i )
            {
                const float x = std::round( map.XToScreen( ticks[i] ) );
                drawList->AddLine( ImVec2( x, map.Min.y ), ImVec2( x, map.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Separator, 0.6f ) ), Thin() );
                const std::string label = FormatValue( Unit::Meters, ticks[i] );
                const ImVec2 size = ImGui::CalcTextSize( label.c_str() );
                const float left = std::clamp( x - size.x * 0.5f, Graph::CanvasRect().Min.x, Graph::CanvasRect().Max.x - size.x );
                if( left > lastRight + ImGuiTheme::Em( 0.4f ) )
                {
                    drawList->AddText( ImVec2( left, map.Max.y + ImGuiTheme::Em( 0.1f ) ), U32( palette.Muted ), label.c_str() );
                    lastRight = left + size.x;
                }
            }
            // The opacity curve, 95% as a dashed rule.
            std::vector<ImVec2> points;
            for( int i = 0; i <= kSegments; ++i )
            {
                const double d = xAxis.FromNormalized( static_cast<double>( i ) / kSegments );
                points.push_back( map.ToScreen( d, FogOpacity( d, InOutDensity, InStart, InMaxOpacity ) ) );
            }
            drawList->AddPolyline( points.data(), static_cast<int>( points.size() ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.75f ) ), ImDrawFlags_None, Thin() * 1.5f );
            DashedHorizontal( drawList, map.Min.x, map.Max.x, std::round( map.YToScreen( 0.95 ) ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.35f ) ) );
            if( std::isfinite( uncapped ) && uncapped <= xAxis.Max )
            {
                DashedVertical( drawList, std::round( map.XToScreen( std::max( uncapped, xAxis.Min ) ) ), map.Min.y, map.Max.y, U32( ImGuiTheme::WithAlpha( palette.Text, capped ? 0.3f : 0.6f ) ) );
            }
            drawList->AddRect( map.Min, map.Max, U32( palette.Separator ), 0.f, 0, Thin() );
            const std::string text = describe( visibility, uncapped );
            drawList->AddText( ImVec2( Graph::CanvasRect().Min.x + ImGuiTheme::Em( 0.25f ), Graph::CanvasRect().Min.y + ImGuiTheme::Em( 0.2f ) ), U32( palette.Text ), text.c_str() );
        }

        // The handle sits on the plot's bottom edge; Graph places X-only handles on the canvas
        // mapping's bottom, which is the same line.
        double ticks[32];
        Graph::HandleSpec spec;
        spec.MinX = xAxis.Min;
        spec.MaxX = xAxis.Max;
        spec.Detents = ticks;
        spec.DetentCount = Graph::Ticks( xAxis, ticks, 32 );
        double shown = std::isfinite( uncapped ) ? std::clamp( uncapped, xAxis.Min, xAxis.Max ) : xAxis.Max;
        bool changed = false;
        auto apply = [&]( double InVisibility ) {
            double density = 0.0;
            if( !FogDensityFor( Significant( InVisibility, 2 ), InStart, density ) )
            {
                return;
            }
            density = Constrain( density, InDensitySpec );
            if( density != InOutDensity )
            {
                InOutDensity = density;
                changed = true;
            }
        };
        if( Graph::DragHandle( "Visibility", &shown, nullptr, spec ) )
        {
            apply( shown );
        }
        const Graph::CanvasInput input = Graph::End();
        // Only the bar takes a click (the readout above it doesn't); a drag begun on it goes on.
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID inBarKey = ImHashStr( "##InBar", 0, canvasId );
        if( input.Pressed )
        {
            const float y = GImGui->IO.MousePos.y;
            storage->SetBool( inBarKey, y >= map.Min.y && y <= map.Max.y );
        }
        if( storage->GetBool( inBarKey, false ) && ( input.Pressed || ( input.Held && GImGui->IO.MouseDelta.x != 0.f ) ) )
        {
            apply( std::clamp( input.X, xAxis.Min, xAxis.Max ) );
        }
        if( input.Hovered && !input.Held )
        {
            const double heightFactor = FogHeightFactor( 5.0, 0.0, InFalloff );
            const double above = FogVisibility( InOutDensity, InStart, InMaxOpacity, heightFactor );
            ImGui::SetTooltip( "How far you see looking level at the fog's base height: where the fog reaches 95%%\n"
                               "(start + ln 20 / density, from Fog.sh). The curve is the fog's opacity with distance (log scale).\n"
                               "5 m above the base height the fog is thinner: %s.\n"
                               "Drag Visibility or click the bar to set the density for that distance%s.",
                describe( above, FogVisibility( InOutDensity, InStart, 1.0, heightFactor ) ).c_str(), capped ? ".\nMax Opacity is under 95%, so the fog never hides anything: the handle shows (and sets) where it would reach 95% at 100%" : "" );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        ItemLog::SetItemValue( canvasId, describe( FogVisibility( InOutDensity, InStart, InMaxOpacity ), FogVisibility( InOutDensity, InStart, 1.0 ) ) );
        ImGui::PopID();
        return changed;
    }
}
