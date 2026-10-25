#pragma once
#include "RenderCommands.h"
#include "bgfx/bgfx.h"

namespace Moonlight
{
    struct ScreenPosVertex
    {
        Vector2 Position;

        static void init()
        {
            ms_layout
                .begin()
                .add( bgfx::Attrib::Position, 2, bgfx::AttribType::Float )
                .end();
        }

        static bgfx::VertexLayout ms_layout;
    };

    struct PosColorVertex
    {
        Vector3 vec;
        uint32_t m_abgr;

        static void Init()
        {
            ms_layout
                .begin()
                .add( bgfx::Attrib::Position, 3, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
                .end();
        };

        static bgfx::VertexLayout ms_layout;
    };

    struct PosTexCoordVertex
    {
        Vector3 vec;
        Vector2 coords;

        static void Init()
        {
            ms_layout
                .begin()
                .add( bgfx::Attrib::Position, 3, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
                .end();
        };

        static bgfx::VertexLayout ms_layout;
    };

    struct PosNormTexTanBiVertex
    {
        Vector3 Position;
        Vector3 Normal;
        Vector2 TextureCoord;
        Vector3 Tangent;
        Vector3 BiTangent;

        static void Init()
        {
            ms_layout
                .begin()
                .add( bgfx::Attrib::Position, 3, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Normal, 3, bgfx::AttribType::Float, true )
                .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Tangent, 3, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Float )
                .end();
        };

        static bgfx::VertexLayout ms_layout;
    };

    // Skinning stream (vertex buffer 1 of skinned meshes): up to four bone influences.
    struct SkinWeightsVertex
    {
        uint8_t Indices[4] = { 0, 0, 0, 0 };
        float Weights[4] = { 0.f, 0.f, 0.f, 0.f };

        static void Init()
        {
            ms_layout
                .begin()
                // Normalized (UNORM is supported everywhere, unlike USCALED): the shader scales by 255.
                .add( bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8, true, false )
                .add( bgfx::Attrib::Weight, 4, bgfx::AttribType::Float )
                .end();
        };

        static bgfx::VertexLayout ms_layout;
    };

    // Wind stream (vertex buffer 1 of generated trees, read as a_color0 by the foliage shaders):
    // R leaf flutter, G branch phase, B branch sway, A baked crown occlusion.
    struct WindVertex
    {
        uint8_t Data[4] = { 0, 0, 0, 255 };

        static void Init()
        {
            ms_layout
                .begin()
                .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
                .end();
        };

        static bgfx::VertexLayout ms_layout;
    };

    struct ShaderProgram
    {
        float test;
        /*Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;*/
    };
}
