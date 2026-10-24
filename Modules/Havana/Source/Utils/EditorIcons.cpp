#include "EditorIcons.h"

#if USING( ME_EDITOR )
#include "ImGuiTheme.h"
#include "Path.h"
#include "Utils/ImGuiUtils.h"
#include "imgui_internal.h"
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

namespace
{
    std::map<std::string, bgfx::TextureHandle> s_icons;

    // Spaces as wide as InSize, reserving a menu's icon column (the icon is drawn over them).
    std::string IconPlaceholder( float InSize )
    {
        std::string placeholder = " ";
        while( ImGui::CalcTextSize( placeholder.c_str() ).x < InSize ) placeholder += ' ';
        return placeholder;
    }
}


bgfx::TextureHandle EditorIcons::Get( const std::string& InName, float InPixels )
{
    const int pixels = std::clamp( static_cast<int>( std::lround( InPixels ) ), 8, 256 );
    const std::string key = InName + "@" + std::to_string( pixels );
    auto found = s_icons.find( key );
    if( found != s_icons.end() ) return found->second;

    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    std::ifstream file( Path( "Assets/Icons/outline/" + InName + ".svg" ).FullPath );
    if( file )
    {
        std::string svg( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
        for( size_t pos = 0; ( pos = svg.find( "currentColor", pos ) ) != std::string::npos; pos += 7 ) svg.replace( pos, 12, "#ffffff" );
        static bx::DefaultAllocator allocator;
        if( bimg::ImageContainer* image = bimg::imageParseSvg( &allocator, svg.data(), static_cast<uint32_t>( svg.size() ), static_cast<uint32_t>( pixels ), static_cast<uint32_t>( pixels ) ) )
        {
            handle = bgfx::createTexture2D( static_cast<uint16_t>( image->m_width ), static_cast<uint16_t>( image->m_height ), false, 1,
                bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy( image->m_data, image->m_size ) );
            bimg::imageFree( image );
        }
    }
    s_icons.emplace( key, handle );
    return handle;
}


void EditorIcons::Draw( ImDrawList* InDrawList, const std::string& InName, const ImVec2& InMin, float InSize, ImU32 InColor )
{
    // Whole pixels, so the rasterized icon maps 1:1 onto the screen.
    const ImVec2 min( std::round( InMin.x ), std::round( InMin.y ) );
    const float size = std::round( InSize );
    const bgfx::TextureHandle texture = Get( InName, size );
    if( bgfx::isValid( texture ) )
    {
        InDrawList->AddImage( ImGui::toId( texture, IMGUI_FLAGS_ALPHA_BLEND, 0 ), min, ImVec2( min.x + size, min.y + size ), ImVec2( 0.f, 0.f ), ImVec2( 1.f, 1.f ), InColor );
    }
    else
    {
        InDrawList->AddText( ImVec2( min.x + ( size - ImGui::CalcTextSize( "?" ).x ) * 0.5f, min.y + ( size - ImGui::GetTextLineHeight() ) * 0.5f ), InColor, "?" );
    }
}


void EditorIcons::Image( const std::string& InName, float InSize, ImU32 InColor )
{
    const float size = InSize > 0.f ? InSize : ImGui::GetTextLineHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy( ImVec2( size, size ) );
    Draw( ImGui::GetWindowDrawList(), InName, min, size, InColor ? InColor : ImGui::GetColorU32( ImGuiCol_Text ) );
}


bool EditorIcons::Button( const char* InIcon, const char* InLabel, const char* InDescription, bool InEnabled, bool InChecked )
{
    ImGui::PushID( InLabel );
    ImGui::BeginDisabled( !InEnabled );
    if( InChecked )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_ButtonActive ) );
    }
    const ImVec2 size = ImGuiTheme::IconButtonSize();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button( "##icon", size );
    if( InChecked )
    {
        ImGui::PopStyleColor();
    }
    const float icon = ImGuiTheme::Em( 1.05f );
    Draw( ImGui::GetWindowDrawList(), InIcon, ImVec2( start.x + ( size.x - icon ) * 0.5f, start.y + ( size.y - icon ) * 0.5f ), icon, ImGui::GetColorU32( ImGuiCol_Text ) );
    ImGui::EndDisabled();
    if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
    {
        Tooltip( InLabel, InDescription );
    }
    ImGui::PopID();
    return clicked;
}


void EditorIcons::Planned( const char* InIcon, const char* InLabel, const char* InDescription )
{
    Button( InIcon, InLabel, ( std::string( "Planned: " ) + InDescription ).c_str(), false );
}


void EditorIcons::Tooltip( const char* InLabel, const char* InDescription )
{
    ImGui::BeginTooltip();
    ImGui::TextUnformatted( InLabel );
    if( InDescription && *InDescription )
    {
        ImGui::PushTextWrapPos( ImGuiTheme::Em( 22.f ) );
        ImGui::TextDisabled( "%s", InDescription );
        ImGui::PopTextWrapPos();
    }
    ImGui::EndTooltip();
}


bool EditorIcons::MenuItem( const char* InIcon, const char* InLabel, const char* InShortcut, bool InSelected, bool InEnabled )
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if( window->DC.LayoutType == ImGuiLayoutType_Horizontal ) return ImGui::MenuItem( InLabel, InShortcut, InSelected, InEnabled );
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 at( window->DC.CursorPos.x + window->DC.MenuColumns.OffsetIcon, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset );
    const bool pressed = ImGui::MenuItemEx( InLabel, IconPlaceholder( size ).c_str(), InShortcut, InSelected, InEnabled );
    if( ImGui::IsItemVisible() ) Draw( window->DrawList, InIcon, at, size, ImGui::GetColorU32( InEnabled ? ImGuiCol_Text : ImGuiCol_TextDisabled ) );
    return pressed;
}


bool EditorIcons::BeginMenu( const char* InIcon, const char* InLabel, bool InEnabled )
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if( window->DC.LayoutType == ImGuiLayoutType_Horizontal ) return ImGui::BeginMenu( InLabel, InEnabled );
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 at( window->DC.CursorPos.x + window->DC.MenuColumns.OffsetIcon, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset );
    const ImU32 color = ImGui::GetColorU32( InEnabled ? ImGuiCol_Text : ImGuiCol_TextDisabled );
    // Drawn into this menu, not the submenu BeginMenuEx leaves current when it opens.
    const bool open = ImGui::BeginMenuEx( InLabel, IconPlaceholder( size ).c_str(), InEnabled );
    Draw( window->DrawList, InIcon, at, size, color );
    return open;
}


void EditorIcons::Shutdown()
{
    for( const auto& [key, handle] : s_icons )
    {
        if( bgfx::isValid( handle ) ) bgfx::destroy( handle );
    }
    s_icons.clear();
}
#endif
