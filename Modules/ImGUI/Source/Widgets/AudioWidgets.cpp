#include "AudioWidgets.h"
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

    namespace AudioMath
    {
        namespace
        {
            constexpr double kInf = std::numeric_limits<double>::infinity();

            // IEC 60268-18 meter law: dB -> percent of the scale (0 dB = 100), extended past
            // 0 dB with the top segment's slope.
            struct Segment
            {
                double FromDb;
                double Slope;   // percent per dB
                double AtFrom;  // percent at FromDb
            };
            const Segment kSegments[] = {
                { -70.0, 0.25, 0.0 },
                { -60.0, 0.5, 2.5 },
                { -50.0, 0.75, 7.5 },
                { -40.0, 1.5, 15.0 },
                { -30.0, 2.0, 30.0 },
                { -20.0, 2.5, 50.0 },
            };
            constexpr int kSegmentCount = static_cast<int>( sizeof( kSegments ) / sizeof( kSegments[0] ) );

            double MeterPercent( double InDb )
            {
                if( !( InDb > kMeterFloorDb ) )
                {
                    return 0.0;
                }
                const double db = std::min( InDb, kMeterTopDb );
                int i = kSegmentCount - 1;
                while( i > 0 && db < kSegments[i].FromDb )
                {
                    --i;
                }
                return kSegments[i].AtFrom + ( db - kSegments[i].FromDb ) * kSegments[i].Slope;
            }

            double ClampMin( double InMin )
            {
                return std::max( InMin, 0.001 );
            }

            double ClampMax( double InMin, double InMax )
            {
                return std::max( InMax, ClampMin( InMin ) + 0.001 );
            }
        }

        double GainToDb( double InGain )
        {
            return InGain > 0.0 ? 20.0 * std::log10( InGain ) : -kInf;
        }

        double DbToGain( double InDb )
        {
            return std::isinf( InDb ) && InDb < 0.0 ? 0.0 : std::pow( 10.0, InDb / 20.0 );
        }

        double SemitonesToRatio( double InSemitones )
        {
            return std::pow( 2.0, InSemitones / 12.0 );
        }

        double RatioToSemitones( double InRatio )
        {
            return InRatio > 0.0 ? 12.0 * std::log2( InRatio ) : -kInf;
        }

        void SplitCents( double InSemitones, int& OutWhole, int& OutCents )
        {
            const long long totalCents = std::llround( InSemitones * 100.0 );
            long long whole = totalCents / 100;
            long long cents = totalCents - whole * 100;
            // Nearest whole semitone: -50..50 cents left over.
            if( cents > 50 )
            {
                ++whole;
                cents -= 100;
            }
            else if( cents < -50 )
            {
                --whole;
                cents += 100;
            }
            OutWhole = static_cast<int>( whole );
            OutCents = static_cast<int>( cents );
        }

        bool IsBlackKey( int InSemitone )
        {
            const int pitchClass = ( ( InSemitone % 12 ) + 12 ) % 12;
            return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
        }

        double MeterFromDb( double InDb )
        {
            return MeterPercent( InDb ) / MeterPercent( kMeterTopDb );
        }

        double MeterToDb( double InT )
        {
            const double percent = std::clamp( InT, 0.0, 1.0 ) * MeterPercent( kMeterTopDb );
            if( !( percent > 0.0 ) )
            {
                return -kInf;
            }
            int i = kSegmentCount - 1;
            while( i > 0 && percent < kSegments[i].AtFrom )
            {
                --i;
            }
            return kSegments[i].FromDb + ( percent - kSegments[i].AtFrom ) / kSegments[i].Slope;
        }

        const char* RolloffName( int InMode )
        {
            switch( InMode )
            {
            case 1: return "Linear";
            case 2: return "Linear Square";
            default: return "Inverse";
            }
        }

        double RolloffGain( int InMode, double InDistance, double InMin, double InMax )
        {
            const double minDistance = ClampMin( InMin );
            const double maxDistance = ClampMax( InMin, InMax );
            if( !( InDistance > minDistance ) )
            {
                return 1.0;
            }
            switch( InMode )
            {
            case 1:
            case 2:
            {
                const double x = std::clamp( ( maxDistance - InDistance ) / ( maxDistance - minDistance ), 0.0, 1.0 );
                return InMode == 2 ? x * x : x;
            }
            default:
                return minDistance / std::min( InDistance, maxDistance );
            }
        }

        double RolloffDistanceFor( int InMode, double InGain, double InMin, double InMax )
        {
            const double minDistance = ClampMin( InMin );
            const double maxDistance = ClampMax( InMin, InMax );
            if( InGain >= 1.0 )
            {
                return minDistance;
            }
            const double gain = std::max( InGain, 0.0 );
            switch( InMode )
            {
            case 1:
                return maxDistance - gain * ( maxDistance - minDistance );
            case 2:
                return maxDistance - std::sqrt( gain ) * ( maxDistance - minDistance );
            default:
            {
                if( !( gain > 0.0 ) )
                {
                    return kInf;
                }
                const double d = minDistance / gain;
                return d <= maxDistance * ( 1.0 + 1e-9 ) ? d : kInf;
            }
            }
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

        bool IsLightTheme()
        {
            return ImGuiTheme::Luminance( ImGuiTheme::ActivePalette().Window ) > 0.5f;
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

        double RoundTo( double InValue, double InStep )
        {
            const double rounded = std::round( InValue / InStep ) * InStep;
            return rounded == 0.0 ? 0.0 : rounded;
        }

        bool Crossed( double InFrom, double InTo, double InDetent )
        {
            return ( InFrom < InDetent && InTo >= InDetent ) || ( InFrom > InDetent && InTo <= InDetent );
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

        // Whether the last item (a marker or key) was pressed and is being dragged sideways: the
        // press scrubs the widget like a press on its canvas. Only past the drag threshold (Shift,
        // the fine modifier, scrubs at once), so a click that wobbles a pixel is still a click.
        bool ScrubbingLastItem()
        {
            const ImGuiContext& g = *GImGui;
            const bool dragging = g.IO.KeyShift ? ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.f ) : ImGui::IsMouseDragging( ImGuiMouseButton_Left );
            return ImGui::IsItemActive() && dragging && g.IO.MouseDelta.x != 0.f;
        }

        // A value marker dragged relative to where it was pressed: on activation, remember how far
        // the value sits from the pointer, so the drag moves the value by the pointer's travel
        // instead of jumping it under the pointer. Returns the pointer X the value follows.
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

        // Re-anchor a marker's grab after its value jumped (a double-click reset), so the rest of
        // that press drags from the new value.
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

        // Diagonal hatching over a rect: a range the field doesn't allow.
        void Hatch( ImDrawList* InDrawList, const ImRect& InRect, ImU32 InColor )
        {
            if( InRect.GetWidth() <= 0.f )
            {
                return;
            }
            InDrawList->PushClipRect( InRect.Min, InRect.Max, true );
            const float step = ImGuiTheme::Em( 0.35f );
            const float h = InRect.GetHeight();
            for( float x = InRect.Min.x - h; x < InRect.Max.x; x += step )
            {
                InDrawList->AddLine( ImVec2( x, InRect.Max.y ), ImVec2( x + h, InRect.Min.y ), InColor, Thin() );
            }
            InDrawList->PopClipRect();
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

        // A label at InX (centred, kept in InMin..InMax) unless it would overlap one already placed.
        struct LabelRow
        {
            std::vector<ImRect> Placed;

            bool Place( float InX, float InTop, const char* InText, float InMinX, float InMaxX, ImRect& OutRect )
            {
                const ImVec2 size = ImGui::CalcTextSize( InText );
                const float left = std::round( std::clamp( InX - size.x * 0.5f, InMinX, std::max( InMinX, InMaxX - size.x ) ) );
                const ImRect rect( ImVec2( left, InTop ), ImVec2( left + size.x, InTop + size.y ) );
                const float gap = ImGuiTheme::Em( 0.3f );
                for( const ImRect& other : Placed )
                {
                    if( rect.Min.x < other.Max.x + gap && rect.Max.x + gap > other.Min.x )
                    {
                        return false;
                    }
                }
                Placed.push_back( rect );
                OutRect = rect;
                return true;
            }
        };

        // A chip at InRect: a clickable label lit when InLit (the Auto chip's look).
        void DrawChip( ImDrawList* InDrawList, const ImRect& InRect, const char* InText, bool InLit, bool InHovered, ImGuiID InId )
        {
            if( !ImGui::IsRectVisible( InRect.Min, InRect.Max ) )
            {
                return;
            }
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            const float rounding = ImGui::GetStyle().FrameRounding;
            InDrawList->AddRectFilled( InRect.Min, InRect.Max, U32( InLit ? ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) : InHovered ? palette.ControlHovered : palette.Control ), rounding );
            InDrawList->AddRect( InRect.Min, InRect.Max, U32( InLit ? palette.AccentText : palette.Separator ), rounding, 0, Thin() );
            const ImVec2 size = ImGui::CalcTextSize( InText );
            InDrawList->AddText( ImVec2( std::round( InRect.GetCenter().x - size.x * 0.5f ), std::round( InRect.GetCenter().y - size.y * 0.5f ) ), U32( InLit || InHovered ? palette.Text : palette.Muted ), InText );
            FlashOutline( InDrawList, InRect, InId );
        }
    }

    // ---- Level meter --------------------------------------------------------------------------

    namespace
    {
        struct MeterTick
        {
            double Db;
            const char* Text;
            int Priority;   // labels placed in this order; one that overlaps an earlier one is left out
        };
        const MeterTick kMeterTicks[] = {
            { 0.0, "0", 0 },
            { -6.0, "-6", 1 },
            { -12.0, "-12", 2 },
            { -24.0, "-24", 3 },
            { -48.0, "-48", 6 },
            { 6.0, "+6", 4 },
            // Silence, at the left end.
            { -std::numeric_limits<double>::infinity(), "-inf", 5 },
        };
        // Pointing this far left of -60 dB means silence.
        constexpr double kSilentBelowDb = -60.0;

        std::string LevelText( double InGain )
        {
            return FormatValue( Unit::LinearGain, InGain );
        }
    }

    bool LevelMeter( const char* InCaption, double& InOutGain, const NumberSpec& InSpec, double InLiveGain )
    {
        using namespace AudioMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiContext& g = *GImGui;
        // The loudest the field allows: its range's top (gain 1 = 0 dB for AudioSource.Volume).
        const double maxDb = std::min( GainToDb( InSpec.HasRange ? InSpec.Max : DbToGain( kMeterTopDb ) ), kMeterTopDb );
        const float stripHeight = std::round( ImGui::GetFrameHeight() * 0.6f );
        const float labelTop = stripHeight + ImGuiTheme::Em( 0.15f );
        const float height = labelTop + ImGui::GetFontSize() + ImGuiTheme::Em( 0.1f );

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), Graph::Axis(), Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect strip( map.Min, ImVec2( map.Max.x, map.Min.y + stripHeight ) );
        auto toX = [&]( double InDb ) { return std::round( strip.Min.x + static_cast<float>( MeterFromDb( InDb ) ) * strip.GetWidth() ); };
        bool changed = false;

        // The value's marker: pressing it doesn't move the value, so a double-click on it resets
        // to 0 dB in one step (a press on the strip itself jumps to the pointer). Dragging it
        // moves the value by the pointer's travel, from where it was grabbed.
        bool markerScrub = false;
        float markerPointer = 0.f;
        {
            const float x = toX( GainToDb( InOutGain ) );
            const float reach = ImGuiTheme::Em( 0.4f );
            const ImRect rect( ImVec2( x - reach, strip.Min.y ), ImVec2( x + reach, strip.Max.y ) );
            bool hovered = false;
            Graph::MarkerButton( "Value", rect, &hovered );
            markerPointer = MarkerPointerX( x );
            if( DoubleClickedLastItem() )
            {
                const double reset = Constrain( 1.0, InSpec );
                if( InOutGain != reset )
                {
                    InOutGain = reset;
                    changed = true;
                }
                RegrabMarker( toX( GainToDb( reset ) ) );
                ResetFeedback( canvasId, ImVec2( toX( GainToDb( reset ) ), strip.GetCenter().y ) );
            }
            else
            {
                // Never on the reset's own frame: the scrub would overwrite the reset.
                markerScrub = ScrubbingLastItem();
                if( hovered && !ImGui::IsItemActive() )
                {
                    ImGui::SetTooltip( "%s: drag along the meter, double-click for 0 dB", LevelText( InOutGain ).c_str() );
                }
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), LevelText( InOutGain ) );
        }

        const Graph::CanvasInput input = Graph::End();
        if( input.Pressed || markerScrub || ( input.Held && g.IO.MouseDelta.x != 0.f ) )
        {
            // A press on the strip jumps to the pointer; the marker drags from where it was grabbed.
            const float pointer = markerScrub && !input.Pressed ? markerPointer : g.IO.MousePos.x;
            double db = MeterToDb( ( pointer - strip.Min.x ) / std::max( strip.GetWidth(), 1.f ) );
            if( !( db >= kSilentBelowDb ) )
            {
                db = -std::numeric_limits<double>::infinity();
            }
            else
            {
                // Half decibels, Shift for tenths (free of the ticks). Without Shift the labelled
                // ticks catch the pointer, but never further than halfway to the next half-decibel
                // step, so every step next to a tick stays reachable.
                const double step = g.IO.KeyShift ? 0.1 : 0.5;
                db = RoundTo( db, step );
                if( !g.IO.KeyShift )
                {
                    for( const MeterTick& tick : kMeterTicks )
                    {
                        // Silence has no neighbouring step: its tick keeps the full reach.
                        const float tx = toX( tick.Db );
                        const float gap = std::isfinite( tick.Db ) ? std::min( std::abs( toX( tick.Db + step ) - tx ), std::abs( tx - toX( tick.Db - step ) ) ) : ImGuiTheme::Em( 1.f );
                        const float reach = std::min( ImGuiTheme::Em( 0.3f ), gap * 0.45f );
                        if( std::abs( tx - pointer ) <= reach )
                        {
                            db = tick.Db;
                        }
                    }
                }
            }
            const double next = Constrain( DbToGain( db ), InSpec );
            // Ticks as the level passes a labelled tick (compared along the strip, where silence
            // is the left end).
            const double from = MeterFromDb( GainToDb( InOutGain ) );
            const double to = MeterFromDb( GainToDb( next ) );
            bool ticked = Crossed( from, to, 0.0 );
            for( const MeterTick& tick : kMeterTicks )
            {
                ticked |= Crossed( from, to, MeterFromDb( tick.Db ) );
            }
            if( ticked && !input.Pressed )
            {
                Juice::Tick( canvasId, ImVec2( toX( GainToDb( next ) ), strip.Max.y ) );
            }
            if( next != InOutGain )
            {
                InOutGain = next;
                changed = true;
            }
        }
        const bool live = InLiveGain >= 0.0;
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Volume on a level meter (IEC 60268-18 scale, -inf to +6 dB); 0 dB plays the clip as recorded, -6 dB at half amplitude.\n"
                               "Drag along it in half decibels, snapping to the ticks (Shift: tenths, no snapping); the far left is silence.\n"
                               "Double-click the value's marker for 0 dB.%s%s",
                maxDb < kMeterTopDb ? "\nThe hatched end is headroom the field doesn't allow (Volume stops at gain 1, 0 dB)." : "",
                live ? "\nThe bar along the bottom is the live level while it plays: after the bus, distance and occlusion (FMOD audibility)." : "" );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }

        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( strip.Min, strip.Max, U32( palette.Control ), rounding );
            // The set level, filled from silence.
            const float t = std::clamp( Follow( ImGui::GetID( "##fill" ), static_cast<float>( MeterFromDb( GainToDb( InOutGain ) ) ), canvasId ), 0.f, 1.f );
            const float x = std::round( strip.Min.x + t * strip.GetWidth() );
            if( x > strip.Min.x )
            {
                drawList->AddRectFilled( strip.Min, ImVec2( x, strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) ), rounding, ImDrawFlags_RoundCornersLeft );
            }
            // Headroom past the field's maximum.
            const float limitX = toX( maxDb );
            if( limitX < strip.Max.x - Thin() )
            {
                const ImRect headroom( ImVec2( limitX, strip.Min.y ), strip.Max );
                drawList->AddRectFilled( headroom.Min, headroom.Max, U32( palette.Recessed ), rounding, ImDrawFlags_RoundCornersRight );
                Hatch( drawList, headroom, U32( ImGuiTheme::WithAlpha( palette.Muted, 0.45f ) ) );
            }
            // The live level, a bar along the bottom third: green, amber over -6 dB.
            if( live )
            {
                const double liveDb = GainToDb( InLiveGain );
                const float liveT = std::clamp( Juice::Spring( ImGui::GetID( "##live" ), static_cast<float>( MeterFromDb( liveDb ) ), 14.f ), 0.f, 1.f );
                const float liveX = std::round( strip.Min.x + liveT * strip.GetWidth() );
                const float top = std::round( strip.Max.y - stripHeight * 0.35f );
                if( liveX > strip.Min.x )
                {
                    const ImVec4 ink = liveDb > -6.0 ? palette.Warning : palette.Success;
                    drawList->AddRectFilled( ImVec2( strip.Min.x, top ), ImVec2( liveX, strip.Max.y ), U32( ImGuiTheme::WithAlpha( ink, 0.9f ) ), rounding, ImDrawFlags_RoundCornersBottomLeft );
                }
            }
            // Ticks: 0 dB full height, the others short.
            for( const MeterTick& tick : kMeterTicks )
            {
                const float tx = toX( tick.Db );
                if( !std::isfinite( tick.Db ) || tx >= strip.Max.x - Thin() )
                {
                    continue;
                }
                const float inset = tick.Db == 0.0 ? 0.f : stripHeight * 0.5f;
                drawList->AddLine( ImVec2( tx, strip.Min.y + inset ), ImVec2( tx, strip.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Text, tick.Db == 0.0 ? 0.6f : 0.3f ) ), Thin() );
            }
            // The set level's marker.
            drawList->AddLine( ImVec2( x, strip.Min.y - Thin() ), ImVec2( x, strip.Max.y + Thin() ), U32( palette.Text ), Thin() * 2.f );
            TickOverlay( drawList, strip, canvasId );
            FlashOutline( drawList, strip, canvasId );

            // Scale labels under the strip, by priority (one that would overlap is left out).
            LabelRow labels;
            const float top = map.Min.y + labelTop;
            std::vector<const MeterTick*> order;
            for( const MeterTick& tick : kMeterTicks )
            {
                order.push_back( &tick );
            }
            std::sort( order.begin(), order.end(), []( const MeterTick* InA, const MeterTick* InB ) { return InA->Priority < InB->Priority; } );
            for( const MeterTick* tick : order )
            {
                ImRect rect;
                if( labels.Place( toX( tick->Db ), top, tick->Text, map.Min.x, map.Max.x, rect ) )
                {
                    const bool outOfRange = tick->Db > maxDb + 1e-9;
                    drawList->AddText( rect.Min, U32( outOfRange ? ImGuiTheme::WithAlpha( palette.Muted, 0.6f ) : palette.Muted ), tick->Text );
                }
            }
        }
        ItemLog::SetItemValue( canvasId, LevelText( InOutGain ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Pitch keyboard -----------------------------------------------------------------------

    namespace
    {
        constexpr int kLowKey = -12;
        constexpr int kHighKey = 12;
        constexpr int kWhiteKeys = 15;  // C to C over two octaves

        // The key's white index (0..14) for a white key, or the white key it follows for a black one.
        int WhiteIndex( int InSemitone )
        {
            static const int kIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
            const int octave = static_cast<int>( std::floor( InSemitone / 12.0 ) );
            const int pitchClass = InSemitone - octave * 12;
            return ( octave + 1 ) * 7 + kIndex[pitchClass];
        }

        struct Keyboard
        {
            float Left = 0.f;
            float WhiteWidth = 1.f;
            float Top = 0.f;
            float Bottom = 0.f;
            float BlackBottom = 0.f;

            // Where a key's centre sits along the keyboard: a white key's middle, or a black
            // key's (on the line between the two whites).
            float CentreX( int InSemitone ) const
            {
                const int white = WhiteIndex( InSemitone );
                return AudioMath::IsBlackKey( InSemitone ) ? Left + ( white + 1 ) * WhiteWidth : Left + ( white + 0.5f ) * WhiteWidth;
            }

            ImRect KeyRect( int InSemitone ) const
            {
                if( AudioMath::IsBlackKey( InSemitone ) )
                {
                    const float x = CentreX( InSemitone );
                    const float half = std::round( WhiteWidth * 0.3f );
                    return ImRect( ImVec2( x - half, Top ), ImVec2( x + half, BlackBottom ) );
                }
                const float left = std::round( Left + WhiteIndex( InSemitone ) * WhiteWidth );
                return ImRect( ImVec2( left, Top ), ImVec2( std::round( left + WhiteWidth ), Bottom ) );
            }

            // Semitones (continuous) at a screen X: linear between neighbouring key centres.
            double SemitoneAt( float InX ) const
            {
                if( InX <= CentreX( kLowKey ) )
                {
                    return kLowKey;
                }
                for( int n = kLowKey; n < kHighKey; ++n )
                {
                    const float a = CentreX( n );
                    const float b = CentreX( n + 1 );
                    if( InX <= b )
                    {
                        return n + ( InX - a ) / std::max( b - a, 1e-3f );
                    }
                }
                return kHighKey;
            }

            // The key under a point (black keys first, in the band where they exist), with the
            // point held inside the keys: a drag above or below them still picks the key it's over.
            int KeyAt( ImVec2 InPos ) const
            {
                const float y = std::clamp( InPos.y, Top, Bottom - 1.f );
                if( y < BlackBottom )
                {
                    for( int n = kLowKey; n <= kHighKey; ++n )
                    {
                        if( AudioMath::IsBlackKey( n ) )
                        {
                            const ImRect rect = KeyRect( n );
                            if( InPos.x >= rect.Min.x && InPos.x < rect.Max.x )
                            {
                                return n;
                            }
                        }
                    }
                }
                if( InPos.x < KeyRect( kLowKey ).Max.x )
                {
                    return kLowKey;
                }
                for( int n = kLowKey; n <= kHighKey; ++n )
                {
                    if( !AudioMath::IsBlackKey( n ) && InPos.x < KeyRect( n ).Max.x )
                    {
                        return n;
                    }
                }
                return kHighKey;
            }

            float XAt( double InSemitones ) const
            {
                const double s = std::clamp( InSemitones, static_cast<double>( kLowKey ), static_cast<double>( kHighKey ) );
                const int n = std::min( static_cast<int>( std::floor( s ) ), kHighKey - 1 );
                const float a = CentreX( n );
                const float b = CentreX( n + 1 );
                return a + static_cast<float>( s - n ) * ( b - a );
            }
        };

        std::string KeyName( int InSemitone )
        {
            return InSemitone > 0 ? "+" + std::to_string( InSemitone ) : std::to_string( InSemitone );
        }

        std::string PitchText( double InRatio )
        {
            using namespace AudioMath;
            const double semitones = RatioToSemitones( InRatio );
            if( !std::isfinite( semitones ) )
            {
                return "silent";
            }
            int whole = 0;
            int cents = 0;
            SplitCents( semitones, whole, cents );
            const std::string signedCents = cents > 0 ? "+" + std::to_string( cents ) : cents == 0 ? std::string( "+0" ) : std::to_string( cents );
            const std::string signedWhole = whole > 0 ? "+" + std::to_string( whole ) : std::to_string( whole );
            return signedWhole + " st, " + signedCents + " cents, ×" + FormatNumber( InRatio, 3 );
        }
    }

    bool PitchKeyboard( const char* InCaption, double& InOutRatio, const NumberSpec& InSpec )
    {
        using namespace AudioMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiContext& g = *GImGui;
        const bool light = IsLightTheme();
        const float railHeight = ImGuiTheme::Em( 0.6f );
        const float keyHeight = ImGuiTheme::Em( 2.2f );
        const float labelTop = railHeight + keyHeight + ImGuiTheme::Em( 0.15f );
        const float height = labelTop + ImGui::GetFontSize() + ImGuiTheme::Em( 0.1f );

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( width, height ), Graph::Axis(), Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        Keyboard keys;
        keys.Left = map.Min.x;
        keys.WhiteWidth = ( map.Max.x - map.Min.x ) / kWhiteKeys;
        keys.Top = std::round( map.Min.y + railHeight );
        keys.Bottom = std::round( keys.Top + keyHeight );
        keys.BlackBottom = std::round( keys.Top + keyHeight * 0.62f );
        const ImRect rail( map.Min, ImVec2( map.Max.x, keys.Top - Thin() ) );
        const double semitones = RatioToSemitones( InOutRatio );
        bool changed = false;
        bool keyScrub = false;      // dragging from a key: whole semitones by the key under the pointer
        bool markerScrub = false;   // dragging the rail marker: from where it was grabbed
        float markerPointer = 0.f;

        // Keys: the black ones first, so they take the pointer where they overlap the whites.
        std::vector<int> order;
        for( int n = kLowKey; n <= kHighKey; ++n )
        {
            if( IsBlackKey( n ) )
            {
                order.push_back( n );
            }
        }
        for( int n = kLowKey; n <= kHighKey; ++n )
        {
            if( !IsBlackKey( n ) )
            {
                order.push_back( n );
            }
        }
        int hoveredKey = kHighKey + 1;
        int heldKey = kHighKey + 1;
        // A press that moved the value by scrubbing (past the drag threshold, or any move with
        // Shift for cents) isn't a click when it lets go.
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID scrubbedKey = ImGui::GetID( "##scrubbed" );
        for( const int n : order )
        {
            const std::string name = KeyName( n );
            bool hovered = false;
            const bool pressed = Graph::MarkerButton( name.c_str(), keys.KeyRect( n ), &hovered );
            if( ImGui::IsItemActivated() )
            {
                storage->SetBool( scrubbedKey, false );
            }
            if( ScrubbingLastItem() )
            {
                keyScrub = true;
                storage->SetBool( scrubbedKey, true );
            }
            const bool clicked = pressed && NotDragged() && !storage->GetBool( scrubbedKey, false );
            if( ImGui::IsItemActive() )
            {
                heldKey = n;
            }
            if( clicked )
            {
                const double next = Constrain( SemitonesToRatio( n ), InSpec );
                if( next != InOutRatio )
                {
                    InOutRatio = next;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( keys.CentreX( n ), keys.Top ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::PitchRatio, SemitonesToRatio( n ) ) );
            if( hovered )
            {
                hoveredKey = n;
                ImGui::SetTooltip( "%s st: x%s the clip's rate%s", name.c_str(), FormatNumber( SemitonesToRatio( n ), 3 ).c_str(),
                    n == 0 ? " (as recorded)" : n == 12 ? " (an octave up, twice as fast)" : n == -12 ? " (an octave down, half as fast)" : "" );
            }
        }

        // The exact pitch's marker on the rail: pressing it doesn't move the value, so a
        // double-click on it resets to the clip's own pitch in one step. Dragging it moves the
        // pitch by the pointer's travel, from where it was grabbed.
        const float markerX = std::round( keys.XAt( std::isfinite( semitones ) ? semitones : kLowKey ) );
        {
            const float reach = ImGuiTheme::Em( 0.4f );
            const ImRect rect( ImVec2( markerX - reach, rail.Min.y ), ImVec2( markerX + reach, rail.Max.y ) );
            bool hovered = false;
            Graph::MarkerButton( "Value", rect, &hovered );
            markerPointer = MarkerPointerX( markerX );
            if( DoubleClickedLastItem() )
            {
                const double reset = Constrain( 1.0, InSpec );
                if( InOutRatio != reset )
                {
                    InOutRatio = reset;
                    changed = true;
                }
                RegrabMarker( std::round( keys.XAt( RatioToSemitones( reset ) ) ) );
                ResetFeedback( canvasId, ImVec2( keys.XAt( 0.0 ), rail.GetCenter().y ) );
            }
            else
            {
                // Never on the reset's own frame: the scrub would overwrite the reset.
                markerScrub = ScrubbingLastItem();
                if( hovered && !ImGui::IsItemActive() )
                {
                    ImGui::SetTooltip( "%s: drag across the keys, double-click for 0 st", PitchText( InOutRatio ).c_str() );
                }
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), FormatValue( Unit::PitchRatio, InOutRatio ) );
        }

        const Graph::CanvasInput input = Graph::End();
        if( input.Pressed || keyScrub || markerScrub || ( input.Held && g.IO.MouseDelta.x != 0.f ) )
        {
            double next = 0.0;
            if( keyScrub && !input.Pressed && !g.IO.KeyShift )
            {
                // Dragging across the keys: the key under the pointer.
                next = keys.KeyAt( g.IO.MousePos );
            }
            else
            {
                // The rail (and Shift): whole semitones (Shift: cents) between the key centres;
                // the marker from where it was grabbed.
                next = keys.SemitoneAt( markerScrub && !input.Pressed ? markerPointer : g.IO.MousePos.x );
                next = g.IO.KeyShift ? RoundTo( next, 0.01 ) : std::round( next );
            }
            const double from = std::isfinite( semitones ) ? semitones : static_cast<double>( kLowKey );
            const double nextRatio = Constrain( SemitonesToRatio( next ), InSpec );
            const double to = RatioToSemitones( nextRatio );
            // A tick for each semitone passed.
            if( !input.Pressed && std::floor( from + 1e-9 ) != std::floor( to + 1e-9 ) )
            {
                Juice::Tick( canvasId, ImVec2( keys.XAt( to ), keys.Top ) );
            }
            if( nextRatio != InOutRatio )
            {
                InOutRatio = nextRatio;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Pitch on a keyboard, one octave each way: each key is a semitone, ratio 2^(n / 12) of the clip's rate\n"
                               "(the dot is the clip as recorded; +12 plays twice as fast). Click a key, or drag across them in whole\n"
                               "semitones (Shift for cents). The marker above the keys is the exact pitch: double-click it for 0 st." );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }

        if( visible )
        {
            const double shown = RatioToSemitones( InOutRatio );
            int shownWhole = 0;
            int shownCents = 0;
            SplitCents( std::isfinite( shown ) ? shown : -100.0, shownWhole, shownCents );
            const bool shownOn = std::isfinite( shown ) && shown >= kLowKey - 0.5 && shown <= kHighKey + 0.5;
            const ImVec4 whiteInk = light ? palette.Control : ImGuiTheme::Mix( palette.Control, palette.Text, 0.62f );
            const ImVec4 blackInk = light ? ImGuiTheme::Mix( palette.Text, palette.Control, 0.12f ) : palette.Canvas;
            const ImVec4 edge = ImGuiTheme::WithAlpha( light ? palette.Separator : palette.Canvas, 1.f );
            const ImVec4 lit = ImGuiTheme::Mix( palette.Accent, light ? palette.Control : palette.Text, light ? 0.1f : 0.15f );
            auto drawKey = [&]( int n ) {
                const ImRect rect = keys.KeyRect( n );
                const bool black = IsBlackKey( n );
                const bool current = shownOn && n == shownWhole;
                ImVec4 fill = black ? blackInk : whiteInk;
                if( current )
                {
                    fill = black ? ImGuiTheme::Mix( palette.Accent, blackInk, 0.25f ) : lit;
                }
                else if( n == heldKey || n == hoveredKey )
                {
                    fill = ImGuiTheme::Mix( fill, palette.Accent, 0.3f );
                }
                const float rounding = std::round( Thin() * 2.f );
                drawList->AddRectFilled( rect.Min, rect.Max, U32( fill ), rounding, ImDrawFlags_RoundCornersBottom );
                drawList->AddRect( rect.Min, rect.Max, U32( edge ), rounding, ImDrawFlags_RoundCornersBottom, Thin() );
                // The clip's own pitch: a dot near the key's bottom.
                if( n == 0 )
                {
                    drawList->AddCircleFilled( ImVec2( rect.GetCenter().x, rect.Max.y - ImGuiTheme::Em( 0.4f ) ), Thin() * 2.5f, U32( current ? palette.Text : palette.Muted ) );
                }
            };
            for( int n = kLowKey; n <= kHighKey; ++n )
            {
                if( !IsBlackKey( n ) )
                {
                    drawKey( n );
                }
            }
            for( int n = kLowKey; n <= kHighKey; ++n )
            {
                if( IsBlackKey( n ) )
                {
                    drawKey( n );
                }
            }

            // The rail: a tick per semitone (taller at the octaves and the clip's pitch), and the
            // exact pitch's marker with a line down through the keys.
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( rail.Min, rail.Max, U32( palette.Recessed ), rounding );
            for( int n = kLowKey; n <= kHighKey; ++n )
            {
                const float x = std::round( keys.CentreX( n ) );
                const bool major = n % 12 == 0;
                drawList->AddLine( ImVec2( x, major ? rail.Min.y + Thin() : rail.GetCenter().y ), ImVec2( x, rail.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Text, major ? 0.5f : 0.22f ) ), Thin() );
            }
            const float x = std::round( Follow( ImGui::GetID( "##marker" ), keys.XAt( std::isfinite( shown ) ? shown : kLowKey ), canvasId ) );
            const ImU32 markerInk = U32( palette.AccentText );
            if( shownOn )
            {
                const float half = ImGuiTheme::Em( 0.3f );
                drawList->AddTriangleFilled( ImVec2( x - half, rail.Min.y ), ImVec2( x + half, rail.Min.y ), ImVec2( x, rail.Max.y ), markerInk );
                drawList->AddLine( ImVec2( x, rail.Max.y ), ImVec2( x, keys.Bottom ), U32( ImGuiTheme::WithAlpha( palette.AccentText, 0.9f ) ), Thin() * 2.f );
            }
            else
            {
                // Past an octave: chevrons at the end it went off.
                const bool high = std::isfinite( shown ) && shown > 0.0;
                const float tip = high ? rail.Max.x - Thin() : rail.Min.x + Thin();
                Chevrons( drawList, ImVec2( tip, rail.GetCenter().y ), high ? 1.f : -1.f, markerInk );
            }
            TickOverlay( drawList, rail, canvasId );
            FlashOutline( drawList, ImRect( rail.Min, ImVec2( map.Max.x, keys.Bottom ) ), canvasId );

            // Under the keys: the octave ends and the reading.
            const float top = map.Min.y + labelTop;
            const std::string reading = PitchText( InOutRatio );
            const ImVec2 readingSize = ImGui::CalcTextSize( reading.c_str() );
            const ImVec2 lowSize = ImGui::CalcTextSize( "-1 oct" );
            const ImVec2 highSize = ImGui::CalcTextSize( "+1 oct" );
            const float readingLeft = std::round( map.Min.x + ( map.Max.x - map.Min.x - readingSize.x ) * 0.5f );
            const bool ends = readingLeft > map.Min.x + lowSize.x + ImGuiTheme::Em( 0.4f );
            if( ends )
            {
                drawList->AddText( ImVec2( map.Min.x, top ), U32( palette.Muted ), "-1 oct" );
                drawList->AddText( ImVec2( map.Max.x - highSize.x, top ), U32( palette.Muted ), "+1 oct" );
            }
            drawList->AddText( ImVec2( std::max( readingLeft, map.Min.x ), top ), U32( palette.Text ), reading.c_str() );
        }
        ItemLog::SetItemValue( canvasId, PitchText( InOutRatio ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Rolloff graph ------------------------------------------------------------------------

    namespace
    {
        constexpr double kGraphTopDb = 18.0;   // room above 0 dB for the reading
        constexpr double kGraphBottomDb = -48.0;
        constexpr double kReportDb = -6.0;

        // dB for plotting: silence lands below the plot (clamped onto its bottom edge).
        double PlotDb( double InGain )
        {
            return InGain > 0.0 ? std::max( AudioMath::GainToDb( InGain ), kGraphBottomDb - 12.0 ) : kGraphBottomDb - 12.0;
        }

        std::string DistanceText( double InMetres )
        {
            return FormatValue( Unit::Meters, Significant( InMetres, 2 ) );
        }

        // "Inverse: -6 dB at 2 m", or the level it holds at past the max distance when it never
        // gets that quiet.
        std::string RolloffReading( int InMode, double InMin, double InMax )
        {
            using namespace AudioMath;
            const double distance = RolloffDistanceFor( InMode, DbToGain( kReportDb ), InMin, InMax );
            const std::string name = RolloffName( InMode );
            if( std::isfinite( distance ) )
            {
                return name + ": " + FormatNumber( kReportDb ) + " dB at " + DistanceText( distance );
            }
            const double floorDb = GainToDb( RolloffGain( InMode, InMax, InMin, InMax ) );
            return name + ": " + FormatNumber( floorDb, 1 ) + " dB at " + DistanceText( InMax ) + " and beyond";
        }
    }

    bool RolloffGraph( const char* InCaption, int& InOutRolloff, double& InOutMin, double& InOutMax, const NumberSpec& InMinSpec, const NumberSpec& InMaxSpec, double InSpatialBlend )
    {
        using namespace AudioMath;
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const int active = std::clamp( InOutRolloff, 0, kRolloffCount - 1 );

        // The distance axis spans 1.25x the max distance (log); it holds still under a dragged
        // handle, then eases over (as the light falloff's range axis does).
        static ImGuiID heldFor = 0;
        static double heldSpan = 0.0;
        static std::unordered_map<ImGuiID, double> lastSpans;
        const ImGuiID minId = Graph::HandleId( InCaption, "Min Distance" );
        const ImGuiID maxId = Graph::HandleId( InCaption, "Max Distance" );
        const ImGuiID spanId = ImGui::GetID( "##span" );
        const double target = std::max( { InOutMax * 1.25, InOutMin * 1.5, 1.0 } );
        double span = target;
        const ImGuiID activeId = GImGui->ActiveId;
        if( activeId == minId || activeId == maxId )
        {
            if( heldFor != spanId )
            {
                heldFor = spanId;
                const auto drawn = lastSpans.find( spanId );
                heldSpan = drawn != lastSpans.end() ? drawn->second : target;
            }
            span = heldSpan;
            Juice::Snap( spanId, static_cast<float>( span ) );
        }
        else
        {
            if( heldFor == spanId )
            {
                heldFor = 0;
            }
            span = Juice::Spring( spanId, static_cast<float>( target ), 5.f );
        }
        span = std::max( span, 1.0 );
        lastSpans[spanId] = span;

        // The rolloff names under the plot, to switch between them.
        const float rowStep = ImGui::GetFontSize() + ImGuiTheme::Em( 0.1f );
        Graph::Frame frame;
        frame.Below = rowStep + ImGuiTheme::Em( 0.15f );
        const Graph::Axis xAxis { 0.0, span, true, Unit::Meters };
        const Graph::Axis yAxis { kGraphBottomDb, kGraphTopDb, false, Unit::Decibels };
        const bool visible = Graph::Begin( InCaption, ImVec2( width, ImGuiTheme::Em( 6.5f ) + frame.Below ), xAxis, yAxis, frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool is2D = !( InSpatialBlend > 0.0 );
        bool changed = false;

        if( visible )
        {
            Graph::Grid( true, false );
            // -6 dB, where the reading is taken.
            const float reportY = std::round( map.YToScreen( kReportDb ) );
            DashedHorizontal( drawList, map.Min.x, map.Max.x, reportY, U32( ImGuiTheme::WithAlpha( palette.Text, 0.3f ) ) );
            const float zeroY = std::round( map.YToScreen( 0.0 ) );
            drawList->AddLine( ImVec2( map.Min.x, zeroY ), ImVec2( map.Max.x, zeroY ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.2f ) ), Thin() );
            for( int mode = 0; mode < kRolloffCount; ++mode )
            {
                if( mode != active )
                {
                    Graph::Plot( [&]( double d ) { return PlotDb( RolloffGain( mode, d, InOutMin, InOutMax ) ); }, U32( ImGuiTheme::WithAlpha( palette.Muted, 0.55f ) ), false, 96, Thin() );
                }
            }
            const ImVec4 curve = ImGuiTheme::WithAlpha( palette.AccentText, is2D ? 0.45f : 0.95f );
            Graph::Plot( [&]( double d ) { return PlotDb( RolloffGain( active, d, InOutMin, InOutMax ) ); }, U32( curve ), true, 160, Thin() * 2.f );
            // Where it reaches -6 dB.
            const double at = RolloffDistanceFor( active, DbToGain( kReportDb ), InOutMin, InOutMax );
            if( std::isfinite( at ) && at <= span )
            {
                const float ax = std::round( map.XToScreen( at ) );
                DashedVertical( drawList, ax, reportY, map.Max.y, U32( ImGuiTheme::WithAlpha( palette.Text, 0.3f ) ) );
                drawList->AddCircleFilled( ImVec2( ax, reportY ), Thin() * 3.f, U32( palette.Text ) );
            }
            // The reading, top right; a 2D voice doesn't fade at all.
            const std::string reading = is2D ? std::string( "2D: no distance fade" ) : RolloffReading( active, InOutMin, InOutMax );
            const ImVec2 size = ImGui::CalcTextSize( reading.c_str() );
            const ImVec2 textAt( std::max( map.Max.x - size.x - ImGuiTheme::Em( 0.25f ), map.Min.x + ImGuiTheme::Em( 0.2f ) ), map.Min.y + Thin() * 2.f );
            const float pad = ImGuiTheme::Em( 0.15f );
            drawList->AddRectFilled( ImVec2( textAt.x - pad, textAt.y ), ImVec2( textAt.x + size.x + pad, textAt.y + size.y ), U32( ImGuiTheme::WithAlpha( palette.Recessed, 0.85f ) ), ImGui::GetStyle().FrameRounding );
            drawList->AddText( textAt, U32( is2D ? palette.Muted : palette.Text ), reading.c_str() );
        }

        // Handles: min on the 0 dB line, max on the curve where it stops (held level or silence).
        double ticks[32];
        const int tickCount = Graph::Ticks( xAxis, ticks, 32 );
        {
            Graph::HandleSpec spec;
            spec.MinX = InMinSpec.HasRange ? InMinSpec.Min : 0.0;
            spec.MaxX = std::min( InOutMax, InMinSpec.HasRange ? InMinSpec.Max : InOutMax );
            spec.MinY = 0.0;
            spec.MaxY = 0.0;
            spec.Detents = ticks;
            spec.DetentCount = tickCount;
            spec.FreezeAxis = true;
            double x = InOutMin;
            double y = 0.0;
            if( Graph::DragHandle( "Min Distance", &x, &y, spec ) )
            {
                const double next = Constrain( std::clamp( Significant( x, 2 ), spec.MinX, spec.MaxX ), InMinSpec );
                if( next != InOutMin )
                {
                    InOutMin = next;
                    changed = true;
                }
            }
            ItemLog::SetItemValue( minId, DistanceText( InOutMin ) );
        }
        {
            Graph::HandleSpec spec;
            spec.MinX = std::max( InOutMin, InMaxSpec.HasRange ? InMaxSpec.Min : 0.0 );
            spec.MaxX = InMaxSpec.HasRange ? InMaxSpec.Max : 1e6;
            const double atMax = std::clamp( PlotDb( RolloffGain( active, InOutMax, InOutMin, InOutMax ) ), kGraphBottomDb, kGraphTopDb );
            spec.MinY = atMax;
            spec.MaxY = atMax;
            spec.Detents = ticks;
            spec.DetentCount = tickCount;
            spec.FreezeAxis = true;
            double x = InOutMax;
            double y = atMax;
            if( Graph::DragHandle( "Max Distance", &x, &y, spec ) )
            {
                const double next = Constrain( std::clamp( Significant( x, 2 ), spec.MinX, spec.MaxX ), InMaxSpec );
                if( next != InOutMax )
                {
                    InOutMax = next;
                    changed = true;
                }
            }
            ItemLog::SetItemValue( maxId, DistanceText( InOutMax ) );
        }

        // The rolloff names, with line swatches when they fit.
        const float legendTop = map.Max.y + ( ImGui::GetFontSize() + ImGuiTheme::Em( 0.2f ) ) + ImGuiTheme::Em( 0.1f );
        float swatch = ImGuiTheme::Em( 0.7f );
        const float inner = ImGuiTheme::Em( 0.25f );
        const float gap = ImGuiTheme::Em( 0.8f );
        float total = 0.f;
        for( int mode = 0; mode < kRolloffCount; ++mode )
        {
            total += swatch + inner + ImGui::CalcTextSize( RolloffName( mode ) ).x + ( mode + 1 < kRolloffCount ? gap : 0.f );
        }
        if( total > width - ImGuiTheme::Em( 0.5f ) )
        {
            swatch = 0.f;
        }
        const float textOffset = swatch > 0.f ? swatch + inner : 0.f;
        float left = map.Min.x;
        for( int mode = 0; mode < kRolloffCount; ++mode )
        {
            const char* name = RolloffName( mode );
            const ImVec2 size = ImGui::CalcTextSize( name );
            const ImRect rect( ImVec2( left, legendTop ), ImVec2( left + textOffset + size.x, legendTop + size.y ) );
            bool hovered = false;
            if( Graph::MarkerButton( name, rect, &hovered ) )
            {
                if( mode != InOutRolloff )
                {
                    InOutRolloff = mode;
                    changed = true;
                }
                Juice::Flash( canvasId );
                Juice::Tick( canvasId, ImVec2( rect.GetCenter().x, rect.Min.y ) );
            }
            ItemLog::SetItemValue( ImGui::GetItemID(), RolloffReading( mode, InOutMin, InOutMax ) );
            if( hovered )
            {
                static const char* const kNotes[] = {
                    "realistic: halves (-6 dB) with each doubling of distance past Min Distance, and holds its level past Max Distance",
                    "a straight fade from full at Min Distance to silence at Max Distance",
                    "fades faster near Min Distance, then trails off to silence at Max Distance",
                };
                ImGui::SetTooltip( "%s: %s\n%s", name, kNotes[mode], RolloffReading( mode, InOutMin, InOutMax ).c_str() );
            }
            if( visible )
            {
                const bool current = mode == active;
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
            ImGui::SetTooltip( "How loud the sound is at each distance from the listener (FMOD's rolloff; log distance, gain in dB).\n"
                               "The faint curves are the other rolloffs; the dot is where it reaches -6 dB (half amplitude).\n"
                               "Drag Min Distance (full volume inside it) and Max Distance; click a name below to switch rolloff.%s",
                is2D ? "\nSpatial Blend is 0: the voice is 2D and plays at full volume at any distance." : "" );
        }
        if( changed )
        {
            MarkEdited( canvasId );
        }
        if( visible )
        {
            FlashOutline( drawList, Graph::CanvasRect(), canvasId );
        }
        ItemLog::SetItemValue( canvasId, RolloffReading( std::clamp( InOutRolloff, 0, kRolloffCount - 1 ), InOutMin, InOutMax ) );
        ImGui::PopID();
        return changed;
    }

    // ---- Spatial blend ------------------------------------------------------------------------

    namespace
    {
        std::string BlendText( double InBlend )
        {
            if( !( InBlend > 0.0 ) )
            {
                return "2D";
            }
            if( InBlend >= 1.0 )
            {
                return "3D";
            }
            return FormatValue( Unit::Percent, InBlend ) + " 3D";
        }
    }

    bool SpatialBlendSlider( const char* InCaption, double& InOutBlend, const NumberSpec& InSpec )
    {
        ImGui::PushID( InCaption );
        const float width = HavanaUtils::Label( InCaption );
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const ImGuiContext& g = *GImGui;
        const float height = ImGui::GetFrameHeight();
        const float gap = ImGuiTheme::InnerGap();
        const float chipWidth = std::max( ImGui::CalcTextSize( "2D" ).x, ImGui::CalcTextSize( "3D" ).x ) + ImGuiTheme::Em( 0.8f );
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImRect chip2D( start, ImVec2( start.x + chipWidth, start.y + height ) );
        const ImRect chip3D( ImVec2( start.x + width - chipWidth, start.y ), ImVec2( start.x + width, start.y + height ) );
        const float trackWidth = std::max( width - 2.f * ( chipWidth + gap ), ImGuiTheme::Em( 3.f ) );
        ImGui::SetCursorScreenPos( ImVec2( chip2D.Max.x + gap, start.y ) );

        Graph::Frame frame;
        frame.XLabels = false;
        frame.Background = false;
        frame.Inset = 0.f;
        const bool visible = Graph::Begin( InCaption, ImVec2( trackWidth, height ), Graph::Axis(), Graph::Axis(), frame );
        const Graph::Mapping& map = Graph::Current();
        const ImGuiID canvasId = Graph::CanvasId();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect track( map.Min, map.Max );
        const Graph::CanvasInput input = Graph::End();
        bool changed = false;
        const double detents[] = { 0.0, 0.5, 1.0 };

        if( input.Pressed || ( input.Held && g.IO.MouseDelta.x != 0.f ) )
        {
            const double raw = std::clamp( RoundTo( map.XFromScreen( g.IO.MousePos.x ), 0.01 ), 0.0, 1.0 );
            const double reach = ImGuiTheme::Em( 0.4f ) / std::max( track.GetWidth(), 1.f );
            double next = raw;
            for( const double detent : detents )
            {
                next = input.Pressed ? ( std::abs( raw - detent ) < reach ? detent : next ) : Detent( InOutBlend, next, detent, reach );
            }
            next = Constrain( next, InSpec );
            bool ticked = false;
            for( const double detent : detents )
            {
                ticked |= next == detent && InOutBlend != detent;
            }
            if( ticked )
            {
                Juice::Tick( canvasId, ImVec2( map.XToScreen( next ), track.GetCenter().y ) );
            }
            if( next != InOutBlend )
            {
                InOutBlend = next;
                changed = true;
            }
        }
        if( input.Hovered && !input.Held )
        {
            ImGui::SetTooltip( "Spatial blend: 2D plays the same in both ears at any distance (music, UI); 3D is positioned at this entity,\n"
                               "panned and faded with distance. Between them FMOD mixes the two. Drag along the track (detents at the ends\n"
                               "and the middle) or click an end." );
        }

        // The ends: click to snap.
        const double ends[] = { 0.0, 1.0 };
        const ImRect chips[] = { chip2D, chip3D };
        const char* const names[] = { "2D", "3D" };
        ImGuiID chipIds[2] = {};
        bool chipHovered[2] = {};
        for( int i = 0; i < 2; ++i )
        {
            if( Graph::MarkerButton( names[i], chips[i], &chipHovered[i] ) )
            {
                const double next = Constrain( ends[i], InSpec );
                if( next != InOutBlend )
                {
                    InOutBlend = next;
                    changed = true;
                }
                Juice::Flash( ImGui::GetItemID() );
                Juice::Play( Juice::Cue::Tick );
            }
            chipIds[i] = ImGui::GetItemID();
            ItemLog::SetLastItemRow( InCaption );
            ItemLog::SetItemValue( chipIds[i], BlendText( ends[i] ) );
            if( chipHovered[i] )
            {
                ImGui::SetTooltip( i == 0 ? "2D: no panning or distance fade" : "3D: positioned at this entity, faded by the rolloff" );
            }
        }

        if( visible )
        {
            const float rounding = ImGui::GetStyle().FrameRounding;
            drawList->AddRectFilled( track.Min, track.Max, U32( input.Hovered || input.Held ? palette.ControlHovered : palette.Control ), rounding );
            const float t = std::clamp( Follow( ImGui::GetID( "##fill" ), static_cast<float>( std::clamp( InOutBlend, 0.0, 1.0 ) ), canvasId ), 0.f, 1.f );
            const float x = std::round( track.Min.x + t * track.GetWidth() );
            if( x > track.Min.x )
            {
                drawList->AddRectFilled( track.Min, ImVec2( x, track.Max.y ), U32( ImGuiTheme::WithAlpha( palette.Accent, 0.45f ) ), rounding, ImDrawFlags_RoundCornersLeft );
            }
            // The centre detent.
            const float mid = std::round( track.GetCenter().x );
            drawList->AddLine( ImVec2( mid, track.Min.y + height * 0.25f ), ImVec2( mid, track.Max.y - height * 0.25f ), U32( ImGuiTheme::WithAlpha( palette.Text, 0.35f ) ), Thin() );
            // The grab.
            const float grab = ImGuiTheme::Em( 0.3f );
            const float gx = std::clamp( x, track.Min.x + grab * 0.5f, track.Max.x - grab * 0.5f );
            drawList->AddRectFilled( ImVec2( gx - grab * 0.5f, track.Min.y + Thin() ), ImVec2( gx + grab * 0.5f, track.Max.y - Thin() ), U32( input.Held ? palette.Pressed : palette.Text ), rounding );
            // The reading, in the free side of the track.
            const std::string reading = BlendText( InOutBlend );
            const ImVec2 size = ImGui::CalcTextSize( reading.c_str() );
            const float textX = t > 0.5f ? track.Min.x + ImGuiTheme::Em( 0.4f ) : track.Max.x - size.x - ImGuiTheme::Em( 0.4f );
            drawList->AddText( ImVec2( std::round( textX ), std::round( track.GetCenter().y - size.y * 0.5f ) ), U32( palette.Text ), reading.c_str() );
            TickOverlay( drawList, track, canvasId );
            FlashOutline( drawList, track, canvasId );
        }
        const double now = InOutBlend;
        DrawChip( drawList, chip2D, "2D", !( now > 0.0 ), chipHovered[0], chipIds[0] );
        DrawChip( drawList, chip3D, "3D", now >= 1.0, chipHovered[1], chipIds[1] );
        ItemLog::SetItemValue( canvasId, BlendText( now ) );
        if( changed )
        {
            MarkEdited( canvasId );
        }
        ImGui::PopID();
        return changed;
    }
}
