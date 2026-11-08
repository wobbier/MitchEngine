#include "PhysicsWidgets.h"
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
#include <unordered_map>
#include <vector>

namespace MeWidgets
{
    // ---- Math ---------------------------------------------------------------------------------

    namespace PhysicsMath
    {
        namespace
        {
            constexpr double kPiD = 3.14159265358979323846;
        }

        const MassRung* MassRungs( int& OutCount )
        {
            static const MassRung rungs[] = {
                { "Feather", 0.01, "A feather, a coin, a bullet" },
                { "Apple", 0.1, "An apple, a phone, a tennis ball" },
                { "Cat", 4.0, "A cat, a bowling ball, a full backpack" },
                { "Human", 70.0, "An adult person" },
                { "Motorbike", 200.0, "A motorbike, a piano, a big crate" },
                { "Car", 1500.0, "A family car" },
                { "Truck", 15000.0, "A loaded truck, a bus" },
            };
            OutCount = static_cast<int>( sizeof( rungs ) / sizeof( rungs[0] ) );
            return rungs;
        }

        double MassToLadder( double InKilograms )
        {
            const double kg = std::clamp( InKilograms, kLadderMinKg, kLadderMaxKg );
            return ( std::log10( kg ) - std::log10( kLadderMinKg ) ) / ( std::log10( kLadderMaxKg ) - std::log10( kLadderMinKg ) );
        }

        double MassFromLadder( double InT )
        {
            const double t = std::clamp( InT, 0.0, 1.0 );
            return std::pow( 10.0, std::log10( kLadderMinKg ) + t * ( std::log10( kLadderMaxKg ) - std::log10( kLadderMinKg ) ) );
        }

        double RoundMass( double InKilograms )
        {
            if( !( InKilograms > 0.0 ) )
            {
                return 0.0;
            }
            const double power = std::pow( 10.0, std::floor( std::log10( InKilograms ) ) - 1.0 );
            return std::round( InKilograms / power ) * power;
        }

        double BounceRatio( double InRestitution )
        {
            const double e = std::clamp( InRestitution, 0.0, 1.0 );
            return e * e;
        }

        double BounceHeight( double InDropHeight, double InRestitution, int InBounce )
        {
            return InDropHeight * std::pow( std::clamp( InRestitution, 0.0, 1.0 ), 2.0 * std::max( InBounce, 0 ) );
        }

        double BallHeight( double InTime, double InRestitution, double InFallTime, double InRest )
        {
            const double t0 = std::max( InFallTime, 1e-6 );
            if( InTime <= 0.0 )
            {
                return 1.0;
            }
            if( InTime < t0 )
            {
                const double s = InTime / t0;
                return 1.0 - s * s;
            }
            // Height 1 falls in t0, so height y = h - (s / t0)^2 at s from the apex: bounce n
            // (peak e^(2n)) lasts 2 t0 e^n.
            const double e = std::clamp( InRestitution, 0.0, 1.0 );
            double t = InTime - t0;
            for( int n = 1; n < 10000; ++n )
            {
                const double peak = BounceHeight( 1.0, e, n );
                if( peak < InRest )
                {
                    return 0.0;
                }
                const double flight = 2.0 * t0 * std::pow( e, n );
                if( t < flight )
                {
                    const double s = ( t - flight * 0.5 ) / t0;
                    return std::max( peak - s * s, 0.0 );
                }
                t -= flight;
            }
            return 0.0;
        }

        double BounceApexTime( int InBounce, double InRestitution, double InFallTime )
        {
            if( InBounce <= 0 )
            {
                return 0.0;
            }
            const double e = std::clamp( InRestitution, 0.0, 1.0 );
            double t = InFallTime;
            for( int k = 1; k < InBounce; ++k )
            {
                t += 2.0 * InFallTime * std::pow( e, k );
            }
            return t + InFallTime * std::pow( e, InBounce );
        }

        double SlideAngleDegrees( double InFriction )
        {
            return std::atan( std::max( InFriction, 0.0 ) ) * 180.0 / kPiD;
        }

        double FrictionForAngle( double InDegrees )
        {
            return std::tan( std::clamp( InDegrees, 0.0, 89.9 ) * kPiD / 180.0 );
        }

        double DampingStepFactor( double InDamping, double InSubstep )
        {
            return 1.0 / ( 1.0 + std::max( InSubstep, 0.0 ) * std::max( InDamping, 0.0 ) );
        }

        double DampedSpeed( double InDamping, double InTime, double InSubstep )
        {
            const double d = std::max( InDamping, 0.0 );
            if( !( InSubstep > 0.0 ) )
            {
                return std::exp( -d * InTime );
            }
            return std::exp( -InTime / InSubstep * std::log1p( InSubstep * d ) );
        }

        double DampingHalfLife( double InDamping, double InSubstep )
        {
            const double d = std::max( InDamping, 0.0 );
            if( !( d > 0.0 ) )
            {
                return std::numeric_limits<double>::infinity();
            }
            if( !( InSubstep > 0.0 ) )
            {
                return std::log( 2.0 ) / d;
            }
            return InSubstep * std::log( 2.0 ) / std::log1p( InSubstep * d );
        }

        double DampingForHalfLife( double InHalfLife, double InSubstep )
        {
            if( !std::isfinite( InHalfLife ) )
            {
                return 0.0;
            }
            const double t = std::max( InHalfLife, 1e-6 );
            if( !( InSubstep > 0.0 ) )
            {
                return std::log( 2.0 ) / t;
            }
            return std::expm1( InSubstep * std::log( 2.0 ) / t ) / InSubstep;
        }

        double DampingAxisSeconds( double InHalfLife )
        {
            for( const double span : { 3.0, 5.0, 10.0, 20.0 } )
            {
                if( InHalfLife * 1.25 <= span )
                {
                    return span;
                }
            }
            return kDampingMaxAxis;
        }
    }

    // ---- Shared -------------------------------------------------------------------------------

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

        double Significant( double InValue, int InDigits )
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

        bool IsLightTheme()
        {
            return ImGuiTheme::Luminance( ImGuiTheme::ActivePalette().Window ) > 0.5f;
        }

        // An axis colour readable on the panel: pulled towards the text colour on light themes.
        ImVec4 AxisInk( int InAxis )
        {
            const ImVec4 axis = ImGui::ColorConvertU32ToFloat4( ImGuiTheme::AxisColor( InAxis ) );
            return IsLightTheme() ? ImGuiTheme::Mix( axis, ImGuiTheme::ActivePalette().Text, 0.3f ) : axis;
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

        bool DoubleClickedThisFrame()
        {
            const ImGuiContext& g = *GImGui;
            return g.IO.MouseClicked[ImGuiMouseButton_Left] && g.IO.MouseClickedCount[ImGuiMouseButton_Left] == 2;
        }

        // A double-click on the last item whose first click landed on it too (a click elsewhere
        // can move the item under the pointer: that double-click is not a reset).
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

        // Labels along a strip, left to right, in one row or staggered over up to three; a label
        // that still overlaps becomes a dot (still clickable).
        struct MarkerLayout
        {
            float LastRight[3] = { -1e9f, -1e9f, -1e9f };
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

        void DashedHorizontal( ImDrawList* InDrawList, float InLeft, float InRight, float InY, ImU32 InColor )
        {
            const float dash = ImGuiTheme::Em( 0.25f );
            for( float x = InLeft; x < InRight; x += dash * 2.f )
            {
                InDrawList->AddLine( ImVec2( x, InY ), ImVec2( std::min( x + dash, InRight ), InY ), InColor, Thin() );
            }
        }

        // A padlock glyph centred at InCenter, InSize tall: closed (filled body, shackle down) or
        // open (outlined body, shackle lifted to one side).
        void Padlock( ImDrawList* InDrawList, ImVec2 InCenter, float InSize, bool InClosed, ImU32 InColor )
        {
            const float bodyW = std::round( InSize * 0.75f );
            const float bodyH = std::round( InSize * 0.5f );
            const ImVec2 bodyMin( std::round( InCenter.x - bodyW * 0.5f ), std::round( InCenter.y + InSize * 0.5f - bodyH ) );
            const ImVec2 bodyMax( bodyMin.x + bodyW, bodyMin.y + bodyH );
            const float radius = bodyW * 0.3f;
            const float lift = InClosed ? 0.f : InSize * 0.18f;
            const float shiftX = InClosed ? 0.f : bodyW * 0.35f;
            const ImVec2 centre( InCenter.x + shiftX, bodyMin.y - InSize * 0.12f - lift );
            InDrawList->PathArcTo( centre, radius, kPi, 2.f * kPi, 10 );
            InDrawList->PathStroke( InColor, 0, Thin() * 1.5f );
            InDrawList->AddLine( ImVec2( centre.x - radius, centre.y ), ImVec2( centre.x - radius, bodyMin.y + ( InClosed ? 0.f : -lift ) ), InColor, Thin() * 1.5f );
            InDrawList->AddLine( ImVec2( centre.x + radius, centre.y ), ImVec2( centre.x + radius, bodyMin.y ), InColor, Thin() * 1.5f );
            if( InClosed )
            {
                InDrawList->AddRectFilled( bodyMin, bodyMax, InColor, Thin() );
            }
            else
            {
                InDrawList->AddRect( bodyMin, bodyMax, InColor, Thin(), 0, Thin() );
            }
        }
    }

    // ---- Mass ladder ----------------------------------------------------------------------------

    bool MassLadder( const char* InCaption, double& InOutKilograms, const NumberSpec& InSpec, double InDefaultKg, bool InAllowAuto )
    {
        using namespace PhysicsMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiContext& g = *GImGui;
        int rungCount = 0;
        const MassRung* rungs = MassRungs( rungCount );
        const bool isAuto = InAllowAuto && !( InOutKilograms > 0.0 );
        bool changed = false;

        // "Auto": a chip before the strip (0 kg derives the mass from the colliders' densities).
        // Its item is added after the strip's, so the caption finds the strip first.
        const ImVec2 start = ImGui::GetCursorScreenPos();
        float chipWidth = 0.f;
        ImRect chip;
        bool chipHovered = false;
        ImGuiID chipId = 0;
        if( InAllowAuto )
        {
            chipWidth = ImGui::CalcTextSize( "Auto" ).x + ImGuiTheme::Em( 0.7f );
            chip = ImRect( start, ImVec2( start.x + chipWidth, start.y + ImGui::GetFrameHeight() ) );
            ImGui::SetCursorScreenPos( ImVec2( start.x + chipWidth + ImGuiTheme::InnerGap(), start.y ) );
        }
        const float stripWidth = std::max( width - ( InAllowAuto ? chipWidth + ImGuiTheme::InnerGap() : 0.f ), ImGuiTheme::Em( 4.f ) );
        const float stripHeight = std::round( ImGui::GetFrameHeight() * 0.5f );
        const float labelTop = stripHeight + ImGuiTheme::Em( 0.2f );

        // As few rows of rung names as let every name show (at most three).
        int rows = 1;
        for( ; rows < 3; ++rows )
        {
            MarkerLayout probe;
            probe.Rows = rows;
            bool all = true;
            for( int i = 0; i < rungCount; ++i )
            {
                bool fits = false;
                probe.Place( static_cast<float>( MassToLadder( rungs[i].Kilograms ) ) * stripWidth, 0.f, rungs[i].Name, 0.f, stripWidth, fits );
                all &= fits;
            }
            if( all )
            {
                break;
            }
        }
        MarkerLayout layout;
        layout.Rows = rows;
        layout.RowStep = ImGui::GetFontSize() + ImGuiTheme::Em( 0.05f );
        const float height = std::max( labelTop + ImGui::GetFontSize() + layout.RowStep * ( rows - 1 ) + ImGuiTheme::Em( 0.1f ), ImGui::GetFrameHeight() );

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( stripWidth, height ), Graph::Axis { kLadderMinKg, kLadderMaxKg, true, Unit::Kilograms }, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect strip( map.Min, ImVec2( map.Max.x, map.Min.y + stripHeight ) );
        auto toX = [&]( double InKg ) { return std::round( strip.Min.x + static_cast<float>( MassToLadder( InKg ) ) * strip.GetWidth() ); };

        // Every rung's label first, so a leader running down to a lower row can step around the
        // names above it (labels placed later can sit over an earlier rung's leader too).
        std::vector<ImRect> rects( static_cast<size_t>( rungCount ) );
        std::vector<char> fitted( static_cast<size_t>( rungCount ), 0 );
        for( int i = 0; i < rungCount; ++i )
        {
            bool fits = false;
            rects[i] = layout.Place( toX( rungs[i].Kilograms ), map.Min.y + labelTop, rungs[i].Name, map.Min.x, map.Max.x, fits );
            fitted[i] = fits ? 1 : 0;
        }
        // The leader from the strip down to rung InIndex's label, broken around other labels.
        auto drawLeader = [&]( int InIndex, float InX, ImU32 InColor )
        {
            const float pad = ImGuiTheme::Em( 0.15f );
            float y = strip.Max.y;
            const float end = rects[InIndex].Min.y;
            // Rows are placed top to bottom, so the labels in the way, by top, are in row order.
            std::vector<ImRect> blocks;
            for( int j = 0; j < rungCount; ++j )
            {
                const ImRect& other = rects[j];
                if( j != InIndex && fitted[j] && InX >= other.Min.x - pad && InX <= other.Max.x + pad && other.Max.y > y && other.Min.y < end )
                {
                    blocks.push_back( other );
                }
            }
            std::sort( blocks.begin(), blocks.end(), []( const ImRect& InA, const ImRect& InB ) { return InA.Min.y < InB.Min.y; } );
            for( const ImRect& block : blocks )
            {
                if( block.Min.y > y )
                {
                    drawList->AddLine( ImVec2( InX, y ), ImVec2( InX, block.Min.y ), InColor, Thin() );
                }
                y = std::max( y, block.Max.y );
            }
            if( end > y )
            {
                drawList->AddLine( ImVec2( InX, y ), ImVec2( InX, end ), InColor, Thin() );
            }
        };

        bool markerScrub = false;
        for( int i = 0; i < rungCount; ++i )
        {
            const MassRung& rung = rungs[i];
            const float x = toX( rung.Kilograms );
            const bool fits = fitted[i] != 0;
            const ImRect rect = rects[i];
            bool hovered = false;
            // A press dragged past the threshold is a scrub, not a click.
            const bool clicked = Graph::MarkerButton( rung.Name, rect, &hovered ) && g.IO.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < g.IO.MouseDragThreshold * g.IO.MouseDragThreshold;
            markerScrub |= ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) && g.IO.MouseDelta.x != 0.f;
            if( clicked )
            {
                const double next = Constrain( rung.Kilograms, InSpec );
                if( next != InOutKilograms )
                {
                    InOutKilograms = next;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, strip.Max.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::Kilograms, rung.Kilograms ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s\n%s", rung.Name, FormatValue( Unit::Kilograms, rung.Kilograms ).c_str(), rung.Note );
            }
            if( visible )
            {
                const bool current = !isAuto && std::abs( InOutKilograms - rung.Kilograms ) <= rung.Kilograms * 1e-3;
                drawLeader( i, x, U32( ImGuiTheme::WithAlpha( palette.Text, 0.25f ) ) );
                DrawMarker( drawList, rect, fits, rung.Name, U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted ) );
            }
        }

        // The value's marker: pressing it doesn't move the value, so a double-click on it resets
        // to the default in one step (a click on the strip itself jumps to the pointer).
        if( !isAuto )
        {
            const float x = toX( InOutKilograms );
            const float reach = ImGuiTheme::Em( 0.4f );
            const ImRect rect( ImVec2( x - reach, strip.Min.y ), ImVec2( x + reach, strip.Max.y ) );
            bool hovered = false;
            Graph::MarkerButton( "Value", rect, &hovered );
            markerScrub |= ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) && g.IO.MouseDelta.x != 0.f;
            if( DoubleClickedLastItem() )
            {
                const double reset = Constrain( InDefaultKg, InSpec );
                if( InOutKilograms != reset )
                {
                    InOutKilograms = reset;
                    changed = true;
                }
                ResetFeedback( canvasId, ImVec2( toX( reset ), strip.GetCenter().y ) );
            }
            else if( hovered && !ImGui::IsItemActive() )
            {
                ImGui::SetTooltip( "%s: drag along the strip, double-click for %s", FormatValue( Unit::Kilograms, InOutKilograms ).c_str(), FormatValue( Unit::Kilograms, InDefaultKg ).c_str() );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::Kilograms, InOutKilograms ) );
        }

        const Graph::CanvasInput input = Graph::End();
        if( input.Pressed || markerScrub || ( input.Held && g.IO.MouseDelta.x != 0.f ) )
        {
            double next = RoundMass( MassFromLadder( ( g.IO.MousePos.x - strip.Min.x ) / std::max( strip.GetWidth(), 1.f ) ) );
            const float reach = ImGuiTheme::Em( 0.35f );
            for( int i = 0; i < rungCount; ++i )
            {
                if( std::abs( toX( rungs[i].Kilograms ) - g.IO.MousePos.x ) <= reach )
                {
                    next = rungs[i].Kilograms;
                }
            }
            next = Constrain( next, InSpec );
            const double from = isAuto ? 0.0 : InOutKilograms;
            bool ticked = false;
            for( int i = 0; i < rungCount; ++i )
            {
                ticked |= Crossed( from, next, rungs[i].Kilograms );
            }
            if( ticked && !input.Pressed )
            {
                Juice::Tick( canvasId, ImVec2( toX( next ), strip.Max.y ) );
            }
            if( next != InOutKilograms )
            {
                InOutKilograms = next;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Mass against everyday things (log scale): feather 10 g to a loaded truck 15 t.\n"
                               "Click a rung or the strip, drag along it (two significant digits, snapping to the rungs);\n"
                               "double-click the value's marker for %s.%s",
                FormatValue( Unit::Kilograms, InDefaultKg ).c_str(), InAllowAuto ? "\nAuto (0 kg) derives the mass from the colliders' densities." : "" );
        }
        if( InAllowAuto )
        {
            if( Graph::MarkerButton( "Auto", chip, &chipHovered ) && !isAuto )
            {
                InOutKilograms = 0.0;
                changed = true;
                Juice::Flash( ImGui::GetItemID() );
                Juice::Play( Juice::Cue::Tick );
            }
            chipId = ImGui::GetItemID();
            ItemLog::SetLastItemRow( InCaption );
            ItemLog::SetItemValue( chipId, FormatValue( Unit::Kilograms, 0.0 ) );
            if( chipHovered )
            {
                ImGui::SetTooltip( "Auto: 0 kg, the mass comes from the colliders' densities (Density x volume)" );
            }
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }

        const bool nowAuto = InAllowAuto && !( InOutKilograms > 0.0 );
        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( strip.Min, strip.Max, U32( palette.Control ), rounding );
            for( int i = 0; i < rungCount; ++i )
            {
                const float x = toX( rungs[i].Kilograms );
                drawList->AddLine( ImVec2( x, strip.Min.y + stripHeight * 0.3f ), ImVec2( x, strip.Max.y - stripHeight * 0.3f ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.3f ) ), Thin() );
            }
            if( !nowAuto )
            {
                const float t = std::clamp( Follow( ImGui::GetID( "##fill" ), static_cast<float>( MassToLadder( InOutKilograms ) ), canvasId ), 0.f, 1.f );
                const float x = std::round( strip.Min.x + t * strip.GetWidth() );
                if( x > strip.Min.x )
                {
                    drawList->AddRectFilled( strip.Min, ImVec2( x, strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) ), rounding, ImDrawFlags_RoundCornersLeft );
                }
                drawList->AddLine( ImVec2( x, strip.Min.y - Thin() ), ImVec2( x, strip.Max.y + Thin() ), U32( palette.Text ), Thin() * 2.f );
            }
            const float tick = Juice::TickAmount( canvasId );
            if( tick > 0.f )
            {
                drawList->AddRectFilled( strip.Min, strip.Max, U32( ImGuiTheme::WithAlpha( palette.Hovered, 0.5f * tick ) ), rounding );
            }
            FlashOutline( drawList, strip, canvasId );
        }
        if( InAllowAuto && ImGui::IsRectVisible( chip.Min, chip.Max ) )
        {
            // Lit when the mass is automatic, like a selected rung.
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( chip.Min, chip.Max, U32( nowAuto ? ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) : chipHovered ? palette.ControlHovered : palette.Control ), rounding );
            drawList->AddRect( chip.Min, chip.Max, U32( nowAuto ? palette.AccentText : palette.Separator ), rounding, 0, Thin() );
            const ImVec2 size = ImGui::CalcTextSize( "Auto" );
            drawList->AddText( ImVec2( std::round( chip.GetCenter().x - size.x * 0.5f ), std::round( chip.GetCenter().y - size.y * 0.5f ) ), U32( nowAuto ? palette.Text : palette.Muted ), "Auto" );
            FlashOutline( drawList, chip, chipId );
        }
        ItemLog::SetItemValue( canvasId, nowAuto ? std::string( "Auto (from density)" ) : FormatValue( Unit::Kilograms, InOutKilograms ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Bounce ---------------------------------------------------------------------------------

    void BouncePreview( const char* InCaption, double InRestitution )
    {
        using namespace PhysicsMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const double e = std::clamp( InRestitution, 0.0, 1.0 );
        // A 1-unit drop takes this long; the picture shows this much of the motion.
        constexpr double kFall = 0.35;
        constexpr double kWindow = 2.4;
        constexpr float kPause = 0.6f;
        constexpr double kRest = 0.004;

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Below = ImGui::GetFontSize() + ImGuiTheme::Em( 0.15f );
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 3.2f ) + frame.Below ), Graph::Axis { 0.0, kWindow, false, Unit::Seconds }, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        const ImGuiID ballId = ImGui::GetID( "##ball" );
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // A new restitution starts the loop over, from the drop.
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID shownKey = ImGui::GetID( "##shown" );
        if( storage->GetFloat( shownKey, -1.f ) != static_cast<float>( e ) )
        {
            storage->SetFloat( shownKey, static_cast<float>( e ) );
            Juice::Restart( ballId );
        }

        const double ratio = std::round( BounceRatio( e ) * 1000.0 ) / 1000.0;
        const std::string reading = "bounces to " + FormatValue( Unit::Percent, ratio );
        if( visible )
        {
            const float radius = ImGuiTheme::Em( 0.28f );
            const float floorY = std::round( map.Max.y - Thin() );
            const float left = map.Min.x + radius + ImGuiTheme::Em( 0.2f );
            const float right = map.Max.x - radius;
            const float top = map.Min.y + radius + Thin();
            auto at = [&]( double InTime, double InHeight ) {
                return ImVec2( left + static_cast<float>( InTime / kWindow ) * ( right - left ), floorY - radius - static_cast<float>( InHeight ) * ( floorY - radius - top ) );
            };
            // Floor.
            drawList->AddLine( ImVec2( map.Min.x, floorY ), ImVec2( map.Max.x, floorY ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.55f ) ), Thin() );
            // The path, faint.
            std::vector<ImVec2> path;
            constexpr int kSamples = 160;
            for( int i = 0; i <= kSamples; ++i )
            {
                const double t = kWindow * i / kSamples;
                path.push_back( at( t, BallHeight( t, e, kFall, kRest ) ) );
            }
            drawList->AddPolyline( path.data(), static_cast<int>( path.size() ), U32( ImGuiTheme::WithAlpha( palette.Muted, 0.6f ) ), ImDrawFlags_None, Thin() );
            // Each bounce's apex: h0 e^(2n), the still picture.
            for( int n = 1; n < 64; ++n )
            {
                const double apex = BounceApexTime( n, e, kFall );
                const double h = BounceHeight( 1.0, e, n );
                if( apex > kWindow || h < kRest )
                {
                    break;
                }
                const ImVec2 p = at( apex, h );
                drawList->AddCircleFilled( p, radius * 0.45f, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.85f ) ) );
                // A dashed rule at the first bounce's height, the ratio the readout gives.
                if( n == 1 )
                {
                    DashedHorizontal( drawList, at( 0.0, h ).x, p.x, p.y, U32( ImGuiTheme::WithAlpha( palette.Text, 0.3f ) ) );
                }
            }
            // The ball: animated through Juice, or resting at its drop point when motion is off.
            const float loop = Juice::LoopTime( ballId, static_cast<float>( kWindow ) + kPause );
            const double t = loop < 0.f ? 0.0 : std::min( static_cast<double>( loop ), kWindow );
            const ImVec2 ball = at( t, BallHeight( t, e, kFall, kRest ) );
            drawList->AddCircleFilled( ball, radius + Thin(), U32( palette.Window ) );
            drawList->AddCircleFilled( ball, radius, U32( palette.AccentText ) );
            drawList->AddCircle( ball, radius + Thin(), U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), 0, Thin() );
            // The readout under the floor.
            drawList->AddText( ImVec2( map.Min.x, floorY + ImGuiTheme::Em( 0.15f ) ), U32( palette.Muted ), reading.c_str() );
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered )
        {
            ImGui::SetTooltip( "A ball dropped on the floor: each bounce leaves at e times the speed it hit with,\n"
                               "so it rises to e^2 of the height it fell from (h_n = h0 e^(2n)); the dots are the apexes.\n"
                               "Two shapes bounce with the larger restitution of the pair (Box3D's default mix)." );
        }
        ItemLog::SetItemValue( canvasId, reading );
        ImGui::PopID();
    }

    // ---- Friction -------------------------------------------------------------------------------

    bool SlideAngleRamp( const char* InCaption, double& InOutFriction, const NumberSpec& InSpec )
    {
        using namespace PhysicsMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        Graph::Frame frame;
        frame.XLabels = false;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 5.f ) ), Graph::Axis(), Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float pad = ImGuiTheme::Em( 0.45f );
        const ImVec2 apex( std::round( map.Min.x + pad ), std::round( map.Max.y - pad * 0.5f ) );
        const float radius = std::max( std::min( map.Max.y - map.Min.y - pad * 1.5f, ( map.Max.x - map.Min.x ) * 0.45f ), ImGuiTheme::Em( 1.f ) );
        const double maxFriction = InSpec.HasRange ? InSpec.Max : 2.0;
        const double maxDegrees = SlideAngleDegrees( maxFriction );
        double degrees = SlideAngleDegrees( InOutFriction );
        auto rad = []( double InDegrees ) { return static_cast<float>( InDegrees ) * kPi / 180.f; };

        if( visible )
        {
            const float a = rad( degrees );
            const ImVec2 dir( std::cos( a ), -std::sin( a ) );
            const ImVec2 crest( apex.x + radius * dir.x, apex.y + radius * dir.y );
            const ImVec2 foot( crest.x, apex.y );
            // Ground, the steepest the friction range allows (faint), and the ramp.
            drawList->AddLine( ImVec2( map.Min.x, apex.y ), ImVec2( apex.x + radius + pad, apex.y ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.55f ) ), Thin() );
            drawList->PathArcTo( apex, radius, -rad( maxDegrees ), 0.f, 24 );
            drawList->PathStroke( U32( ImGuiTheme::WithAlpha( palette.Text, 0.15f ) ), 0, Thin() );
            if( degrees > 0.05 )
            {
                drawList->AddTriangleFilled( apex, crest, foot, U32( ImGuiTheme::WithAlpha( palette.Muted, 0.35f ) ) );
            }
            drawList->AddLine( apex, crest, U32( palette.Text ), Thin() * 1.5f );
            // A block resting on the slope, just about to slide.
            const float block = ImGuiTheme::Em( 0.8f );
            // The slope's normal, pointing up off it (screen Y grows downwards).
            const ImVec2 up( -std::sin( a ), -std::cos( a ) );
            const ImVec2 base( apex.x + dir.x * radius * 0.55f, apex.y + dir.y * radius * 0.55f );
            const ImVec2 c0( base.x - dir.x * block * 0.5f, base.y - dir.y * block * 0.5f );
            const ImVec2 c1( base.x + dir.x * block * 0.5f, base.y + dir.y * block * 0.5f );
            const ImVec2 c2( c1.x + up.x * block, c1.y + up.y * block );
            const ImVec2 c3( c0.x + up.x * block, c0.y + up.y * block );
            drawList->AddQuadFilled( c0, c1, c2, c3, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.85f ) ) );
            drawList->AddQuad( c0, c1, c2, c3, U32( ImGuiTheme::WithAlpha( palette.Text, 0.7f ) ), Thin() );
            // The angle's arc at the foot of the ramp.
            const float arc = ImGuiTheme::Em( 1.3f );
            if( degrees > 0.5 )
            {
                drawList->PathArcTo( apex, arc, -a, 0.f, 16 );
                drawList->PathStroke( U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), 0, Thin() );
            }
            // The readout beside the ramp.
            const float textLeft = apex.x + radius + pad * 2.f;
            const std::string line1 = degrees > 0.05 ? "slides above " + FormatValue( Unit::Degrees, Significant( degrees, 3 ) ) : std::string( "slides on any slope" );
            const std::string line2 = "friction " + FormatNumber( InOutFriction ) + " = tan " + FormatValue( Unit::Degrees, Significant( degrees, 3 ) );
            if( textLeft + ImGui::CalcTextSize( line1.c_str() ).x <= Graph::CanvasRect().Max.x )
            {
                drawList->AddText( ImVec2( textLeft, map.Min.y + Thin() * 2.f ), U32( palette.Text ), line1.c_str() );
                if( textLeft + ImGui::CalcTextSize( line2.c_str() ).x <= Graph::CanvasRect().Max.x )
                {
                    drawList->AddText( ImVec2( textLeft, map.Min.y + ImGui::GetFontSize() + Thin() * 3.f ), U32( palette.Muted ), line2.c_str() );
                }
            }
        }

        bool changed = false;
        if( Graph::AngleHandle( "Angle", apex, 0.f, -1.f, radius, degrees, 0.0, maxDegrees, U32( palette.AccentText ) ) )
        {
            const double friction = Constrain( FrictionForAngle( degrees ), InSpec );
            if( friction != InOutFriction )
            {
                InOutFriction = friction;
                changed = true;
            }
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "A block on a ramp of the same material starts to slide once the slope passes atan(friction).\n"
                               "Drag the Angle handle to set friction = tan(angle) (whole degrees, Shift for tenths).\n"
                               "Two shapes in contact use sqrt(a x b) of their frictions (Box3D's default mix)." );
        }
        const double shown = SlideAngleDegrees( InOutFriction );
        ItemLog::SetItemValue( canvasId, shown > 0.05 ? "slides above " + FormatValue( Unit::Degrees, Significant( shown, 3 ) ) : std::string( "slides on any slope" ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Damping --------------------------------------------------------------------------------

    bool DampingGraph( const char* InCaption, double& InOutDamping, const NumberSpec& InSpec, double InSubstep, const char* InWhat )
    {
        using namespace PhysicsMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const double damping = std::max( InOutDamping, 0.0 );
        const double halfLife = DampingHalfLife( damping, InSubstep );

        // The time axis fits the half-life (3 .. 30 s); it holds still under a dragged handle,
        // then eases over (as the light falloff's range axis does).
        static ImGuiID heldFor = 0;
        static double heldSpan = 0.0;
        static std::unordered_map<ImGuiID, double> lastSpans;
        const ImGuiID handleId = Graph::HandleId( InCaption, "Half-life" );
        const ImGuiID spanId = ImGui::GetID( "##span" );
        const double target = DampingAxisSeconds( halfLife );
        double span = target;
        if( GImGui->ActiveId == handleId )
        {
            if( heldFor != handleId )
            {
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
        span = std::clamp( span, 1.0, kDampingMaxAxis );
        lastSpans[handleId] = span;

        const Graph::Axis xAxis { 0.0, span, false, Unit::Seconds };
        const Graph::Axis yAxis { 0.0, 1.0, false, Unit::Percent };
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 5.f ) ), xAxis, yAxis );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const std::string what = InWhat ? InWhat : "speed";

        if( visible )
        {
            Graph::Grid( true, false );
            DashedHorizontal( drawList, map.Min.x, map.Max.x, std::round( map.YToScreen( 0.5 ) ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.35f ) ) );
            // exp(-d t), what the per-substep factor approximates, faint behind the engine's curve.
            Graph::Plot( [&]( double t ) { return std::exp( -damping * t ); }, U32( ImGuiTheme::WithAlpha( palette.Muted, 0.8f ) ), false, 64, Thin() );
            Graph::Plot( [&]( double t ) { return DampedSpeed( damping, t, InSubstep ); }, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.95f ) ), true, 96, Thin() * 2.f );
            const std::string reading = std::isfinite( halfLife ) ? "half " + what + " after " + FormatValue( Unit::Seconds, Significant( halfLife, 2 ) ) : "no damping: keeps its " + what;
            const ImVec2 size = ImGui::CalcTextSize( reading.c_str() );
            drawList->AddText( ImVec2( std::max( map.Max.x - size.x - ImGuiTheme::Em( 0.25f ), map.Min.x ), map.Min.y + Thin() * 2.f ), U32( palette.Text ), reading.c_str() );
        }

        double ticks[16];
        Graph::HandleSpec spec;
        const double maxDamping = InSpec.HasRange ? InSpec.Max : 100.0;
        spec.MinX = std::max( Significant( DampingHalfLife( maxDamping, InSubstep ), 2 ), 0.01 );
        spec.MaxX = xAxis.Max;
        spec.MinY = 0.5;
        spec.MaxY = 0.5;
        spec.Detents = ticks;
        spec.DetentCount = Graph::Ticks( xAxis, ticks, 16 );
        // Past the axis (or never): the handle sits pinned at the right edge, where a drag starts
        // from the edge's own half-life; chevrons say so and it reports the true value.
        const bool pinned = !( halfLife <= spec.MaxX * 1.0001 );
        spec.ShowValue = !pinned;
        double x = pinned ? spec.MaxX : std::clamp( halfLife, spec.MinX, spec.MaxX );
        double y = 0.5;
        bool changed = false;
        if( Graph::DragHandle( "Half-life", &x, &y, spec ) )
        {
            const double next = Constrain( DampingForHalfLife( Significant( x, 2 ), InSubstep ), InSpec );
            if( next != InOutDamping )
            {
                InOutDamping = next;
                changed = true;
            }
        }
        const double nowHalf = DampingHalfLife( std::max( InOutDamping, 0.0 ), InSubstep );
        const std::string halfText = std::isfinite( nowHalf ) ? FormatValue( Unit::Seconds, Significant( nowHalf, 2 ) ) : std::string( "never" );
        ItemLog::SetItemValue( handleId, halfText );
        if( pinned && !changed && visible )
        {
            // Two chevrons before the handle, pointing off the graph (the reading above gives
            // the true half-life).
            const float yMid = std::round( map.YToScreen( 0.5 ) );
            const float arrow = ImGuiTheme::Em( 0.3f );
            const ImU32 ink = U32( palette.AccentText );
            float tip = map.Max.x - ImGuiTheme::Em( 0.55f );
            for( int i = 0; i < 2; ++i, tip -= arrow * 0.9f )
            {
                drawList->AddTriangleFilled( ImVec2( tip, yMid ), ImVec2( tip - arrow, yMid - arrow ), ImVec2( tip - arrow, yMid + arrow ), ink );
            }
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "How much %s is left after a push, with nothing else acting on the body.\n"
                               "The engine damps every substep (h = %s): v *= 1 / (1 + h d), so v(t) = (1 + h d)^(-t / h);\n"
                               "the faint curve is exp(-d t), which that approximates.\n"
                               "Drag Half-life along the dashed line to set the damping that halves it in that time.",
                what.c_str(), FormatValue( Unit::Seconds, InSubstep ).c_str() );
        }
        ItemLog::SetItemValue( canvasId, "half-life " + halfText );
        ImGui::PopID();
        return changed;
    }

    // ---- Axis locks -----------------------------------------------------------------------------

    bool AxisLocks( const char* InCaption, bool* InOutLocks, const char* const* InNames, const int* InAxes, int InCount, bool InRotation )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float height = ImGui::GetFrameHeight();
        const float chipWidth = ImGuiTheme::Em( 2.6f );
        const float gap = ImGuiTheme::InnerGap();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float rounding = ImGui::GetStyle().FrameRounding;
        bool changed = false;
        std::string lockedNames;

        for( int i = 0; i < InCount; ++i )
        {
            const ImRect rect( ImVec2( start.x + i * ( chipWidth + gap ), start.y ), ImVec2( start.x + i * ( chipWidth + gap ) + chipWidth, start.y + height ) );
            const ImGuiID id = window->GetID( InNames[i] );
            bool hovered = false;
            bool held = false;
            bool pressed = false;
            if( ImGui::ItemAdd( rect, id ) )
            {
                pressed = ImGui::ButtonBehavior( rect, id, &hovered, &held );
            }
            IMGUI_TEST_ENGINE_ITEM_INFO( id, InNames[i], g.LastItemData.StatusFlags );
            ItemLog::SetLastItemRow( InCaption );
            if( pressed )
            {
                InOutLocks[i] = !InOutLocks[i];
                changed = true;
                ImGui::MarkItemEdited( id );
                Juice::Flash( id );
                Juice::Play( Juice::Cue::Tick );
            }
            const bool locked = InOutLocks[i];
            ItemLog::SetItemValue( id, locked ? "locked" : "free" );
            if( hovered )
            {
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
                if( locked )
                {
                    ImGui::SetTooltip( "%s %s: locked, the body can't %s %s it. Click to free it.", InNames[i], InRotation ? "rotation" : "position", InRotation ? "turn about" : "move along", InNames[i] );
                }
                else
                {
                    ImGui::SetTooltip( "%s %s: free. Click to lock it.", InNames[i], InRotation ? "rotation" : "position" );
                }
            }
            if( locked )
            {
                lockedNames += lockedNames.empty() ? InNames[i] : std::string( ", " ) + InNames[i];
            }
            if( !ImGui::IsRectVisible( rect.Min, rect.Max ) )
            {
                continue;
            }
            const ImVec4 axis = AxisInk( InAxes[i] );
            const ImVec4 background = locked ? ImGuiTheme::Mix( palette.Control, ImGui::ColorConvertU32ToFloat4( ImGuiTheme::AxisColor( InAxes[i] ) ), 0.3f )
                                             : ( held ? palette.ControlPressed : hovered ? palette.ControlHovered : palette.Control );
            drawList->AddRectFilled( rect.Min, rect.Max, U32( background ), rounding );
            drawList->AddRect( rect.Min, rect.Max, U32( locked ? axis : palette.Separator ), rounding, 0, locked ? Thin() * 1.5f : Thin() );
            // The axis letter, and a padlock: closed when locked, open and faint when free.
            const ImVec2 letterSize = ImGui::CalcTextSize( InNames[i] );
            const float glyph = std::round( ImGui::GetFontSize() * 0.7f );
            const float contentWidth = letterSize.x + ImGuiTheme::Em( 0.3f ) + glyph;
            const float left = std::round( rect.GetCenter().x - contentWidth * 0.5f );
            drawList->AddText( ImVec2( left, std::round( rect.GetCenter().y - letterSize.y * 0.5f ) ), U32( locked ? palette.Text : axis ), InNames[i] );
            Padlock( drawList, ImVec2( left + letterSize.x + ImGuiTheme::Em( 0.3f ) + glyph * 0.5f, rect.GetCenter().y + Thin() ), glyph, locked,
                U32( locked ? palette.Text : ImGuiTheme::WithAlpha( palette.Muted, 0.8f ) ) );
            const float flash = Juice::FlashAmount( id );
            if( flash > 0.f )
            {
                drawList->AddRect( rect.Min, rect.Max, U32( ImGuiTheme::WithAlpha( palette.Text, flash ) ), rounding, 0, Thin() * 2.f );
            }
        }
        // What is locked, after the chips.
        const float chipsRight = start.x + InCount * ( chipWidth + gap );
        const std::string summary = lockedNames.empty() ? std::string( "free" ) : lockedNames + " locked";
        const ImVec2 summarySize = ImGui::CalcTextSize( summary.c_str() );
        if( chipsRight + ImGuiTheme::Em( 0.2f ) + summarySize.x <= start.x + width )
        {
            drawList->AddText( ImVec2( chipsRight + ImGuiTheme::Em( 0.2f ), std::round( start.y + ( height - summarySize.y ) * 0.5f ) ), U32( palette.Muted ), summary.c_str() );
        }
        ImGui::ItemSize( ImRect( start, ImVec2( chipsRight, start.y + height ) ) );
        ImGui::PopID();
        return changed;
    }
}
