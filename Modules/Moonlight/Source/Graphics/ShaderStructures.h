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

    struct ShaderProgram
    {
        float test;
        /*Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;*/
    };

    // Ultralight
    struct Vertex_2f_4ub_2f
    {
        Vector2 pos;
        uint8_t color[4];
        Vector2 objCoord;

        static void Init()
        {
            ms_layout
                .begin()
                .add( bgfx::Attrib::Position, 2, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
                .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
                .end();
        };

        static bgfx::VertexLayout ms_layout;
    };

    struct Vertex_2f_4ub_2f_2f_28f
    {
        Vector2 pos;
        uint8_t color[4];
        Vector2 texCoord;
        Vector2 objCoord;
        float data_0[4];
        float data_1[4];
        float data_2[4];
        float data_3[4];
        float data_4[4];
        float data_5[4];
        float data_6[4];

        static void Init()
        {
            ms_layout
                .begin()
                .add( bgfx::Attrib::Position, 2, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
                .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float )

                .add( bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Color1, 4, bgfx::AttribType::Float )
                .end();
        };

        static bgfx::VertexLayout ms_layout;
    };
}
