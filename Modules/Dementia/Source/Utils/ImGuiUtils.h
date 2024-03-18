#pragma once
#include <bgfx/bgfx.h>
#include <imgui.h>
#include "Core/Assert.h"

namespace ImGui
{
#define IMGUI_FLAGS_NONE        UINT8_C(0x00)
#define IMGUI_FLAGS_ALPHA_BLEND UINT8_C(0x01)

    inline ImTextureID toId( bgfx::TextureHandle _handle, uint8_t _flags, uint8_t _mip )
    {
        union
        {
            struct
            {
                bgfx::TextureHandle handle; uint8_t flags; uint8_t mip;
            } s; ImTextureID id;
        } tex;
        tex.s.handle = _handle;
        tex.s.flags = _flags;
        tex.s.mip = _mip;
        return tex.id;
    }

    // Helper function for passing bgfx::TextureHandle to ImGui::Image.
    inline void Image( bgfx::TextureHandle _handle
        , uint8_t _flags
        , uint8_t _mip
        , const ImVec2& _size
        , const ImVec2& _uv0 = ImVec2( 0.0f, 0.0f )
        , const ImVec2& _uv1 = ImVec2( 1.0f, 1.0f )
        , const ImVec4& _tintCol = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f )
        , const ImVec4& _borderCol = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f )
    )
    {
        Image( toId( _handle, _flags, _mip ), _size, _uv0, _uv1, _tintCol, _borderCol );
    }

    // Helper function for passing bgfx::TextureHandle to ImGui::Image.
    inline void Image( bgfx::TextureHandle _handle
        , const ImVec2& _size
        , const ImVec2& _uv0 = ImVec2( 0.0f, 0.0f )
        , const ImVec2& _uv1 = ImVec2( 1.0f, 1.0f )
        , const ImVec4& _tintCol = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f )
        , const ImVec4& _borderCol = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f )
    )
    {
        Image( _handle, IMGUI_FLAGS_ALPHA_BLEND, 0, _size, _uv0, _uv1, _tintCol, _borderCol );
    }

    // Helper function for passing bgfx::TextureHandle to ImGui::ImageButton.
    inline bool ImageButton( bgfx::TextureHandle _handle
        , uint8_t _flags
        , uint8_t _mip
        , const ImVec2& _size
        , const ImVec2& _uv0 = ImVec2( 0.0f, 0.0f )
        , const ImVec2& _uv1 = ImVec2( 1.0f, 1.0f )
        , int _framePadding = -1
        , const ImVec4& _bgCol = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f )
        , const ImVec4& _tintCol = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f )
    )
    {
        return ImageButton( toId( _handle, _flags, _mip ), _size, _uv0, _uv1, _framePadding, _bgCol, _tintCol );
    }

    // Helper function for passing bgfx::TextureHandle to ImGui::ImageButton.
    inline bool ImageButton( bgfx::TextureHandle _handle
        , const ImVec2& _size
        , const ImVec2& _uv0 = ImVec2( 0.0f, 0.0f )
        , const ImVec2& _uv1 = ImVec2( 1.0f, 1.0f )
        , int _framePadding = -1
        , const ImVec4& _bgCol = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f )
        , const ImVec4& _tintCol = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f )
    )
    {
        return ImageButton( _handle, IMGUI_FLAGS_ALPHA_BLEND, 0, _size, _uv0, _uv1, _framePadding, _bgCol, _tintCol );
    }

    inline void NextLine()
    {
        SetCursorPosY( GetCursorPosY() + GetTextLineHeightWithSpacing() );
    }

    inline bool MouseOverArea()
    {
        return false
            || ImGui::IsAnyItemActive()
            || ImGui::IsAnyItemHovered()
            || ImGui::IsWindowHovered( ImGuiHoveredFlags_AnyWindow )
            //			|| ImGuizmo::IsOver()
            ;
    }

} // namespace ImGui