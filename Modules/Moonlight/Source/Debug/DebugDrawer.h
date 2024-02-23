#pragma once
#include <memory>
#include <bgfx/bgfx.h>

struct DebugVertex
{
    float m_x;
    float m_y;
    float m_z;
    float m_len;
    uint32_t m_abgr;

    static void init()
    {
        ms_layout
            .begin()
            .add( bgfx::Attrib::Position, 3, bgfx::AttribType::Float )
            .add( bgfx::Attrib::TexCoord0, 1, bgfx::AttribType::Float )
            .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
            .end();
    }

    static bgfx::VertexLayout ms_layout;
};


class DebugDrawer
{
public:
    enum Program
    {
        Lines = 0,
        LinesStipple,
        Fill,
        FillMesh,
        FillLit,
        FillLitMesh,
        FillTexture,

        Count
    };

    struct DebugMesh
    {
        enum Enum
        {
            Sphere0,
            Sphere1,
            Sphere2,
            Sphere3,

            Cone0,
            Cone1,
            Cone2,
            Cone3,

            Cylinder0,
            Cylinder1,
            Cylinder2,
            Cylinder3,

            Capsule0,
            Capsule1,
            Capsule2,
            Capsule3,

            Quad,

            Cube,

            Count,

            SphereMaxLod = Sphere3 - Sphere0,
            ConeMaxLod = Cone3 - Cone0,
            CylinderMaxLod = Cylinder3 - Cylinder0,
            CapsuleMaxLod = Capsule3 - Capsule0,
        };

        uint32_t m_startVertex;
        uint32_t m_numVertices;
        uint32_t m_startIndex[2];
        uint32_t m_numIndices[2];
    };

    struct State
    {
        enum Enum
        {
            None,
            MoveTo,
            LineTo,

            Count
        };
    };

    struct Attrib
    {
        uint64_t m_state;
        float    m_offset;
        float    m_scale;
        float    m_spin;
        uint32_t m_abgr;
        bool     m_stipple;
        bool     m_wireframe;
        uint8_t  m_lod;
    };

    DebugDrawer();
    ~DebugDrawer();

    void Init( bgfx::Encoder* InEncoder );
    void Begin( uint16_t InViewId, bool InDepthTestLess = true );

    void Push();
    void Pop();

    void Flush();
    void SoftFlush();

    void End();

    void Draw( const float* InOBB );

private:
    static const uint32_t kStackSize = 16;
    static const uint32_t kCacheSize = 1024;
    DebugVertex   m_cache[kCacheSize + 1];
    struct MatrixStack
    {
        void reset()
        {
            mtx = 0;
            num = 1;
            data = NULL;
        }

        uint32_t mtx;
        uint16_t num;
        float* data;
    };

    State::Enum m_state;
    Attrib m_attrib[kStackSize];
    uint16_t m_indices[kCacheSize * 2];
    uint16_t m_pos;
    uint16_t m_posQuad;
    uint16_t m_indexPos;
    uint16_t m_vertexPos;
    uint32_t m_mtxStackCurrent;
    MatrixStack m_mtxStack[32];

    bgfx::ViewId m_viewId;
    uint8_t m_stack;
    bool    m_depthTestLess;

    bgfx::ProgramHandle m_program[Program::Count];
    bgfx::UniformHandle u_params;
    bgfx::UniformHandle s_texColor;

    DebugMesh m_mesh[DebugMesh::Count];
    bgfx::VertexBufferHandle m_vbh;
    bgfx::IndexBufferHandle  m_ibh;

    bgfx::Encoder* m_defaultEncoder;


    void PushTransform( const void* _mtx, uint16_t _num, bool _flush = true );
    void PopTransform( bool _flush = true );
    void SetTransform( const void* _mtx, uint16_t _num = 1, bool _flush = true );

    void moveTo( float _x, float _y, float _z = 0.0f );
    void lineTo( float _x, float _y, float _z = 0.0f );
    void close();
};