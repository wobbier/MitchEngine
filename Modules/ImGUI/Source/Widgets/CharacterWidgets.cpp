#include "CharacterWidgets.h"
#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace MeWidgets
{
    // ---- Math ---------------------------------------------------------------------------------

    namespace CharacterMath
    {
        namespace
        {
            constexpr double kPiD = 3.14159265358979323846;
        }

        double CapsuleHeight( double InHeight, double InRadius )
        {
            return std::max( InHeight, 2.0 * std::max( InRadius, 0.0 ) );
        }

        void SetCapsuleRadius( double InRadius, double& InOutRadius, double& InOutHeight )
        {
            InOutRadius = std::max( InRadius, 0.0 );
            InOutHeight = CapsuleHeight( InOutHeight, InOutRadius );
        }

        double CapsuleCylinder( double InHeight, double InRadius )
        {
            return std::max( InHeight - 2.0 * std::max( InRadius, 0.0 ), 0.0 );
        }

        double SilhouetteSpan( double InHeight )
        {
            const double needed = std::max( InHeight, kHumanHeight ) * 1.125;
            for( const double span : { 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 15.0, 20.0, 25.0 } )
            {
                if( needed <= span )
                {
                    return span;
                }
            }
            return std::ceil( needed / 5.0 ) * 5.0;
        }

        double JumpGravity( double InGravityY, double InGravityScale )
        {
            return InGravityY * InGravityScale;
        }

        double TakeoffSpeed( double InGravity, double InHeight )
        {
            return std::sqrt( 2.0 * std::max( InGravity, 0.0 ) * std::max( InHeight, 0.0 ) );
        }

        double AirTime( double InGravity, double InHeight )
        {
            if( !( InHeight > 0.0 ) )
            {
                return 0.0;
            }
            if( !( InGravity > 0.0 ) )
            {
                return std::numeric_limits<double>::infinity();
            }
            return 2.0 * TakeoffSpeed( InGravity, InHeight ) / InGravity;
        }

        double JumpDistance( double InSpeed, double InGravity, double InHeight )
        {
            const double t = AirTime( InGravity, InHeight );
            const double speed = std::max( InSpeed, 0.0 );
            // Without gravity the cores give the jump no takeoff speed: it never leaves.
            if( !( speed > 0.0 ) || !( t > 0.0 ) || !std::isfinite( t ) )
            {
                return 0.0;
            }
            return speed * t;
        }

        double JumpHeightAt( double InDistance, double InSpeed, double InGravity, double InHeight )
        {
            const double h = std::max( InHeight, 0.0 );
            const double total = AirTime( InGravity, h );
            const double speed = std::max( InSpeed, 0.0 );
            if( !( h > 0.0 ) || !std::isfinite( total ) || !( speed > 0.0 ) )
            {
                return 0.0;
            }
            const double t = InDistance / speed;
            if( t < 0.0 || t > total )
            {
                return 0.0;
            }
            const double fromApex = t - total * 0.5;
            return std::max( h - 0.5 * InGravity * fromApex * fromApex, 0.0 );
        }

        const HeightMark* JumpMarks( int& OutCount )
        {
            static const HeightMark marks[] = {
                { "Curb", 0.15, "A kerb, a stair step is about 0.18 m" },
                { "Table", 0.75, "A table or desk top" },
                { "Human", kHumanHeight, "A tall adult" },
            };
            OutCount = static_cast<int>( sizeof( marks ) / sizeof( marks[0] ) );
            return marks;
        }

        double SlopeGrade( double InDegrees )
        {
            return std::tan( std::clamp( InDegrees, 0.0, 89.999 ) * kPiD / 180.0 );
        }

        const SlopeMark* SlopeMarks( int& OutCount )
        {
            static const SlopeMark marks[] = {
                { "Ramp", std::atan( 1.0 / 12.0 ) * 180.0 / kPiD, "An accessible ramp: 1 in 12 (ADA), 4.8\xC2\xB0" },
                { "Stairs", 35.0, "A typical staircase, 30\xC2\xB0 to 37\xC2\xB0" },
                { "Steep", 60.0, "Steeper than people walk up without their hands" },
            };
            OutCount = static_cast<int>( sizeof( marks ) / sizeof( marks[0] ) );
            return marks;
        }

        const PaceRung* PaceRungs( int& OutCount )
        {
            static const PaceRung rungs[] = {
                { "Walk", 1.4, "A relaxed walk, 5 km/h" },
                { "Jog", 3.0, "A jog, 11 km/h" },
                { "Run", 5.0, "A run, 18 km/h" },
                { "Sprint", 7.5, "A sprint, 27 km/h" },
            };
            OutCount = static_cast<int>( sizeof( rungs ) / sizeof( rungs[0] ) );
            return rungs;
        }

        const PaceRung* PaceRungAt( double InSpeed )
        {
            int count = 0;
            const PaceRung* rungs = PaceRungs( count );
            for( int i = 0; i < count; ++i )
            {
                if( std::abs( InSpeed - rungs[i].MetersPerSecond ) <= 1e-3 )
                {
                    return &rungs[i];
                }
            }
            return nullptr;
        }

        double RoundPace( double InSpeed, bool InFine )
        {
            const double step = InFine ? 0.01 : 0.1;
            const double rounded = std::round( std::max( InSpeed, 0.0 ) / step ) * step;
            return rounded == 0.0 ? 0.0 : rounded;
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
            if( !( std::abs( InValue ) > 0.0 ) || !std::isfinite( InValue ) )
            {
                return InValue;
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

        void TickOverlay( ImDrawList* InDrawList, const ImRect& InRect, ImGuiID InId )
        {
            const float tick = Juice::TickAmount( InId );
            if( tick > 0.f )
            {
                InDrawList->AddRectFilled( InRect.Min, InRect.Max, U32( ImGuiTheme::WithAlpha( ImGuiTheme::ActivePalette().Hovered, 0.5f * tick ) ), ImGui::GetStyle().FrameRounding );
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

        // Whether the last item (a marker or rung) was pressed and is being dragged sideways: the
        // press scrubs the widget like a press on its canvas. Only past the drag threshold (Shift,
        // the fine modifier, scrubs at once), so a click that wobbles a pixel is still a click.
        bool ScrubbingLastItem()
        {
            const ImGuiContext& g = *GImGui;
            const bool dragging = g.IO.KeyShift ? ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) : ImGui::IsMouseDragging( ImGuiMouseButton_Left );
            return ImGui::IsItemActive() && dragging && g.IO.MouseDelta.x != 0.f;
        }

        // A value marker dragged relative to where it was pressed (see AudioWidgets).
        float MarkerPointerX( float InValueX )
        {
            const ImGuiContext& g = *GImGui;
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID key = ImHashStr( "##GrabOffset", 0, g.LastItemData.ID );
            if( ImGui::IsItemActivated() )
            {
                storage->SetFloat( key, InValueX - g.IO.MousePos.x );
            }
            return g.IO.MousePos.x + storage->GetFloat( key, 0.f );
        }

        void RegrabMarker( float InValueX )
        {
            const ImGuiContext& g = *GImGui;
            ImGui::GetStateStorage()->SetFloat( ImHashStr( "##GrabOffset", 0, g.LastItemData.ID ), InValueX - g.IO.MousePos.x );
        }

        // A click that didn't turn into a drag.
        bool NotDragged()
        {
            const ImGuiContext& g = *GImGui;
            return g.IO.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < g.IO.MouseDragThreshold * g.IO.MouseDragThreshold;
        }

        void DashedHorizontal( ImDrawList* InDrawList, float InLeft, float InRight, float InY, ImU32 InColor )
        {
            const float dash = ImGuiTheme::Em( 0.25f );
            for( float x = InLeft; x < InRight; x += dash * 2.f )
            {
                InDrawList->AddLine( ImVec2( x, InY ), ImVec2( std::min( x + dash, InRight ), InY ), InColor, Thin() );
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

        // Two chevrons at InTip pointing InDirection (+1 right, -1 left): a value past the end.
        void Chevrons( ImDrawList* InDrawList, ImVec2 InTip, float InDirection, ImU32 InColor )
        {
            const float arrow = ImGuiTheme::Em( 0.25f );
            float tip = InTip.x;
            for( int i = 0; i < 2; ++i, tip -= InDirection * arrow * 0.9f )
            {
                InDrawList->AddTriangleFilled( ImVec2( tip, InTip.y ), ImVec2( tip - InDirection * arrow, InTip.y - arrow ), ImVec2( tip - InDirection * arrow, InTip.y + arrow ), InColor );
            }
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
                const float left = std::round( std::clamp( InX - size.x * 0.5f, InMinX, std::max( InMinX, InMaxX - size.x ) ) );
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

        // The axis span a graph shows: held still while one of its handles is dragged (so the
        // handle stays under the pointer), then eased over to InTarget through Juice.
        struct HeldSpan
        {
            ImGuiID HeldFor = 0;
            float Held = 0.f;
            float Last = 0.f;
        };

        float SpanFor( ImGuiID InKey, float InTarget, bool InHeld )
        {
            static std::unordered_map<ImGuiID, HeldSpan> spans;
            HeldSpan& state = spans[InKey];
            float span = InTarget;
            if( InHeld )
            {
                if( state.HeldFor != InKey )
                {
                    state.HeldFor = InKey;
                    state.Held = state.Last > 0.f ? state.Last : InTarget;
                }
                span = state.Held;
                Juice::Snap( InKey, span );
            }
            else
            {
                state.HeldFor = 0;
                span = Juice::Spring( InKey, InTarget, 5.f );
            }
            if( !( span > 0.f ) )
            {
                span = InTarget;
            }
            state.Last = span;
            return span;
        }

        bool ActiveIs( ImGuiID InA, ImGuiID InB = 0 )
        {
            const ImGuiID active = GImGui->ActiveId;
            return active != 0 && ( active == InA || active == InB );
        }

        // A person standing on InFoot (screen), InPixelsPerMeter to scale: legs, body, arms, head.
        void DrawPerson( ImDrawList* InDrawList, ImVec2 InFoot, float InPixelsPerMeter, ImU32 InColor )
        {
            auto at = [&]( double InX, double InY ) { return ImVec2( InFoot.x + static_cast<float>( InX ) * InPixelsPerMeter, InFoot.y - static_cast<float>( InY ) * InPixelsPerMeter ); };
            auto box = [&]( double InX0, double InY0, double InX1, double InY1, double InRound ) {
                const ImVec2 a = at( InX0, InY1 );
                const ImVec2 b = at( InX1, InY0 );
                InDrawList->AddRectFilled( a, b, InColor, static_cast<float>( InRound ) * InPixelsPerMeter );
            };
            box( -0.15, 0.0, -0.025, 0.9, 0.05 );
            box( 0.025, 0.0, 0.15, 0.9, 0.05 );
            box( -0.2, 0.84, 0.2, 1.52, 0.08 );
            box( -0.28, 0.82, -0.21, 1.48, 0.035 );
            box( 0.21, 0.82, 0.28, 1.48, 0.035 );
            box( -0.05, 1.5, 0.05, 1.6, 0.0 );
            InDrawList->AddCircleFilled( at( 0.0, 1.8 - 0.115 ), 0.115f * InPixelsPerMeter, InColor, 20 );
        }

        // A small figure (InSize tall) standing upright with its feet at InFoot on a slope going
        // InSlopeDir (unit, screen), mid-stride uphill.
        void DrawWalker( ImDrawList* InDrawList, ImVec2 InFoot, ImVec2 InSlopeDir, float InSize, ImU32 InColor )
        {
            const float w = Thin() * 1.6f;
            const ImVec2 hip( InFoot.x + InSize * 0.03f, InFoot.y - InSize * 0.48f );
            const ImVec2 front( InFoot.x + InSlopeDir.x * InSize * 0.2f, InFoot.y + InSlopeDir.y * InSize * 0.2f );
            const ImVec2 back( InFoot.x - InSlopeDir.x * InSize * 0.16f, InFoot.y - InSlopeDir.y * InSize * 0.16f );
            const ImVec2 knee( hip.x + InSize * 0.12f, hip.y + InSize * 0.2f );
            InDrawList->AddLine( hip, knee, InColor, w );
            InDrawList->AddLine( knee, front, InColor, w );
            InDrawList->AddLine( hip, back, InColor, w );
            // Leaning a little into the hill.
            const ImVec2 neck( hip.x + InSize * 0.07f, hip.y - InSize * 0.32f );
            InDrawList->AddLine( hip, neck, InColor, w );
            const ImVec2 shoulder( neck.x - InSize * 0.01f, neck.y + InSize * 0.05f );
            InDrawList->AddLine( shoulder, ImVec2( shoulder.x + InSize * 0.17f, shoulder.y + InSize * 0.15f ), InColor, w );
            InDrawList->AddLine( shoulder, ImVec2( shoulder.x - InSize * 0.12f, shoulder.y + InSize * 0.2f ), InColor, w );
            InDrawList->AddCircleFilled( ImVec2( neck.x + InSize * 0.03f, neck.y - InSize * 0.11f ), InSize * 0.1f, InColor, 14 );
        }
    }

    // ---- Capsule --------------------------------------------------------------------------------

    bool CapsuleSilhouette( const char* InCaption, double& InOutHeight, double& InOutRadius, const NumberSpec& InHeightSpec, const NumberSpec& InRadiusSpec, bool InCylinder )
    {
        using namespace CharacterMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiID heightId = Graph::HandleId( InCaption, "Height" );
        const ImGuiID radiusId = Graph::HandleId( InCaption, "Radius" );
        const bool held = ActiveIs( heightId, radiusId );

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Below = ImGui::GetFontSize() + ImGuiTheme::Em( 0.2f );
        const float inset = ImGuiTheme::Em( 0.25f );
        const float canvasHeight = ImGuiTheme::Em( 7.5f ) + frame.Below;
        const float plotWidth = std::max( width - inset * 2.f, ImGuiTheme::Em( 4.f ) );
        const float plotHeight = std::max( canvasHeight - inset * 2.f - frame.Below, ImGuiTheme::Em( 2.f ) );

        // Left to right: the metre ruler, the person, a gap, the capsule (centred on x = 0, so
        // the Radius handle's X is the radius). The span fits both sideways too.
        const float ruler = ImGui::CalcTextSize( "10 m" ).x + ImGuiTheme::Em( 0.5f );
        const float gap = ImGuiTheme::Em( 0.8f );
        const double personWidth = 0.6;
        const double radius = std::max( InOutRadius, 0.0 );
        const double height = InCylinder ? std::max( InOutHeight, 0.0 ) : CapsuleHeight( InOutHeight, radius );
        const float room = std::max( plotWidth - ruler - gap * 2.f, ImGuiTheme::Em( 2.f ) );
        const double sideways = ( personWidth + 2.0 * radius ) * plotHeight / room;
        const float target = static_cast<float>( SilhouetteSpan( std::max( height, sideways / 1.125 ) ) );
        const float span = SpanFor( ImGui::GetID( "##span" ), target, held );
        const float ppm = plotHeight / span;
        // The capsule's centre, from the plot's left edge: held with the span while dragging.
        const ImGuiID centreKey = ImGui::GetID( "##centre" );
        const float centreTarget = ruler + static_cast<float>( personWidth ) * ppm + gap + static_cast<float>( radius ) * ppm;
        const float centre = SpanFor( centreKey, centreTarget, held );
        const double xMin = -centre / ppm;
        const double xMax = ( plotWidth - centre ) / ppm;

        const bool visible = Graph::Begin( InCaption, ImVec2( width, canvasHeight ), Graph::Axis { xMin, xMax, false, Unit::Meters }, Graph::Axis { 0.0, span, false, Unit::Meters }, frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float ground = std::round( map.Max.y );

        if( visible )
        {
            drawList->PushClipRect( Graph::CanvasRect().Min, Graph::CanvasRect().Max, true );
            // Metre ticks up the left, faint rules across.
            const double step = span <= 6.0 ? 1.0 : span <= 15.0 ? 2.0 : 5.0;
            for( double m = step; m < span; m += step )
            {
                const float y = std::round( map.YToScreen( m ) );
                drawList->AddLine( ImVec2( map.Min.x + ruler - ImGuiTheme::Em( 0.3f ), y ), ImVec2( map.Max.x, y ), U32( ImGuiTheme::WithAlpha( palette.Separator, 0.5f ) ), Thin() );
                const std::string label = FormatValue( Unit::Meters, m );
                const ImVec2 size = ImGui::CalcTextSize( label.c_str() );
                drawList->AddText( ImVec2( map.Min.x + ruler - ImGuiTheme::Em( 0.4f ) - size.x, y - size.y * 0.5f ), U32( palette.Muted ), label.c_str() );
            }
            // The person, and their height across to the capsule.
            const ImVec2 foot( map.Min.x + ruler + static_cast<float>( personWidth * 0.5 ) * ppm, ground );
            DrawPerson( drawList, foot, ppm, U32( ImGuiTheme::WithAlpha( palette.Muted, 0.55f ) ) );
            DashedHorizontal( drawList, foot.x, map.Max.x, std::round( map.YToScreen( kHumanHeight ) ), U32( ImGuiTheme::WithAlpha( palette.Muted, 0.5f ) ) );

            // The capsule (or cylinder), to scale.
            const ImVec2 lo = map.ToScreen( -radius, 0.0 );
            const ImVec2 hi = map.ToScreen( radius, height );
            const float rounding = InCylinder ? std::min( ImGuiTheme::Em( 0.15f ), static_cast<float>( radius ) * ppm ) : static_cast<float>( radius ) * ppm;
            const ImRect body( ImVec2( lo.x, hi.y ), ImVec2( hi.x, lo.y ) );
            drawList->AddRectFilled( body.Min, body.Max, U32( ImGuiTheme::WithAlpha( palette.Accent, 0.4f ) ), rounding );
            drawList->AddRect( body.Min, body.Max, U32( palette.AccentText ), rounding, 0, Thin() * 1.5f );
            if( !InCylinder )
            {
                // Where the caps meet the straight part.
                const double cylinder = CapsuleCylinder( height, radius );
                for( const double y : { radius, radius + cylinder } )
                {
                    if( cylinder > 0.0 )
                    {
                        const float sy = std::round( map.YToScreen( y ) );
                        DashedHorizontal( drawList, body.Min.x, body.Max.x, sy, U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.45f ) ) );
                    }
                }
            }
            // The centre line and the radius, out to the Radius handle.
            const float axisX = std::round( map.XToScreen( 0.0 ) );
            DashedVertical( drawList, axisX, body.Min.y, body.Max.y, U32( ImGuiTheme::WithAlpha( palette.Text, 0.3f ) ) );
            const float midY = std::round( map.YToScreen( height * 0.5 ) );
            drawList->AddLine( ImVec2( axisX, midY ), ImVec2( map.XToScreen( radius ), midY ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), Thin() );
            // The ground.
            drawList->AddLine( ImVec2( map.Min.x + ruler - ImGuiTheme::Em( 0.3f ), ground ), ImVec2( map.Max.x, ground ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), Thin() );
            drawList->PopClipRect();
        }

        bool changed = false;
        Graph::HandleSpec heightSpec;
        heightSpec.MinX = 0.0;
        heightSpec.MaxX = 0.0;
        heightSpec.MinY = std::max( InCylinder ? 0.0 : 2.0 * radius, InHeightSpec.HasRange ? InHeightSpec.Min : 0.0 );
        heightSpec.MaxY = InHeightSpec.HasRange ? InHeightSpec.Max : 1e6;
        heightSpec.ReportY = true;
        heightSpec.Step = 0.01;
        const double humanMark[] = { kHumanHeight };
        heightSpec.Detents = humanMark;
        heightSpec.DetentCount = 1;
        heightSpec.SnapToDetents = true;
        double hx = 0.0;
        double hy = height;
        if( Graph::DragHandle( "Height", &hx, &hy, heightSpec ) )
        {
            double next = Constrain( hy, InHeightSpec );
            if( !InCylinder )
            {
                next = CapsuleHeight( next, radius );
            }
            if( next != InOutHeight )
            {
                InOutHeight = next;
                changed = true;
            }
        }
        Graph::HandleSpec radiusSpec;
        radiusSpec.MinX = InRadiusSpec.HasRange ? InRadiusSpec.Min : 0.0;
        radiusSpec.MaxX = InRadiusSpec.HasRange ? InRadiusSpec.Max : 1e6;
        radiusSpec.MinY = height * 0.5;
        radiusSpec.MaxY = height * 0.5;
        radiusSpec.Step = 0.01;
        double rx = radius;
        double ry = height * 0.5;
        if( Graph::DragHandle( "Radius", &rx, &ry, radiusSpec ) )
        {
            double nextRadius = InOutRadius;
            double nextHeight = InOutHeight;
            if( InCylinder )
            {
                nextRadius = Constrain( rx, InRadiusSpec );
            }
            else
            {
                SetCapsuleRadius( Constrain( rx, InRadiusSpec ), nextRadius, nextHeight );
                nextHeight = Constrain( nextHeight, InHeightSpec );
                nextRadius = std::min( nextRadius, nextHeight * 0.5 );
            }
            if( nextRadius != InOutRadius || nextHeight != InOutHeight )
            {
                InOutRadius = nextRadius;
                InOutHeight = nextHeight;
                changed = true;
            }
        }

        const double shownHeight = InCylinder ? InOutHeight : CapsuleHeight( InOutHeight, InOutRadius );
        const std::string reading = FormatValue( Unit::Meters, shownHeight ) + " x " + FormatValue( Unit::Meters, InOutRadius );
        if( visible )
        {
            const std::string note = InCylinder ? "  tall x radius" : "  end to end x radius";
            const ImVec2 at( map.Min.x, map.Max.y + ImGuiTheme::Em( 0.2f ) );
            drawList->AddText( at, U32( palette.Text ), reading.c_str() );
            const float noteX = at.x + ImGui::CalcTextSize( reading.c_str() ).x;
            if( noteX + ImGui::CalcTextSize( note.c_str() ).x <= Graph::CanvasRect().Max.x )
            {
                drawList->AddText( ImVec2( noteX, at.y ), U32( palette.Muted ), note.c_str() );
            }
            FlashOutline( drawList, Graph::CanvasRect(), canvasId );
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            if( InCylinder )
            {
                ImGui::SetTooltip( "The agent's cylinder to scale beside a %s person (the navmesh is baked for agent types of a size).\n"
                                   "Drag Height (the top) and Radius (the side) in centimetres, Shift for millimetres.",
                    FormatValue( Unit::Meters, kHumanHeight ).c_str() );
            }
            else
            {
                ImGui::SetTooltip( "The character's capsule to scale beside a %s person: Height is end to end, caps included,\n"
                                   "so it never goes under 2 x Radius (dragging Radius out pushes Height up).\n"
                                   "Drag Height (the top) and Radius (the side) in centimetres, Shift for millimetres.",
                    FormatValue( Unit::Meters, kHumanHeight ).c_str() );
            }
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        ItemLog::SetItemValue( canvasId, reading );
        ImGui::PopID();
        return changed;
    }

    // ---- Slope ----------------------------------------------------------------------------------

    bool SlopeWedge( const char* InCaption, double& InOutDegrees, const NumberSpec& InSpec )
    {
        using namespace CharacterMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiContext& g = *GImGui;
        Graph::Frame frame;
        frame.XLabels = false;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 6.f ) ), Graph::Axis(), Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float pad = ImGuiTheme::Em( 0.45f );
        const ImVec2 apex( std::round( map.Min.x + pad ), std::round( map.Max.y - pad * 0.5f ) );
        const float radius = std::max( std::min( map.Max.y - map.Min.y - pad * 1.5f, ( map.Max.x - map.Min.x ) * 0.42f ), ImGuiTheme::Em( 1.f ) );
        const double minDegrees = InSpec.HasRange ? InSpec.Min : 0.0;
        const double maxDegrees = InSpec.HasRange ? InSpec.Max : 89.0;
        double degrees = std::clamp( InOutDegrees, minDegrees, maxDegrees );
        auto rad = []( double InDegrees ) { return static_cast<float>( InDegrees ) * kPi / 180.f; };
        int markCount = 0;
        const SlopeMark* marks = SlopeMarks( markCount );
        float markWidth = 0.f;
        for( int i = 0; i < markCount; ++i )
        {
            markWidth = std::max( markWidth, ImGui::CalcTextSize( marks[i].Name ).x );
        }
        bool changed = false;

        if( visible )
        {
            const float a = rad( degrees );
            const ImVec2 dir( std::cos( a ), -std::sin( a ) );
            const ImVec2 crest( apex.x + radius * dir.x, apex.y + radius * dir.y );
            const ImVec2 foot( crest.x, apex.y );
            // Steeper than the limit is a wall: the rest of the quarter, faint.
            drawList->PathArcTo( apex, radius, -kPi * 0.5f, -a, 24 );
            drawList->PathStroke( U32( ImGuiTheme::WithAlpha( palette.Text, 0.15f ) ), 0, Thin() );
            drawList->AddLine( ImVec2( map.Min.x, apex.y ), ImVec2( apex.x + radius + pad, apex.y ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.55f ) ), Thin() );
            if( degrees > 0.05 )
            {
                drawList->AddTriangleFilled( apex, crest, foot, U32( ImGuiTheme::WithAlpha( palette.Accent, 0.35f ) ) );
            }
            drawList->AddLine( apex, crest, U32( palette.Text ), Thin() * 1.5f );
            // A small figure walking up it.
            const float figure = std::min( ImGuiTheme::Em( 1.4f ), radius * 0.4f );
            const ImVec2 feet( apex.x + dir.x * radius * 0.5f, apex.y + dir.y * radius * 0.5f );
            DrawWalker( drawList, feet, dir, figure, U32( ImGuiTheme::WithAlpha( palette.Text, 0.85f ) ) );
            // The angle's arc at the foot.
            const float arc = ImGuiTheme::Em( 1.3f );
            if( degrees > 0.5 )
            {
                drawList->PathArcTo( apex, arc, -a, 0.f, 16 );
                drawList->PathStroke( U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), 0, Thin() );
            }
            // The readout beside the wedge.
            // The readout past the marks' names.
            const float textLeft = std::round( apex.x + radius + ImGuiTheme::Em( 0.55f ) + markWidth + pad * 1.5f );
            const std::string line1 = "walks up " + FormatValue( Unit::Degrees, Significant( degrees, 3 ) );
            const std::string line2 = FormatValue( Unit::Percent, Significant( SlopeGrade( degrees ), 3 ) ) + " grade";
            const std::string line3 = "steeper is a wall";
            float y = map.Min.y + Thin() * 2.f;
            for( const std::string* line : { &line1, &line2, &line3 } )
            {
                if( textLeft + ImGui::CalcTextSize( line->c_str() ).x > Graph::CanvasRect().Max.x )
                {
                    break;
                }
                drawList->AddText( ImVec2( textLeft, y ), U32( line == &line1 ? palette.Text : palette.Muted ), line->c_str() );
                y += ImGui::GetFontSize() + Thin();
            }
        }

        // The marks: a tick past the arc and a clickable name.
        for( int i = 0; i < markCount; ++i )
        {
            const SlopeMark& mark = marks[i];
            const ImVec2 inner = Graph::ArcPoint( apex, 0.f, -rad( mark.Degrees ), radius + ImGuiTheme::Em( 0.1f ) );
            const ImVec2 outer = Graph::ArcPoint( apex, 0.f, -rad( mark.Degrees ), radius + ImGuiTheme::Em( 0.4f ) );
            const ImVec2 size = ImGui::CalcTextSize( mark.Name );
            const ImVec2 textPos( std::round( outer.x + ImGuiTheme::Em( 0.15f ) ), std::round( std::min( outer.y - size.y * 0.5f, apex.y - size.y - Thin() * 2.f ) ) );
            const ImRect rect( textPos, ImVec2( textPos.x + size.x, textPos.y + size.y ) );
            bool hovered = false;
            const bool clicked = Graph::MarkerButton( mark.Name, rect, &hovered ) && NotDragged();
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::Degrees, Significant( mark.Degrees, 2 ) ) );
            if( clicked )
            {
                const double next = Constrain( std::round( mark.Degrees * 10.0 ) / 10.0, InSpec );
                if( next != InOutDegrees )
                {
                    InOutDegrees = next;
                    degrees = next;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, outer );
            }
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s\n%s", mark.Name, FormatValue( Unit::Degrees, Significant( mark.Degrees, 2 ) ).c_str(), mark.Note );
            }
            if( visible )
            {
                const bool on = std::abs( InOutDegrees - mark.Degrees ) < 0.05;
                drawList->AddLine( inner, outer, U32( ImGuiTheme::WithAlpha( palette.Text, 0.5f ) ), Thin() );
                drawList->AddText( rect.Min, U32( on ? palette.AccentText : hovered ? palette.Text : palette.Muted ), mark.Name );
            }
        }

        if( Graph::AngleHandle( "Slope", apex, 0.f, -1.f, radius, degrees, minDegrees, maxDegrees, U32( palette.AccentText ) ) )
        {
            const double next = Constrain( degrees, InSpec );
            if( next != InOutDegrees )
            {
                InOutDegrees = next;
                changed = true;
            }
        }
        if( visible )
        {
            FlashOutline( drawList, Graph::CanvasRect(), canvasId );
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held && g.HoveredId == canvasId )
        {
            ImGui::SetTooltip( "The steepest ground the character walks up; steeper surfaces count as walls it slides along.\n"
                               "Drag the Slope handle (whole degrees, Shift for tenths) or click a mark:\n"
                               "Ramp is an accessible 1 in 12 (4.8\xC2\xB0), Stairs 35\xC2\xB0, Steep 60\xC2\xB0." );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        ItemLog::SetItemValue( canvasId, "walks up " + FormatValue( Unit::Degrees, Significant( std::clamp( InOutDegrees, minDegrees, maxDegrees ), 3 ) ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Jump -----------------------------------------------------------------------------------

    namespace
    {
        // The jump arc's vertical span: the first of these with a fifth to spare over the
        // jump (and the person mark).
        double JumpSpan( double InHeight )
        {
            const double needed = std::max( InHeight, CharacterMath::kHumanHeight ) * 1.2;
            for( const double span : { 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 15.0, 20.0, 25.0 } )
            {
                if( needed <= span )
                {
                    return span;
                }
            }
            return std::ceil( needed / 5.0 ) * 5.0;
        }

        // The horizontal span: a nice length over the jump's reach.
        double ReachSpan( double InDistance )
        {
            const double needed = std::max( InDistance * 1.1, 1.0 );
            const double power = std::pow( 10.0, std::floor( std::log10( needed ) ) );
            for( const double m : { 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0 } )
            {
                if( needed <= m * power )
                {
                    return m * power;
                }
            }
            return 10.0 * power;
        }

        std::string JumpReading( double InHeight, double InGravity )
        {
            using namespace CharacterMath;
            if( !( InHeight > 0.0 ) )
            {
                return "no jump";
            }
            if( InGravity < 0.0 )
            {
                return "gravity pulls up: it never lands";
            }
            if( !( InGravity > 0.0 ) )
            {
                return "no gravity: the jump does nothing";
            }
            return FormatValue( Unit::Meters, InHeight ) + " apex, " + FormatValue( Unit::MetersPerSecond, Significant( TakeoffSpeed( InGravity, InHeight ), 3 ) ) + " takeoff, "
                + FormatValue( Unit::Seconds, Significant( AirTime( InGravity, InHeight ), 3 ) ) + " in the air";
        }
    }

    bool JumpArc( const char* InCaption, double& InOutHeight, const NumberSpec& InSpec, double InSpeed, double InGravity )
    {
        using namespace CharacterMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiID apexId = Graph::HandleId( InCaption, "Apex" );
        const bool held = ActiveIs( apexId );
        const double height = std::max( InOutHeight, 0.0 );
        const double gravity = std::max( InGravity, 0.0 );
        const double speed = std::max( InSpeed, 0.0 );
        const double distance = JumpDistance( speed, gravity, height );

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Below = ImGui::GetFontSize() * 2.f + ImGuiTheme::Em( 0.35f );
        const float ySpan = SpanFor( ImGui::GetID( "##height" ), static_cast<float>( JumpSpan( height ) ), held );
        const float xSpan = SpanFor( ImGui::GetID( "##reach" ), static_cast<float>( ReachSpan( distance ) ), held );
        // The arc runs between the marks' names on the left and the right edge, centred on its
        // apex, so the Apex handle's X never moves.
        int markCount = 0;
        const HeightMark* marks = JumpMarks( markCount );
        float labelWidth = 0.f;
        for( int i = 0; i < markCount; ++i )
        {
            labelWidth = std::max( labelWidth, ImGui::CalcTextSize( marks[i].Name ).x );
        }
        const float inset = ImGuiTheme::Em( 0.25f );
        const float left = labelWidth + ImGuiTheme::Em( 0.5f );
        const float plotWidth = std::max( width - inset * 2.f, ImGuiTheme::Em( 4.f ) );
        const float arcWidth = std::max( plotWidth - left - ImGuiTheme::Em( 0.5f ), ImGuiTheme::Em( 2.f ) );
        const double ppm = arcWidth / xSpan;
        const double xMin = -left / ppm;
        const double xMax = ( plotWidth - left ) / ppm;
        const double apexX = xSpan * 0.5;
        const double takeoff = apexX - distance * 0.5;

        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 5.5f ) + frame.Below ), Graph::Axis { xMin, xMax, false, Unit::Meters }, Graph::Axis { 0.0, ySpan, false, Unit::Meters }, frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float ground = std::round( map.Max.y );

        if( visible )
        {
            drawList->PushClipRect( Graph::CanvasRect().Min, Graph::CanvasRect().Max, true );
            // The marks: a dashed rule at each height, named on the left (a name that would
            // overlap the one below is left out).
            float lastTop = ground + ImGui::GetFontSize() * 2.f;
            for( int i = 0; i < markCount; ++i )
            {
                const float y = std::round( map.YToScreen( marks[i].Meters ) );
                if( y < map.Min.y )
                {
                    continue;
                }
                const bool reached = height >= marks[i].Meters - 1e-6;
                DashedHorizontal( drawList, map.Min.x + left - ImGuiTheme::Em( 0.3f ), map.Max.x, y, U32( ImGuiTheme::WithAlpha( palette.Muted, reached ? 0.6f : 0.35f ) ) );
                const float top = y - ImGui::GetFontSize() - Thin();
                if( top + ImGui::GetFontSize() <= lastTop )
                {
                    drawList->AddText( ImVec2( map.Min.x, top ), U32( reached ? palette.Text : palette.Muted ), marks[i].Name );
                    lastTop = top;
                }
            }
            // The arc, filled faintly underneath.
            const ImU32 ink = U32( palette.AccentText );
            if( height > 0.0 && gravity > 0.0 )
            {
                if( distance > 0.0 )
                {
                    std::vector<double> xs;
                    std::vector<double> ys;
                    constexpr int kSamples = 64;
                    for( int i = 0; i <= kSamples; ++i )
                    {
                        const double d = distance * i / kSamples;
                        xs.push_back( takeoff + d );
                        ys.push_back( JumpHeightAt( d, speed, gravity, height ) );
                    }
                    Graph::PlotSamples( xs.data(), ys.data(), static_cast<int>( xs.size() ), U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.95f ) ), true, Thin() * 2.f );
                    drawList->AddCircleFilled( map.ToScreen( takeoff, 0.0 ), ImGuiTheme::Em( 0.18f ), ink );
                    drawList->AddCircle( map.ToScreen( takeoff + distance, 0.0 ), ImGuiTheme::Em( 0.18f ), ink, 0, Thin() * 1.5f );
                }
                else
                {
                    // Standing still: straight up and down.
                    drawList->AddLine( map.ToScreen( apexX, 0.0 ), map.ToScreen( apexX, height ), ink, Thin() * 2.f );
                }
                // From the apex down to the ground, and its height beside it.
                const float apexScreenX = std::round( map.XToScreen( apexX ) );
                DashedVertical( drawList, apexScreenX, map.YToScreen( height ), ground, U32( ImGuiTheme::WithAlpha( palette.Text, 0.4f ) ) );
                if( !held )
                {
                    const std::string apexText = FormatValue( Unit::Meters, height );
                    const ImVec2 size = ImGui::CalcTextSize( apexText.c_str() );
                    const float y = std::clamp( map.YToScreen( height ) - size.y * 0.5f, map.Min.y, ground - size.y );
                    drawList->AddText( ImVec2( apexScreenX + ImGuiTheme::Em( 0.55f ), std::round( y ) ), U32( palette.Text ), apexText.c_str() );
                }
            }
            drawList->AddLine( ImVec2( map.Min.x + left - ImGuiTheme::Em( 0.3f ), ground ), ImVec2( map.Max.x, ground ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.6f ) ), Thin() );
            drawList->PopClipRect();
        }

        bool changed = false;
        double detents[8];
        const int detentCount = std::min( markCount, 8 );
        for( int i = 0; i < detentCount; ++i )
        {
            detents[i] = marks[i].Meters;
        }
        Graph::HandleSpec spec;
        spec.MinX = apexX;
        spec.MaxX = apexX;
        spec.MinY = InSpec.HasRange ? InSpec.Min : 0.0;
        spec.MaxY = InSpec.HasRange ? InSpec.Max : 1e6;
        spec.ReportY = true;
        spec.Step = 0.01;
        spec.Detents = detents;
        spec.DetentCount = detentCount;
        spec.SnapToDetents = true;
        double ax = apexX;
        double ay = height;
        if( Graph::DragHandle( "Apex", &ax, &ay, spec ) )
        {
            const double next = Constrain( ay, InSpec );
            if( next != InOutHeight )
            {
                InOutHeight = next;
                changed = true;
            }
        }

        const double nowHeight = std::max( InOutHeight, 0.0 );
        const std::string reading = JumpReading( nowHeight, InGravity );
        if( visible )
        {
            std::string line1;
            std::string line2;
            if( nowHeight > 0.0 && gravity > 0.0 )
            {
                line1 = "takeoff " + FormatValue( Unit::MetersPerSecond, Significant( TakeoffSpeed( gravity, nowHeight ), 3 ) ) + ", " + FormatValue( Unit::Seconds, Significant( AirTime( gravity, nowHeight ), 3 ) ) + " in the air";
                line2 = speed > 0.0 ? "lands " + FormatValue( Unit::Meters, Significant( JumpDistance( speed, gravity, nowHeight ), 3 ) ) + " away at " + FormatValue( Unit::MetersPerSecond, Significant( speed, 3 ) )
                                    : std::string( "straight up: Max Speed is 0" );
            }
            else
            {
                line1 = reading;
            }
            // Lines that would run past the canvas shorten ("4.85 m/s, 0.989 s") or are left out.
            const float right = Graph::CanvasRect().Max.x;
            const auto fits = [&]( const std::string& InLine ) { return map.Min.x + ImGui::CalcTextSize( InLine.c_str() ).x <= right; };
            if( !fits( line1 ) && nowHeight > 0.0 && gravity > 0.0 )
            {
                line1 = FormatValue( Unit::MetersPerSecond, Significant( TakeoffSpeed( gravity, nowHeight ), 3 ) ) + ", " + FormatValue( Unit::Seconds, Significant( AirTime( gravity, nowHeight ), 3 ) );
            }
            const float top = map.Max.y + ImGuiTheme::Em( 0.2f );
            if( fits( line1 ) )
            {
                drawList->AddText( ImVec2( map.Min.x, top ), U32( palette.Text ), line1.c_str() );
            }
            if( !line2.empty() && fits( line2 ) )
            {
                drawList->AddText( ImVec2( map.Min.x, top + ImGui::GetFontSize() + Thin() ), U32( palette.Muted ), line2.c_str() );
            }
            FlashOutline( drawList, Graph::CanvasRect(), canvasId );
        }
        const Graph::CanvasInput input = Graph::End();
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "The jump running at Max Speed: it leaves the ground at v = sqrt(2 g h) and is in the air for 2 v / g,\n"
                               "with g = %s (the project's gravity.y x Gravity Scale, pulling down).\n"
                               "Drag Apex up and down in centimetres (Shift for millimetres); it snaps to the curb, table and person marks.",
                FormatValue( Unit::MetersPerSecondSq, Significant( InGravity, 3 ) ).c_str() );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        ItemLog::SetItemValue( canvasId, reading );
        ImGui::PopID();
        return changed;
    }

    // ---- Pace -----------------------------------------------------------------------------------

    namespace
    {
        std::string PaceText( double InSpeed )
        {
            const CharacterMath::PaceRung* rung = CharacterMath::PaceRungAt( InSpeed );
            std::string text = FormatValue( Unit::MetersPerSecond, InSpeed );
            if( rung )
            {
                std::string name = rung->Name;
                name[0] = static_cast<char>( std::tolower( static_cast<unsigned char>( name[0] ) ) );
                text += ", " + name;
            }
            return text;
        }
    }

    bool PaceStrip( const char* InCaption, double& InOutSpeed, const NumberSpec& InSpec, double InDefault )
    {
        using namespace CharacterMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiContext& g = *GImGui;
        int rungCount = 0;
        const PaceRung* rungs = PaceRungs( rungCount );
        const float stripHeight = std::round( ImGui::GetFrameHeight() * 0.5f );
        const float labelTop = stripHeight + ImGuiTheme::Em( 0.2f );

        // As few rows of names as let every name show (at most three).
        int rows = 1;
        for( ; rows < 3; ++rows )
        {
            MarkerLayout probe;
            probe.Rows = rows;
            bool all = true;
            for( int i = 0; i < rungCount; ++i )
            {
                bool fits = false;
                probe.Place( static_cast<float>( rungs[i].MetersPerSecond / kPaceAxis ) * width, 0.f, rungs[i].Name, 0.f, width, fits );
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
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), Graph::Axis { 0.0, kPaceAxis, false, Unit::MetersPerSecond }, Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect strip( map.Min, ImVec2( map.Max.x, map.Min.y + stripHeight ) );
        auto toX = [&]( double InSpeed ) { return std::round( strip.Min.x + static_cast<float>( std::clamp( InSpeed / kPaceAxis, 0.0, 1.0 ) ) * strip.GetWidth() ); };
        bool changed = false;

        bool scrub = false;
        for( int i = 0; i < rungCount; ++i )
        {
            const PaceRung& rung = rungs[i];
            const float x = toX( rung.MetersPerSecond );
            bool fits = false;
            const ImRect rect = layout.Place( x, map.Min.y + labelTop, rung.Name, map.Min.x, map.Max.x, fits );
            bool hovered = false;
            // A press dragged past the threshold is a scrub, not a click.
            const bool clicked = Graph::MarkerButton( rung.Name, rect, &hovered ) && NotDragged();
            scrub |= ScrubbingLastItem();
            if( clicked )
            {
                const double next = Constrain( rung.MetersPerSecond, InSpec );
                if( next != InOutSpeed )
                {
                    InOutSpeed = next;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( x, strip.Max.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::MetersPerSecond, rung.MetersPerSecond ) );
            if( hovered )
            {
                ImGui::SetTooltip( "%s: %s\n%s", rung.Name, FormatValue( Unit::MetersPerSecond, rung.MetersPerSecond ).c_str(), rung.Note );
            }
            if( visible )
            {
                const bool current = std::abs( InOutSpeed - rung.MetersPerSecond ) <= 1e-3;
                if( fits )
                {
                    drawList->AddLine( ImVec2( x, strip.Max.y ), ImVec2( x, rect.Min.y ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.25f ) ), Thin() );
                    drawList->AddText( rect.Min, U32( current ? palette.AccentText : hovered ? palette.Text : palette.Muted ), rung.Name );
                }
                else
                {
                    drawList->AddCircleFilled( ImVec2( x, rect.Min.y + Thin() * 3.f ), Thin() * 2.f, U32( current ? palette.AccentText : palette.Muted ) );
                }
            }
        }

        // The value's marker: double-click it for the default; dragging it moves the value by
        // the pointer's travel from where it was grabbed.
        float markerPointer = 0.f;
        {
            const float x = toX( InOutSpeed );
            const float reach = ImGuiTheme::Em( 0.4f );
            const ImRect rect( ImVec2( x - reach, strip.Min.y ), ImVec2( x + reach, strip.Max.y ) );
            bool hovered = false;
            Graph::MarkerButton( "Value", rect, &hovered );
            markerPointer = MarkerPointerX( x );
            if( DoubleClickedLastItem() )
            {
                const double reset = Constrain( InDefault, InSpec );
                if( InOutSpeed != reset )
                {
                    InOutSpeed = reset;
                    changed = true;
                }
                RegrabMarker( toX( reset ) );
                ResetFeedback( canvasId, ImVec2( toX( reset ), strip.GetCenter().y ) );
            }
            else
            {
                const bool markerScrub = ScrubbingLastItem();
                if( markerScrub )
                {
                    scrub = true;
                }
                else
                {
                    markerPointer = -1e9f;
                }
                if( hovered && !ImGui::IsItemActive() )
                {
                    ImGui::SetTooltip( "%s: drag along the strip, double-click for %s", FormatValue( Unit::MetersPerSecond, InOutSpeed ).c_str(), FormatValue( Unit::MetersPerSecond, InDefault ).c_str() );
                }
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::MetersPerSecond, InOutSpeed ) );
        }

        const Graph::CanvasInput input = Graph::End();
        // A press on the strip jumps the value to the pointer, unless it is the second click of
        // a double-click whose first one reset the value (and moved the marker away from under
        // the pointer).
        const bool jump = input.Pressed && g.IO.MouseClickedCount[ImGuiMouseButton_Left] <= 1;
        if( jump || scrub || ( input.Held && g.IO.MouseDelta.x != 0.f ) )
        {
            // A press on the strip (or a rung) jumps to the pointer; the marker drags from where
            // it was grabbed.
            const float pointer = markerPointer > -1e8f && !jump ? markerPointer : g.IO.MousePos.x;
            double next = RoundPace( kPaceAxis * ( pointer - strip.Min.x ) / std::max( strip.GetWidth(), 1.f ), g.IO.KeyShift );
            if( !g.IO.KeyShift )
            {
                const float reach = ImGuiTheme::Em( 0.35f );
                for( int i = 0; i < rungCount; ++i )
                {
                    if( std::abs( toX( rungs[i].MetersPerSecond ) - pointer ) <= reach )
                    {
                        next = rungs[i].MetersPerSecond;
                    }
                }
            }
            next = Constrain( next, InSpec );
            bool ticked = false;
            for( int i = 0; i < rungCount; ++i )
            {
                ticked |= Crossed( InOutSpeed, next, rungs[i].MetersPerSecond );
            }
            if( ticked && !input.Pressed )
            {
                Juice::Tick( canvasId, ImVec2( toX( next ), strip.Max.y ) );
            }
            if( next != InOutSpeed )
            {
                InOutSpeed = next;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Speed against how people move: walk 1.4, jog 3, run 5, sprint 7.5 m/s.\n"
                               "Click a pace or the strip, drag along it (tenths of a m/s, snapping to the paces; Shift: hundredths);\n"
                               "double-click the value's marker for %s.",
                FormatValue( Unit::MetersPerSecond, InDefault ).c_str() );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }

        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( strip.Min, strip.Max, U32( palette.Control ), rounding );
            for( int i = 0; i < rungCount; ++i )
            {
                const float x = toX( rungs[i].MetersPerSecond );
                drawList->AddLine( ImVec2( x, strip.Min.y + stripHeight * 0.3f ), ImVec2( x, strip.Max.y - stripHeight * 0.3f ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.3f ) ), Thin() );
            }
            const float t = std::clamp( Follow( ImGui::GetID( "##fill" ), static_cast<float>( InOutSpeed / kPaceAxis ), canvasId ), 0.f, 1.f );
            const float x = std::round( strip.Min.x + t * strip.GetWidth() );
            if( x > strip.Min.x )
            {
                drawList->AddRectFilled( strip.Min, ImVec2( x, strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) ), rounding, ImDrawFlags_RoundCornersLeft );
            }
            drawList->AddLine( ImVec2( x, strip.Min.y - Thin() ), ImVec2( x, strip.Max.y + Thin() ), U32( palette.Text ), Thin() * 2.f );
            if( InOutSpeed > kPaceAxis )
            {
                Chevrons( drawList, ImVec2( strip.Max.x - ImGuiTheme::Em( 0.2f ), strip.GetCenter().y ), 1.f, U32( palette.Text ) );
            }
            TickOverlay( drawList, strip, canvasId );
            FlashOutline( drawList, strip, canvasId );
        }
        ItemLog::SetItemValue( canvasId, PaceText( InOutSpeed ) );
        ImGui::PopID();
        return changed;
    }
}
