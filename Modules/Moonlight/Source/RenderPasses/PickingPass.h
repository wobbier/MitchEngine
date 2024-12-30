#pragma once
#include "IPass.h"
#include "bgfx/bgfx.h"
#include "Math/Random.h"
#include "Math/Vector4.h"
#include <unordered_map>

//#TODO: better view ids
#define RENDER_PASS_SHADING 100  // Default forward rendered geo with simple shading
#define RENDER_PASS_ID      101  // ID buffer for picking
#define RENDER_PASS_BLIT    102  // Blit GPU render target to CPU texture

#define ID_DIM 32  // Size of the ID buffer

namespace Moonlight { struct CameraData; }
class BGFXRenderer;
class Mesh;

namespace Moonlight
{
    class PickingPass
        : public IPass
    {
    public:
        PickingPass();

        void Render( BGFXRenderer* inRenderer, CameraData* inCamData );

        void RenderDebugMenu();
        virtual bool IsSupported() final;

        Random64 m_random;
        uint32_t m_width;
        uint32_t m_height;
        uint32_t m_debug;
        uint32_t m_reset;
        int64_t m_timeOffset;

        Mesh* m_meshes[12];
        float m_meshScale[12];
        float m_idsF[1000][4];
        uint32_t m_idsU[1000];
        uint32_t m_highlighted;

        // Resource handles
        bgfx::ProgramHandle m_shadingProgram;
        bgfx::ProgramHandle m_idProgram;
        bgfx::UniformHandle u_tint;
        bgfx::UniformHandle u_id;
        bgfx::TextureHandle m_pickingRT;
        bgfx::TextureHandle m_pickingRTDepth;
        bgfx::TextureHandle m_blitTex;
        bgfx::FrameBufferHandle m_pickingFB;
        std::unordered_map<uint32_t, uint64_t> m_idsToEnt;

        uint8_t m_blitData[ID_DIM * ID_DIM * 4]; // Read blit into this

        uint32_t m_reading;
        uint32_t m_currFrame;

        float m_fov;
        bool  m_cameraSpin;
    };
}