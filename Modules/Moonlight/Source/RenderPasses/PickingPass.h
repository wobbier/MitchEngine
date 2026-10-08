#pragma once
#include "IPass.h"
#include "bgfx/bgfx.h"
#include "Math/Vector4.h"
#include <unordered_map>
#include "Core/FrameRenderData.h"

#include <RenderViews.h>

#define RENDER_PASS_ID      Moonlight::RenderView::PickingId    // ID buffer for picking
#define RENDER_PASS_BLIT    Moonlight::RenderView::PickingBlit  // Blit GPU render target to CPU texture

#define ID_DIM 32  // Size of the ID buffer

namespace Moonlight { struct CameraData; }
class BGFXRenderer;

namespace Moonlight
{
    class PickingPass
        : public IPass
    {
    public:
        PickingPass();
        ~PickingPass();

        void Render( BGFXRenderer* inRenderer, CameraData* inCamData, FrameRenderData& inFrameSettings );

        virtual bool IsSupported() final;

        uint32_t m_width = 0;
        uint32_t m_height = 0;

        // Resource handles
        bgfx::ProgramHandle m_idProgram;
        bgfx::UniformHandle u_id;
        bgfx::TextureHandle m_pickingRT;
        bgfx::TextureHandle m_pickingRTDepth;
        bgfx::TextureHandle m_blitTex;
        bgfx::FrameBufferHandle m_pickingFB;
        std::unordered_map<uint32_t, uint64_t> m_idsToEnt;

        uint8_t m_blitData[ID_DIM * ID_DIM * 4]; // Read blit into this

        uint32_t m_reading = 0;

        bool ForceDraw = false;

        float m_fov = 1.f;
    };
}