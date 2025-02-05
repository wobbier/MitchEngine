#include "HavanaUtils.h"

#include <imgui.h>
#include <imgui_internal.h>
#include "imgui_stacklayout.h"

float HavanaUtils::Label( const std::string& Name, float customWidth )
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float fullWidth = ( customWidth >= 0 ) ? customWidth : ImGui::GetContentRegionAvail().x;
    float itemWidth = ( fullWidth * 0.65f );
    ImVec2 textSize = ImGui::CalcTextSize( Name.c_str() );
    ImRect textRect;
    textRect.Min = ImGui::GetCursorScreenPos();
    textRect.Min.x += ImGui::GetStyle().WindowPadding.x;
    textRect.Max = textRect.Min;
    textRect.Max.x += fullWidth - itemWidth - ImGui::GetStyle().WindowPadding.x;
    textRect.Max.y += textSize.y;

    ImGui::SetCursorScreenPos( textRect.Min );

    ImGui::AlignTextToFramePadding();
    textRect.Min.y += window->DC.CurrLineTextBaseOffset;
    textRect.Max.y += window->DC.CurrLineTextBaseOffset;

    ImGui::ItemSize( textRect );
    if( ImGui::ItemAdd( textRect, window->GetID( Name.c_str() ) ) )
    {
        ImGui::RenderTextEllipsis( ImGui::GetWindowDrawList(), textRect.Min, textRect.Max, textRect.Max.x,
            textRect.Max.x, Name.c_str(), nullptr, &textSize );

        if( textRect.GetWidth() < textSize.x && ImGui::IsItemHovered() )
            ImGui::SetTooltip( "%s", Name.c_str() );
    }
    ImGui::SetCursorScreenPos( { textRect.Max.x, textSize.y + window->DC.CurrLineTextBaseOffset } );
    ImGui::SameLine();
    ImGui::SetNextItemWidth( itemWidth - ( ImGui::GetStyle().WindowPadding.x / 2.f ) );

    return itemWidth - ( ImGui::GetStyle().WindowPadding.x / 2.f );
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

bool HavanaUtils::EditableVector3( const std::string& Name, Vector3& Vector, float ResetValue /*= 0.f*/, float customWidth )
{
    ImGui::PushID( Name.c_str() );

    float widgetWidth = Label( Name, customWidth );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.f, 0.f } );
    ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, { 2.f, 2.f } );
    float paddingX = 0.f;// ImGui::GetStyle().WindowPadding.x;
    Vector3 tempVec = Vector;
    if( ImGui::BeginTable( "##vec", 3, 0, { widgetWidth - paddingX, 0.f } ) )
    {

        ImGui::TableNextRow();

        float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
        ImVec2 buttonSize = { lineHeight + 3.f, lineHeight };

        float dragWidths = ( widgetWidth - ( buttonSize.x * 3 ) ) / 3.f;

        {
            ImGui::TableSetColumnIndex( 0 );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 1.f, 51.f / 255.f, 82.f / 255.f, .66f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 1.f, 51.f / 255.f, 82.f / 255.f, 1.f ) );
            if( ImGui::Button( "X", buttonSize ) )
            {
                Vector.x = ResetValue;
            }
            ImGui::PopStyleColor( 2 );
            ImGui::SameLine();
            ImGui::PushItemWidth( dragWidths );
            ImGui::DragFloat( "##X", &Vector.x, 0.1f, 0.f, 0.f, "%.6f");
            ImGui::PopItemWidth();
        }

        {
            ImGui::TableSetColumnIndex( 1 );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 139.f / 255.f, 220.f / 255.f, 0.f, .66f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 139.f / 255.f, 220.f / 255.f, 0.f, 1.f ) );
            if( ImGui::Button( "Y", buttonSize ) )
            {
                Vector.y = ResetValue;
            }
            ImGui::PopStyleColor( 2 );
            ImGui::SameLine();
            ImGui::PushItemWidth( dragWidths );
            ImGui::DragFloat( "##Y", &Vector.y, 0.1f, 0.f, 0.f, "%.6f" );
            ImGui::PopItemWidth();
        }

        {
            ImGui::TableSetColumnIndex( 2 );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 40.f / 255.f, 143.f / 255.f, 253.f / 255.f, .66f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 40.f / 255.f, 143.f / 255.f, 253.f / 255.f, 1.f ) );
            if( ImGui::Button( "Z", buttonSize ) )
            {
                Vector.z = ResetValue;
            }
            ImGui::PopStyleColor( 2 );
            ImGui::SameLine();
            ImGui::PushItemWidth( dragWidths );
            ImGui::DragFloat( "##Z", &Vector.z, 0.1f, 0.f, 0.f, "%.6f" );
            ImGui::PopItemWidth();
        }

        ImGui::EndTable();

    }

    ImGui::PopStyleVar( 2 );
    ImGui::PopID();

    return tempVec != Vector;
}

bool HavanaUtils::EditableVector3Spring( const std::string& Name, Vector3& Vector, float ResetValue /*= 0.f*/, float customWidth /*= -1 */ )
{
    //ImGui::PushID( Name.c_str() );

    ImGui::BeginHorizontal( Name.c_str() );

    float widgetWidth = Label( Name, customWidth );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.f, 0.f } );
    ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, { 2.f, 2.f } );
    float paddingX = 0.f;// ImGui::GetStyle().WindowPadding.x;
    ImGui::BeginTable( "##vec", 3, 0, { widgetWidth - paddingX, 0.f } );
    ImGui::TableNextRow();

    float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
    ImVec2 buttonSize = { lineHeight + 3.f, lineHeight };

    float dragWidths = ( widgetWidth - ( buttonSize.x * 3 ) ) / 3.f;

    Vector3 tempVec = Vector;
    {
        ImGui::TableSetColumnIndex( 0 );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 1.f, 51.f / 255.f, 82.f / 255.f, .66f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 1.f, 51.f / 255.f, 82.f / 255.f, 1.f ) );
        if( ImGui::Button( "X", buttonSize ) )
        {
            Vector.x = ResetValue;
        }
        ImGui::PopStyleColor( 2 );
        ImGui::SameLine();
        ImGui::PushItemWidth( dragWidths );
        ImGui::DragFloat( "##X", &Vector.x, 0.1f );
        ImGui::PopItemWidth();
    }

    {
        ImGui::TableSetColumnIndex( 1 );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 139.f / 255.f, 220.f / 255.f, 0.f, .66f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 139.f / 255.f, 220.f / 255.f, 0.f, 1.f ) );
        if( ImGui::Button( "Y", buttonSize ) )
        {
            Vector.y = ResetValue;
        }
        ImGui::PopStyleColor( 2 );
        ImGui::SameLine();
        ImGui::PushItemWidth( dragWidths );
        ImGui::DragFloat( "##Y", &Vector.y, 0.1f );
        ImGui::PopItemWidth();
    }

    {
        ImGui::TableSetColumnIndex( 2 );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 40.f / 255.f, 143.f / 255.f, 253.f / 255.f, .66f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 40.f / 255.f, 143.f / 255.f, 253.f / 255.f, 1.f ) );
        if( ImGui::Button( "Z", buttonSize ) )
        {
            Vector.z = ResetValue;
        }
        ImGui::PopStyleColor( 2 );
        ImGui::SameLine();
        ImGui::PushItemWidth( dragWidths );
        ImGui::DragFloat( "##Z", &Vector.z, 0.1f );
        ImGui::PopItemWidth();
    }

    ImGui::EndTable();

    ImGui::PopStyleVar( 2 );

    //ImGui::PopID();
    ImGui::EndHorizontal();

    return tempVec != Vector;
}

bool HavanaUtils::EditableVector( const std::string& Name, Vector2& Vector, float ResetValue /*= 0.f*/, float customWidth )
{
    ImGui::PushID( Name.c_str() );

    float widgetSize = Label( Name, customWidth );

    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.f, 0.f } );
    ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, { 2.f, 2.f } );

    ImGui::BeginTable( "##vec", 2, 0, { widgetSize, 0.f } );
    ImGui::TableNextRow();

    float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
    ImVec2 buttonSize = { lineHeight + 3.f, lineHeight };

    Vector2 tempVec = Vector;
    {
        ImGui::TableSetColumnIndex( 0 );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 1.f, 51.f / 255.f, 82.f / 255.f, .66f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 1.f, 51.f / 255.f, 82.f / 255.f, 1.f ) );
        if( ImGui::Button( "X", buttonSize ) )
        {
            Vector.x = ResetValue;
        }
        ImGui::PopStyleColor( 2 );
        ImGui::SameLine();
        ImGui::PushItemWidth( -1 );
        ImGui::DragFloat( "##X", &Vector.x, 0.1f );
        ImGui::PopItemWidth();
    }

    {
        ImGui::TableSetColumnIndex( 1 );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 139.f / 255.f, 220.f / 255.f, 0.f, .66f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 139.f / 255.f, 220.f / 255.f, 0.f, 1.f ) );
        if( ImGui::Button( "Y", buttonSize ) )
        {
            Vector.y = ResetValue;
        }
        ImGui::PopStyleColor( 2 );
        ImGui::SameLine();
        ImGui::PushItemWidth( -1 );
        ImGui::DragFloat( "##Y", &Vector.y, 0.1f );
        ImGui::PopItemWidth();
    }

    ImGui::EndTable();

    ImGui::PopStyleVar( 2 );

    ImGui::PopID();

    return tempVec != Vector;
}

bool HavanaUtils::Float( const std::string& Name, float& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

    bool res = ImGui::DragFloat( "##float", &value );

    ImGui::PopID();

    return res;
}

bool HavanaUtils::Double( const std::string& Name, double& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

    bool res = ImGui::DragScalar( "##float", ImGuiDataType_Double, &value );

    ImGui::PopID();

    return res;
}

bool HavanaUtils::Int( const std::string& Name, int32_t& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

    bool res = ImGui::DragScalar( "##int", ImGuiDataType_S32, &value );

    ImGui::PopID();

    return res;
}

bool HavanaUtils::Int( const std::string& Name, int16_t& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

    bool res = ImGui::DragScalar( "##int", ImGuiDataType_S16, &value );

    ImGui::PopID();

    return res;
}

bool HavanaUtils::Int( const std::string& Name, int64_t& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

    bool res = ImGui::DragScalar( "##int", ImGuiDataType_S64, &value );

    ImGui::PopID();

    return res;
}

bool HavanaUtils::UInt( const std::string& Name, uint32_t& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

    bool res = ImGui::DragScalar( "##uint", ImGuiDataType_U32, &value );

    ImGui::PopID();

    return res;
}

bool HavanaUtils::UInt( const std::string& Name, uint16_t& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

    bool res = ImGui::DragScalar( "##uint", ImGuiDataType_U16, &value );

    ImGui::PopID();

    return res;
}

bool HavanaUtils::UInt( const std::string& Name, uint64_t& value )
{
    ImGui::PushID( Name.c_str() );
    float size = Label( Name );

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
    //ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    //open_popup |= ImGui::Button("Palette");
    if( open_popup )
    {
        ImGui::OpenPopup( "mypicker" );
        backup_color = color;
    }
    if( ImGui::BeginPopup( "mypicker" ) )
    {
        ImGui::Text( "\n" );
        ImGui::Separator();
        ImGui::ColorPicker4( "##picker", (float*)&color, 0 | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoAlpha );
        ImGui::SameLine();

        ImGui::BeginGroup(); // Lock X position
        ImGui::Text( "Current" );
        ImGui::ColorButton( "##current", color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoAlpha, ImVec2( 60, 40 ) );
        ImGui::Text( "Previous" );
        if( ImGui::ColorButton( "##previous", backup_color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoAlpha, ImVec2( 60, 40 ) ) )
            color = backup_color;
        ImGui::Separator();
        ImGui::Text( "Palette" );
        for( int n = 0; n < IM_ARRAYSIZE( saved_palette ); n++ )
        {
            ImGui::PushID( n );
            if( ( n % 8 ) != 0 )
                ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemSpacing.y );

            ImGuiColorEditFlags palette_button_flags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha;
            if( ImGui::ColorButton( "##palette", saved_palette[n], palette_button_flags, ImVec2( 20, 20 ) ) )
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
        ImGui::Text( "\n" );
        ImGui::EndPopup();
    }
    ImGui::PopID();

    arr.x = color.x;
    arr.y = color.y;
    arr.z = color.z;
}

