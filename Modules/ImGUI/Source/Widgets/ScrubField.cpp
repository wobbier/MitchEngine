#include "ScrubField.h"
#include "ItemLog.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MeWidgets
{
    namespace
    {
        // The one scrub in progress (ImGui has one active item at a time).
        struct ScrubGesture
        {
            ImGuiID Id = 0;
            double Raw = 0.0;       // unsnapped, unclamped value the drag accumulates
            ImVec2 PressPos;
            bool Dragged = false;
        };

        ScrubGesture& Gesture()
        {
            static ScrubGesture gesture;
            return gesture;
        }

        // The one text entry in progress, and the last parse error (shown for a moment).
        struct TextEntry
        {
            ImGuiID Id = 0;         // the field being typed into
            char Buffer[128] = {};
            std::string Initial;
            ImGuiID ErrorId = 0;
            std::string Error;
            double ErrorUntil = 0.0;
        };

        TextEntry& Entry()
        {
            static TextEntry entry;
            return entry;
        }

        double LogOffset( double InMin, double InMax )
        {
            return InMin > 0.0 ? 0.0 : ( InMax - InMin ) * 1e-3 - InMin;
        }

        // Shift fine, Ctrl coarse.
        double ModifierScale()
        {
            const ImGuiIO& io = ImGui::GetIO();
            return io.KeyShift ? 0.1 : io.KeyCtrl ? 10.0 : 1.0;
        }

        // Rounds to a 1-2-5 sequence for log fields, or to NiceStep of the span.
        double Snap( double InValue, const NumberSpec& InSpec )
        {
            double low = 0.0;
            double high = 0.0;
            if( InSpec.Log && InValue > 0.0 )
            {
                const double power = std::pow( 10.0, std::floor( std::log10( InValue ) ) );
                const double scaled = InValue / power;
                const double steps[] = { 1.0, 2.0, 5.0, 10.0 };
                double best = steps[0];
                for( double step : steps )
                {
                    if( std::abs( std::log( scaled / step ) ) < std::abs( std::log( scaled / best ) ) ) best = step;
                }
                return best * power;
            }
            const double step = InSpec.ScaleRange( low, high ) ? NiceStep( high - low ) : NiceStep( InSpec.Speed * 100.0 );
            return std::round( InValue / step ) * step;
        }

        bool IsMixed()
        {
            return ( GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue ) != 0;
        }

        // Starts a scrub on InId at the current mouse position.
        void BeginScrub( ImGuiID InId, double InValue )
        {
            ImGuiContext& g = *GImGui;
            ImGui::SetActiveID( InId, g.CurrentWindow );
            ImGui::SetFocusID( InId, g.CurrentWindow );
            ImGui::FocusWindow( g.CurrentWindow );
            ImGui::SetKeyOwner( ImGuiKey_MouseLeft, InId );
            Gesture() = { InId, InValue, g.IO.MousePos, false };
        }

        // Applies this frame's mouse movement to an active scrub. Returns true if the value changed.
        bool UpdateScrub( ImGuiID InId, double& InOutValue, const NumberSpec& InSpec, float InPixelsForSpan, bool InMixed = false )
        {
            ImGuiContext& g = *GImGui;
            ScrubGesture& gesture = Gesture();
            if( gesture.Id != InId )
            {
                gesture = { InId, InOutValue, g.IO.MousePos, false };
            }
            ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
            if( !gesture.Dragged && std::abs( g.IO.MousePos.x - gesture.PressPos.x ) >= g.IO.MouseDragThreshold * 0.5f )
            {
                gesture.Dragged = true;
            }
            const float delta = g.IO.MouseDelta.x;
            if( !gesture.Dragged || delta == 0.f )
            {
                return false;
            }
            gesture.Raw = ScrubValue( gesture.Raw, delta * ModifierScale(), InSpec, InPixelsForSpan );
            if( InSpec.HasRange )
            {
                // The accumulator stays inside the clamp, so turning back responds at once.
                gesture.Raw = std::clamp( gesture.Raw, InSpec.Min, InSpec.Max );
            }
            if( InSpec.HasClamp )
            {
                gesture.Raw = std::clamp( gesture.Raw, InSpec.ClampMin, InSpec.ClampMax );
            }
            const double next = Constrain( g.IO.KeyAlt ? Snap( gesture.Raw, InSpec ) : gesture.Raw, InSpec );
            if( next == InOutValue && !InMixed )
            {
                return false;
            }
            InOutValue = next;
            return true;
        }

        std::string Shown( double InValue, const NumberSpec& InSpec )
        {
            return FormatValue( InSpec.FieldUnit, InValue, InSpec.Integer ? 0 : InSpec.Decimals );
        }
    }

    bool NumberSpec::ScaleRange( double& OutMin, double& OutMax ) const
    {
        if( HasSoftRange && SoftMax > SoftMin )
        {
            OutMin = SoftMin;
            OutMax = SoftMax;
            return true;
        }
        if( HasRange && Max > Min )
        {
            OutMin = Min;
            OutMax = Max;
            return true;
        }
        return false;
    }

    double ScrubValue( double InRaw, double InPixels, const NumberSpec& InSpec, float InPixelsForSpan )
    {
        const double pixelsForSpan = std::max( 1.0, static_cast<double>( InPixelsForSpan ) );
        double low = 0.0;
        double high = 0.0;
        if( InSpec.ScaleRange( low, high ) )
        {
            if( InSpec.Log )
            {
                const double offset = LogOffset( low, high );
                const double logLow = std::log( low + offset );
                const double logHigh = std::log( high + offset );
                const double floor = ( low + offset ) * 1e-6;
                const double u = std::log( std::max( InRaw + offset, floor ) ) + InPixels * ( logHigh - logLow ) / pixelsForSpan;
                return std::exp( u ) - offset;
            }
            return InRaw + InPixels * ( high - low ) / pixelsForSpan;
        }
        if( InSpec.Log && InRaw > 0.0 )
        {
            // Unbounded log: 1% per pixel.
            return InRaw * std::exp( InPixels * 0.01 );
        }
        return InRaw + InPixels * InSpec.Speed;
    }

    double Constrain( double InValue, const NumberSpec& InSpec )
    {
        double value = InValue;
        if( InSpec.Integer )
        {
            value = std::round( value );
        }
        if( InSpec.HasRange && InSpec.Max >= InSpec.Min )
        {
            value = std::clamp( value, InSpec.Min, InSpec.Max );
        }
        if( InSpec.HasClamp && InSpec.ClampMax >= InSpec.ClampMin )
        {
            value = std::clamp( value, InSpec.ClampMin, InSpec.ClampMax );
        }
        return value;
    }

    double FillFraction( double InValue, const NumberSpec& InSpec )
    {
        double low = 0.0;
        double high = 0.0;
        if( !InSpec.ScaleRange( low, high ) )
        {
            return -1.0;
        }
        if( InSpec.Log )
        {
            return LogMap::ToNormalized( InValue, low, high );
        }
        return std::clamp( ( InValue - low ) / ( high - low ), 0.0, 1.0 );
    }

    bool ScrubLastItem( double& InOutValue, const NumberSpec& InSpec, float InPixelsForSpan, bool InMixed )
    {
        ImGuiContext& g = *GImGui;
        const ImGuiID id = g.LastItemData.ID;
        if( id == 0 )
        {
            return false;
        }
        const ImRect bb = g.LastItemData.Rect;
        const bool hovered = ImGui::ItemHoverable( bb, id, g.LastItemData.InFlags );
        bool changed = false;
        if( hovered )
        {
            ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
            if( ImGui::IsMouseClicked( ImGuiMouseButton_Left, id ) )
            {
                BeginScrub( id, InOutValue );
                if( g.IO.MouseClickedCount[ImGuiMouseButton_Left] == 2 && InSpec.HasDefault )
                {
                    // Double-click: back to the default.
                    const double reset = Constrain( InSpec.Default, InSpec );
                    if( reset != InOutValue || InMixed )
                    {
                        InOutValue = reset;
                        changed = true;
                    }
                    Gesture().Raw = InOutValue;
                }
            }
        }
        if( g.ActiveId == id )
        {
            if( g.IO.MouseDown[ImGuiMouseButton_Left] )
            {
                changed |= UpdateScrub( id, InOutValue, InSpec, InPixelsForSpan, InMixed );
            }
            else
            {
                ImGui::ClearActiveID();
            }
        }
        if( changed )
        {
            ImGui::MarkItemEdited( id );
        }
        return changed;
    }

    bool UnitField( const char* InId, double& InOutValue, const NumberSpec& InSpec )
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if( window->SkipItems )
        {
            return false;
        }
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID( InId );
        const float width = ImGui::CalcItemWidth();
        const ImRect bb( window->DC.CursorPos, ImVec2( window->DC.CursorPos.x + width, window->DC.CursorPos.y + ImGui::GetFrameHeight() ) );
        ImGui::ItemSize( bb, style.FramePadding.y );
        if( !ImGui::ItemAdd( bb, id, &bb, ImGuiItemFlags_Inputable ) )
        {
            // Scrolled out of view: nothing to draw, but automation can still read it.
            if( MeWidgets::ItemLog::IsEnabled() )
            {
                MeWidgets::ItemLog::SetItemValue( id, IsMixed() ? std::string( "\xE2\x80\x94" ) : Shown( InOutValue, InSpec ) );
            }
            return false;
        }
        const bool mixed = IsMixed();
        const bool hovered = ImGui::ItemHoverable( bb, id, g.LastItemData.InFlags );
        bool editing = ImGui::TempInputIsActive( id );
        bool changed = false;
        TextEntry& entry = Entry();

        // Typing that ended without Enter or a click away (Tab, arrow navigation) still applies.
        if( !editing && entry.Id == id )
        {
            entry.Id = 0;
            if( entry.Initial != entry.Buffer && g.ActiveIdPreviousFrame == id )
            {
                double parsed = 0.0;
                if( ParseValue( InSpec.FieldUnit, entry.Buffer, parsed ) )
                {
                    InOutValue = Constrain( parsed, InSpec );
                    changed = true;
                }
            }
        }

        if( !editing )
        {
            const bool clicked = hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left, id );
            const bool doubleClicked = hovered && g.IO.MouseClickedCount[ImGuiMouseButton_Left] == 2 && ImGui::TestKeyOwner( ImGuiKey_MouseLeft, id );
            const bool navInput = g.NavActivateId == id && ( g.NavActivateFlags & ImGuiActivateFlags_PreferInput );
            if( clicked && !g.IO.KeyCtrl && !doubleClicked )
            {
                BeginScrub( id, InOutValue );
            }
            else if( clicked || doubleClicked || navInput )
            {
                editing = true;
            }
            else if( g.NavActivateId == id )
            {
                // Keyboard activation (Space/Enter on a focused field) types too.
                editing = true;
                g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
            }

            if( !editing && g.ActiveId == id )
            {
                if( g.IO.MouseDown[ImGuiMouseButton_Left] )
                {
                    changed = UpdateScrub( id, InOutValue, InSpec, width );
                }
                else
                {
                    // A click without a drag types a value.
                    const bool dragged = Gesture().Id == id && Gesture().Dragged;
                    ImGui::ClearActiveID();
                    if( !dragged && hovered )
                    {
                        g.NavActivateId = id;
                        g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
                        editing = true;
                    }
                }
            }
            if( editing && !g.NavActivateId )
            {
                // Mouse-started entry: let InputText take the click this frame.
                g.NavActivateId = id;
                g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
            }
            if( editing )
            {
                entry.Id = id;
                entry.Initial = mixed ? std::string() : Shown( InOutValue, InSpec );
                std::snprintf( entry.Buffer, sizeof( entry.Buffer ), "%s", entry.Initial.c_str() );
            }
        }

        if( editing )
        {
            const bool enter = ImGui::TempInputText( bb, id, InId, entry.Buffer, sizeof( entry.Buffer ),
                ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue );
            const bool finished = enter || ImGui::IsItemDeactivatedAfterEdit();
            if( finished )
            {
                entry.Id = 0;
            }
            if( finished && entry.Initial != entry.Buffer )
            {
                double parsed = 0.0;
                std::string error;
                if( ParseValue( InSpec.FieldUnit, entry.Buffer, parsed, &error ) )
                {
                    const double next = Constrain( parsed, InSpec );
                    if( next != InOutValue || mixed )
                    {
                        InOutValue = next;
                        changed = true;
                    }
                }
                else
                {
                    entry.ErrorId = id;
                    entry.Error = error;
                    entry.ErrorUntil = g.Time + 2.5;
                }
            }
            if( changed )
            {
                ImGui::MarkItemEdited( id );
            }
            MeWidgets::ItemLog::SetItemValue( id, Shown( InOutValue, InSpec ) );
            return changed;
        }

        // Frame, fill bar over the (soft) range, then the value in its unit.
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        const bool active = g.ActiveId == id;
        const ImU32 frameColor = ImGui::GetColorU32( active ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg );
        ImGui::RenderNavHighlight( bb, id );
        ImGui::RenderFrame( bb.Min, bb.Max, frameColor, true, style.FrameRounding );
        const double fill = mixed ? -1.0 : FillFraction( InOutValue, InSpec );
        if( fill > 0.0 )
        {
            const float right = bb.Min.x + static_cast<float>( fill ) * bb.GetWidth();
            const ImVec4 tint = ImGuiTheme::WithAlpha( palette.Accent, hovered || active ? 0.55f : 0.4f );
            window->DrawList->AddRectFilled( bb.Min, ImVec2( std::max( right, bb.Min.x + 1.f ), bb.Max.y ), ImGui::GetColorU32( tint ), style.FrameRounding,
                fill >= 0.999 ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft );
        }
        const std::string text = mixed ? std::string( "\xE2\x80\x94" ) : Shown( InOutValue, InSpec );
        ImGui::RenderTextClipped( bb.Min, bb.Max, text.c_str(), nullptr, nullptr, ImVec2( 0.5f, 0.5f ) );

        if( hovered && !active )
        {
            ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
        }
        if( entry.ErrorId == id && g.Time < entry.ErrorUntil )
        {
            window->DrawList->AddRect( bb.Min, bb.Max, ImGui::GetColorU32( palette.Error ), style.FrameRounding, 0, std::max( 1.f, std::round( ImGui::GetFontSize() / 9.f ) ) );
            ImGui::SetTooltip( "%s", entry.Error.c_str() );
        }
        if( changed )
        {
            ImGui::MarkItemEdited( id );
        }
        MeWidgets::ItemLog::SetItemValue( id, text );
        IMGUI_TEST_ENGINE_ITEM_INFO( id, InId, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Inputable );
        return changed;
    }

    bool UnitRow( const char* InLabel, double& InOutValue, const NumberSpec& InSpec )
    {
        ImGui::PushID( InLabel );
        const float width = HavanaUtils::Label( InLabel );
        bool changed = ScrubLastItem( InOutValue, InSpec, width );
        if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) && !ImGui::IsItemActive() && InSpec.HasDefault )
        {
            ImGui::SetTooltip( "Drag to change (Shift fine, Ctrl coarse, Alt snaps)\nDouble-click to reset to %s", Shown( InSpec.Default, InSpec ).c_str() );
        }
        ImGui::SetNextItemWidth( width );
        changed |= UnitField( "##value", InOutValue, InSpec );
        ImGui::PopID();
        return changed;
    }
}
