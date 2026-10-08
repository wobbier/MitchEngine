#pragma once

#include <bgfx/bgfx.h>
#include <Graphics/ShaderStructures.h>
#include "Graphics/MeshData.h"

namespace Moonlight
{
    static Moonlight::PosColorVertex s_cubeVertices[] =
    {
        {{-1.0f,  1.0f,  1.0f}, 0xff000000 },
        {{ 1.0f,  1.0f,  1.0f}, 0xff0000ff },
        {{-1.0f, -1.0f,  1.0f}, 0xff00ff00 },
        {{ 1.0f, -1.0f,  1.0f}, 0xff00ffff },
        {{-1.0f,  1.0f, -1.0f}, 0xffff0000 },
        {{ 1.0f,  1.0f, -1.0f}, 0xffff00ff },
        {{-1.0f, -1.0f, -1.0f}, 0xffffff00 },
        {{ 1.0f, -1.0f, -1.0f}, 0xffffffff },
    };

    static const uint16_t s_cubeTriList[] =
    {
        0, 1, 2, // 0
        1, 3, 2,
        4, 6, 5, // 2
        5, 6, 7,
        0, 2, 4, // 4
        4, 2, 6,
        1, 5, 3, // 6
        5, 7, 3,
        0, 4, 1, // 8
        4, 5, 1,
        2, 3, 6, // 10
        6, 3, 7,
    };

    // Unit cube (-1..1) with per-face normals, tangents and UVs so it lights and normal-maps correctly.
    // Triangles wind so cross(v1 - v0, v2 - v0) points out of the face (the engine's front-face convention).
    class CubeMesh
        : public Moonlight::MeshData
    {
    public:
        CubeMesh()
            : Moonlight::MeshData()
        {
            struct Face { Vector3 Normal; Vector3 Tangent; };
            const Face faces[] = {
                { {  1.f,  0.f,  0.f }, {  0.f,  0.f,  1.f } },
                { { -1.f,  0.f,  0.f }, {  0.f,  0.f, -1.f } },
                { {  0.f,  1.f,  0.f }, {  1.f,  0.f,  0.f } },
                { {  0.f, -1.f,  0.f }, {  1.f,  0.f,  0.f } },
                { {  0.f,  0.f,  1.f }, { -1.f,  0.f,  0.f } },
                { {  0.f,  0.f, -1.f }, {  1.f,  0.f,  0.f } },
            };

            for( const Face& face : faces )
            {
                const Vector3 bitangent = face.Normal.Cross( face.Tangent );
                const Vector3 corners[4] = {
                    face.Normal - face.Tangent - bitangent,
                    face.Normal + face.Tangent - bitangent,
                    face.Normal + face.Tangent + bitangent,
                    face.Normal - face.Tangent + bitangent,
                };
                const Vector2 uvs[4] = { { 0.f, 1.f }, { 1.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f } };

                const uint32_t base = static_cast<uint32_t>( Vertices.size() );
                for( int i = 0; i < 4; ++i )
                {
                    Moonlight::PosNormTexTanBiVertex vertex;
                    vertex.Position = corners[i];
                    vertex.Normal = face.Normal;
                    vertex.TextureCoord = uvs[i];
                    vertex.Tangent = face.Tangent;
                    vertex.BiTangent = bitangent;
                    Vertices.push_back( vertex );
                }

                const uint32_t faceIndices[6] = { 0, 1, 2, 0, 2, 3 };
                for( uint32_t index : faceIndices )
                {
                    Indices.push_back( base + index );
                }
            }

            InitMesh();
        }
    };

    static void screenSpaceQuad( float _textureWidth, float _textureHeight, float _texelHalf, bool _originBottomLeft, float _width = 1.0f, float _height = 1.0f )
    {
        if( 3 == bgfx::getAvailTransientVertexBuffer( 3, PosTexCoordVertex::ms_layout ) )
        {
            bgfx::TransientVertexBuffer vb;
            bgfx::allocTransientVertexBuffer( &vb, 3, PosTexCoordVertex::ms_layout );
            PosTexCoordVertex* vertex = (PosTexCoordVertex*)vb.data;

            const float minx = -_width;
            const float maxx = _width;
            const float miny = 0.0f;
            const float maxy = _height * 2.0f;

            const float texelHalfW = _texelHalf / _textureWidth;
            const float texelHalfH = _texelHalf / _textureHeight;
            const float minu = -1.0f + texelHalfW;
            const float maxu = 1.0f + texelHalfW;

            const float zz = 0.0f;

            float minv = texelHalfH;
            float maxv = 2.0f + texelHalfH;

            if( _originBottomLeft )
            {
                float temp = minv;
                minv = maxv;
                maxv = temp;

                minv -= 1.0f;
                maxv -= 1.0f;
            }

            vertex[0].vec.x = minx;
            vertex[0].vec.y = miny;
            vertex[0].vec.z = zz;
            vertex[0].coords.x = minu;
            vertex[0].coords.y = minv;

            vertex[1].vec.x = maxx;
            vertex[1].vec.y = miny;
            vertex[1].vec.z = zz;
            vertex[1].coords.x = maxu;
            vertex[1].coords.y = minv;

            vertex[2].vec.x = maxx;
            vertex[2].vec.y = maxy;
            vertex[2].vec.z = zz;
            vertex[2].coords.x = maxu;
            vertex[2].coords.y = maxv;

            bgfx::setVertexBuffer( 0, &vb );
        }
    }

}
