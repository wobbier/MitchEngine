#include "HavanaUtils.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuiTheme.h>
#include "Widgets/ItemLog.h"
#include "Widgets/ScrubField.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
    // The current window's row span in screen space: its content area without indentation.
    // Panels that zero WindowPadding (so headers run edge to edge) and borderless child
    // windows still get the standard inset. A table cell spans the cell.
    void RowSpan( float& OutLeft, float& OutRight )
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        if( g.CurrentTable || window->DC.CurrentColumns )
        {
            OutLeft = window->WorkRect.Min.x;
            OutRight = window->WorkRect.Max.x;
            return;
        }
        const float inset = std::max( 0.f, g.Style.WindowPadding.x - window->WindowPadding.x );
        OutLeft = window->ContentRegionRect.Min.x + inset;
        OutRight = std::max( OutLeft, window->ContentRegionRect.Max.x - inset );
    }
}


float HavanaUtils::ValueColumnX()
{
    float left = 0.f;
    float right = 0.f;
    RowSpan( left, right );
    return left + std::min( std::round( ( right - left ) * ImGuiTheme::kLabelColumn ), ImGuiTheme::Em( ImGuiTheme::kMaxLabelColumn ) );
}


float HavanaUtils::RowRight()
{
    float left = 0.f;
    float right = 0.f;
    RowSpan( left, right );
    return right;
}


float HavanaUtils::RowWidth()
{
    return std::max( RowRight() - ImGui::GetCursorScreenPos().x, 1.f );
}


float HavanaUtils::Label( const std::string& Name, float customWidth )
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float right = RowRight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    // Deep indentation can reach the value column: the label then shrinks to nothing.
    const float valueX = std::max( customWidth >= 0.f ? right - customWidth : ValueColumnX(), start.x );
    const float labelWidth = std::max( valueX - ImGuiTheme::InnerGap() - start.x, 0.f );

    ImGui::AlignTextToFramePadding();
    const ImVec2 textSize = ImGui::CalcTextSize( Name.c_str(), nullptr, true );
    const ImVec2 textPos( start.x, start.y + window->DC.CurrLineTextBaseOffset );
    const ImRect bb( textPos, ImVec2( textPos.x + labelWidth, textPos.y + textSize.y ) );
    ImGui::ItemSize( ImVec2( labelWidth, textSize.y ), 0.f );
    const bool visible = ImGui::ItemAdd( bb, window->GetID( Name.c_str() ) );
    MeWidgets::ItemLog::MarkRowLabel( Name.c_str() );
    if( visible )
    {
        ImGui::RenderTextEllipsis( window->DrawList, bb.Min, bb.Max, bb.Max.x, bb.Max.x, Name.c_str(), nullptr, &textSize );
        if( textSize.x > labelWidth && ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "%s", Name.c_str() );
        }
    }

    ImGui::SameLine( 0.f, 0.f );
    ImGui::SetCursorScreenPos( ImVec2( valueX, ImGui::GetCursorScreenPos().y ) );
    const float width = std::max( right - valueX, 1.f );
    ImGui::SetNextItemWidth( width );
    return width;
}


float HavanaUtils::SkipLabel()
{
    const float valueX = std::max( ValueColumnX(), ImGui::GetCursorScreenPos().x );
    ImGui::SetCursorScreenPos( ImVec2( valueX, ImGui::GetCursorScreenPos().y ) );
    const float width = std::max( RowRight() - valueX, 1.f );
    ImGui::SetNextItemWidth( width );
    return width;
}


void HavanaUtils::Text( const std::string& Name, const Vector2& Vector )
{
    ImGui::Text( "%s", Name.c_str() );

    ImGui::Text( "X: %f", Vector.x );
    ImGui::SameLine();
    ImGui::Text( "Y: %f", Vector.y );
}


void HavanaUtils::Text( const std::string& Name, const Vector3& Vector )
{
    ImGui::Text( "%s", Name.c_str() );

    ImGui::Text( "X: %f", Vector.x );
    ImGui::SameLine();
    ImGui::Text( "Y: %f", Vector.y );
    ImGui::SameLine();
    ImGui::Text( "Z: %f", Vector.z );
}


bool HavanaUtils::DragAxes( const char* InId, float* InOutValues, int InCount, float InSpeed, const char* InFormat, bool* OutActive, const float* InResetValue )
{
    static const char* kAxisNames[4] = { "X", "Y", "Z", "W" };
    static const char* kHandleNames[4] = { "X:axis", "Y:axis", "Z:axis", "W:axis" };
    ImGuiContext& g = *GImGui;
    bool changed = false;
    ImGui::PushID( InId );
    // One group, so callers can ask IsItemActive / IsItemDeactivatedAfterEdit of the vector.
    ImGui::BeginGroup();
    const float gap = ImGuiTheme::InnerGap();
    const float slot = std::max( ( ImGui::CalcItemWidth() - gap * static_cast<float>( InCount - 1 ) ) / static_cast<float>( InCount ), 1.f );
    // Each axis starts with its coloured handle (UX-11): drag it sideways to scrub that axis.
    const float handle = std::min( std::round( ImGuiTheme::Em( 0.9f ) ), std::round( slot * 0.4f ) );
    ImRect fields[4];
    const int count = std::min( InCount, 4 );
    for( int i = 0; i < count; ++i )
    {
        ImGui::PushID( i );
        if( i > 0 )
        {
            ImGui::SameLine( 0.f, gap + handle );
        }
        else
        {
            ImGui::SetCursorScreenPos( ImVec2( ImGui::GetCursorScreenPos().x + handle, ImGui::GetCursorScreenPos().y ) );
        }
        ImGui::SetNextItemWidth( std::max( slot - handle, 1.f ) );
        changed |= ImGui::DragFloat( "##axis", &InOutValues[i], InSpeed, 0.f, 0.f, InFormat );
        if( MeWidgets::ItemLog::IsEnabled() )
        {
            char value[64];
            std::snprintf( value, sizeof( value ), InFormat, InOutValues[i] );
            MeWidgets::ItemLog::SetLastItemValue( value );
            MeWidgets::ItemLog::SetLastItemLabel( kAxisNames[i] );
        }
        if( OutActive && ImGui::IsItemActive() )
        {
            *OutActive = true;
        }
        fields[i] = ImRect( ImGui::GetItemRectMin(), ImGui::GetItemRectMax() );
        if( InResetValue && ImGui::BeginPopupContextItem( "AxisContext" ) )
        {
            char label[48];
            std::snprintf( label, sizeof( label ), "Reset %s to %g", kAxisNames[i], *InResetValue );
            if( ImGui::MenuItem( label ) )
            {
                InOutValues[i] = *InResetValue;
                changed = true;
            }
            if( ImGui::MenuItem( "Reset All" ) )
            {
                std::fill( InOutValues, InOutValues + InCount, *InResetValue );
                changed = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // The handles, after the fields so "Row@2" still names the second axis' field. Each is an
    // item named "X:axis" in the row: drag it like a row label (Shift fine, Ctrl coarse, Alt
    // snaps) at the field's speed; double-click it for the reset value.
    MeWidgets::NumberSpec spec;
    spec.Speed = InSpeed;
    spec.HasDefault = InResetValue != nullptr;
    spec.Default = InResetValue ? *InResetValue : 0.0;
    const float rounding = g.Style.FrameRounding;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 textDark = ImGui::GetColorU32( ImGuiTheme::Gray( 0.08f ) );
    const ImU32 textLight = ImGui::GetColorU32( ImGuiTheme::Gray( 0.97f ) );
    for( int i = 0; i < count; ++i )
    {
        ImGui::PushID( i );
        const ImRect bb( ImVec2( fields[i].Min.x - handle, fields[i].Min.y ), ImVec2( fields[i].Min.x, fields[i].Max.y ) );
        const ImGuiID id = ImGui::GetID( "##handle" );
        if( ImGui::ItemAdd( bb, id ) )
        {
            double value = InOutValues[i];
            if( MeWidgets::ScrubLastItem( value, spec, slot ) )
            {
                InOutValues[i] = static_cast<float>( value );
                changed = true;
            }
            const bool active = ImGui::IsItemActive();
            const bool hovered = ImGui::IsItemHovered();
            if( OutActive && active )
            {
                *OutActive = true;
            }
            if( hovered && !active && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            {
                if( InResetValue )
                {
                    ImGui::SetTooltip( "Drag to scrub %s (Shift fine, Ctrl coarse, Alt snaps)\nDouble-click to reset to %g", kAxisNames[i], *InResetValue );
                }
                else
                {
                    ImGui::SetTooltip( "Drag to scrub %s (Shift fine, Ctrl coarse, Alt snaps)", kAxisNames[i] );
                }
            }
            // Drawn over the field's rounded left corners, so handle and field read as one box.
            ImVec4 color = ImGui::ColorConvertU32ToFloat4( ImGuiTheme::AxisColor( i ) );
            if( active || hovered )
            {
                color = ImGuiTheme::Mix( color, ImGuiTheme::Gray( 1.f ), active ? 0.3f : 0.18f );
            }
            drawList->AddRectFilled( bb.Min, ImVec2( bb.Max.x + rounding, bb.Max.y ), ImGui::GetColorU32( color ), rounding, ImDrawFlags_RoundCornersLeft );
            const ImVec2 letter = ImGui::CalcTextSize( kAxisNames[i] );
            if( letter.x <= bb.GetWidth() )
            {
                const ImU32 ink = ImGuiTheme::Contrast( color, ImGuiTheme::Gray( 0.08f ) ) >= ImGuiTheme::Contrast( color, ImGuiTheme::Gray( 0.97f ) ) ? textDark : textLight;
                drawList->AddText( ImVec2( std::round( bb.GetCenter().x - letter.x * 0.5f ), std::round( bb.GetCenter().y - letter.y * 0.5f ) ), ink, kAxisNames[i] );
            }
        }
        IMGUI_TEST_ENGINE_ITEM_INFO( id, kHandleNames[i], g.LastItemData.StatusFlags );
        if( MeWidgets::ItemLog::IsEnabled() )
        {
            char value[64];
            std::snprintf( value, sizeof( value ), InFormat, InOutValues[i] );
            MeWidgets::ItemLog::SetItemValue( id, value );
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}


bool HavanaUtils::EditableVector3( const std::string& Name, Vector3& Vector, float ResetValue /*= 0.f*/, float customWidth )
{
    ImGui::PushID( Name.c_str() );
    Label( Name, customWidth );
    const bool changed = DragAxes( "##vec", &Vector.x, 3, 0.1f, "%.3f", nullptr, &ResetValue );
    ImGui::PopID();
    return changed;
}


bool HavanaUtils::EditableVector3Spring( const std::string& Name, Vector3& Vector, float ResetValue /*= 0.f*/, float customWidth /*= -1 */ )
{
    return EditableVector3( Name, Vector, ResetValue, customWidth );
}


bool HavanaUtils::EditableVector( const std::string& Name, Vector2& Vector, float ResetValue /*= 0.f*/, float customWidth )
{
    ImGui::PushID( Name.c_str() );
    Label( Name, customWidth );
    const bool changed = DragAxes( "##vec", &Vector.x, 2, 0.1f, "%.3f", nullptr, &ResetValue );
    ImGui::PopID();
    return changed;
}


bool HavanaUtils::Float( const std::string& Name, float& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragFloat( "##float", &value );

    ImGui::PopID();

    return res;
}


bool HavanaUtils::Double( const std::string& Name, double& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragScalar( "##float", ImGuiDataType_Double, &value );

    ImGui::PopID();

    return res;
}


bool HavanaUtils::Int( const std::string& Name, int32_t& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragScalar( "##int", ImGuiDataType_S32, &value );

    ImGui::PopID();

    return res;
}


bool HavanaUtils::Int( const std::string& Name, int16_t& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragScalar( "##int", ImGuiDataType_S16, &value );

    ImGui::PopID();

    return res;
}


bool HavanaUtils::Int( const std::string& Name, int64_t& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragScalar( "##int", ImGuiDataType_S64, &value );

    ImGui::PopID();

    return res;
}


bool HavanaUtils::UInt( const std::string& Name, uint32_t& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragScalar( "##uint", ImGuiDataType_U32, &value );

    ImGui::PopID();

    return res;
}


bool HavanaUtils::UInt( const std::string& Name, uint16_t& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragScalar( "##uint", ImGuiDataType_U16, &value );

    ImGui::PopID();

    return res;
}


bool HavanaUtils::UInt( const std::string& Name, uint64_t& value )
{
    ImGui::PushID( Name.c_str() );
    Label( Name );

    bool res = ImGui::DragScalar( "##uint", ImGuiDataType_U64, &value );

    ImGui::PopID();

    return res;
}


void HavanaUtils::ColorButton( const std::string& Name, Vector3& arr )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );
    // Generate a default palette. The palette will persist and can be edited.
    ImVec4 color = { arr.x, arr.y ,arr.z, 1.f };
    static bool saved_palette_init = true;
    static ImVec4 saved_palette[32] = {};
    if( saved_palette_init )
    {
        for( int n = 0; n < IM_ARRAYSIZE( saved_palette ); n++ )
        {
            ImGui::ColorConvertHSVtoRGB( n / 31.0f, 0.8f, 0.8f,
                saved_palette[n].x, saved_palette[n].y, saved_palette[n].z );
            saved_palette[n].w = 1.0f; // Alpha
        }
        saved_palette_init = false;
    }

    static ImVec4 backup_color;
    bool open_popup = ImGui::ColorButton( "MyColor##3b", color, 0 | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoAlpha, { size, 0.f } );
    if( open_popup )
    {
        ImGui::OpenPopup( "mypicker" );
        backup_color = color;
    }
    if( ImGui::BeginPopup( "mypicker" ) )
    {
        ImGui::ColorPicker4( "##picker", (float*)&color, 0 | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoAlpha );
        ImGui::SameLine();

        const ImVec2 previewSize( ImGuiTheme::Em( 3.3f ), ImGuiTheme::Em( 2.2f ) );
        const ImVec2 swatchSize( ImGuiTheme::Em( 1.1f ), ImGuiTheme::Em( 1.1f ) );
        ImGui::BeginGroup(); // Lock X position
        ImGui::Text( "Current" );
        ImGui::ColorButton( "##current", color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoAlpha, previewSize );
        ImGui::Text( "Previous" );
        if( ImGui::ColorButton( "##previous", backup_color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoAlpha, previewSize ) )
            color = backup_color;
        ImGui::Separator();
        ImGui::Text( "Palette" );
        for( int n = 0; n < IM_ARRAYSIZE( saved_palette ); n++ )
        {
            ImGui::PushID( n );
            if( ( n % 8 ) != 0 )
                ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemSpacing.y );

            ImGuiColorEditFlags palette_button_flags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha;
            if( ImGui::ColorButton( "##palette", saved_palette[n], palette_button_flags, swatchSize ) )
                color = ImVec4( saved_palette[n].x, saved_palette[n].y, saved_palette[n].z, color.w ); // Preserve alpha!

            // Allow user to drop colors into each palette entry. Note that ColorButton() is already a
            // drag source by default, unless specifying the ImGuiColorEditFlags_NoDragDrop flag.
            if( ImGui::BeginDragDropTarget() )
            {
                if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( IMGUI_PAYLOAD_TYPE_COLOR_3F ) )
                    memcpy( (float*)&saved_palette[n], payload->Data, sizeof( float ) * 3 );
                if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( IMGUI_PAYLOAD_TYPE_COLOR_4F ) )
                    memcpy( (float*)&saved_palette[n], payload->Data, sizeof( float ) * 4 );
                ImGui::EndDragDropTarget();
            }

            ImGui::PopID();
        }
        ImGui::EndGroup();
        ImGui::EndPopup();
    }
    ImGui::PopID();

    arr.x = color.x;
    arr.y = color.y;
    arr.z = color.z;
}


bool HavanaUtils::ToggleButton( const std::string& InName, bool& OutPressed )
{
    if( ImGui::Button( InName.c_str() ) )
    {
        OutPressed = !OutPressed;
        return true;
    }
    return false;
}
