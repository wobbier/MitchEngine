#include "HavanaUtils.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuiTheme.h>
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
    if( ImGui::ItemAdd( bb, window->GetID( Name.c_str() ) ) )
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
    bool changed = false;
    ImGui::PushID( InId );
    // One group, so callers can ask IsItemActive / IsItemDeactivatedAfterEdit of the vector.
    ImGui::BeginGroup();
    const float gap = ImGuiTheme::InnerGap();
    const float width = std::max( ( ImGui::CalcItemWidth() - gap * static_cast<float>( InCount - 1 ) ) / static_cast<float>( InCount ), 1.f );
    const float marker = std::max( 2.f, std::round( ImGui::GetFontSize() / 6.f ) );
    for( int i = 0; i < InCount; ++i )
    {
        ImGui::PushID( i );
        if( i > 0 )
        {
            ImGui::SameLine( 0.f, gap );
        }
        ImGui::SetNextItemWidth( width );
        changed |= ImGui::DragFloat( "##axis", &InOutValues[i], InSpeed, 0.f, 0.f, InFormat );
        if( OutActive && ImGui::IsItemActive() )
        {
            *OutActive = true;
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled( min, ImVec2( min.x + marker, max.y ), ImGuiTheme::AxisColor( i ), ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft );
        if( InResetValue && ImGui::BeginPopupContextItem( "AxisContext" ) )
        {
            char label[48];
            std::snprintf( label, sizeof( label ), "Reset %s to %g", kAxisNames[std::min( i, 3 )], *InResetValue );
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
