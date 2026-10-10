#pragma once
#include "bgfx/bgfx.h"
#include "bx/bx.h"
#include "Math/Vector2.h"
#include <vector>

namespace Moonlight
{
    // A camera's render targets.
    //   SceneBuffer: HDR colour (RGBA16F) + sampleable depth; the scene renders here.
    //   PostBuffer:  LDR intermediate between post passes (tonemap output before FXAA).
    //   Buffer:      the final LDR image (Texture), what views, editor panels and render-to-texture
    //                users sample. Shares DepthTexture with the scene so late overlays depth-test.
    //   Bloom chain: half-resolution and smaller RGBA16F mips.
    struct FrameBuffer
    {
        FrameBuffer( uint32_t width, uint32_t height );
        ~FrameBuffer();

        void Resize( Vector2 newSize );
        void ReCreate( uint32_t resetFlags );

        bgfx::FrameBufferHandle Buffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle Texture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle DepthTexture = BGFX_INVALID_HANDLE;

        bgfx::FrameBufferHandle SceneBuffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle SceneColor = BGFX_INVALID_HANDLE;

        bgfx::FrameBufferHandle PostBuffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle PostColor = BGFX_INVALID_HANDLE;

        struct Mip
        {
            bgfx::FrameBufferHandle Buffer = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle Color = BGFX_INVALID_HANDLE;
            uint16_t Width = 0;
            uint16_t Height = 0;
        };
        std::vector<Mip> BloomMips;

        // Half-resolution ambient occlusion (raw and blurred).
        Mip AOBuffer;
        Mip AOBlurBuffer;

        // Half-resolution volumetric fog (raymarched and blurred): rgb in-scattering, a transmittance.
        Mip FogBuffer;
        Mip FogBlurBuffer;

        // Auto exposure: this frame's average luminance and the adapted value (ping-pong).
        Mip LuminanceBuffer;
        Mip AdaptedLuminance[2];
        uint32_t AdaptedIndex = 0;

        // Temporal anti-aliasing (created on first use): the resolved HDR colour, ping-ponged with
        // the history it was resolved against.
        Mip TemporalHistory[2];
        uint32_t TemporalIndex = 0;         // the target written last
        bool TemporalValid = false;         // TemporalHistory[TemporalIndex] holds last frame's resolve
        uint32_t TemporalFrame = 0;         // position in the jitter sequence
        float TemporalJitter[2] = { 0.f, 0.f };             // this frame's jitter, in NDC
        float PreviousViewProjection[16] = {};              // last frame's (unjittered)
        float ViewProjection[16] = {};                      // this frame's (unjittered)
        void EnsureTemporalTargets();

        // This frame's HDR image for bloom, exposure and tonemapping: the temporal resolve, or
        // SceneColor.
        bgfx::TextureHandle PostInput = BGFX_INVALID_HANDLE;

        // HDR colour without the depth attachment: soft particles read the depth as a texture.
        bgfx::FrameBufferHandle ParticleBuffer = BGFX_INVALID_HANDLE;

        // Clustered lighting data for this camera (updated every frame).
        bgfx::TextureHandle ClusterGrid = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle ClusterIndices = BGFX_INVALID_HANDLE;

        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t m_resetFlags = 0;
        bool MatchMainBufferSize = false;

        static bgfx::TextureFormat::Enum GetDepthFormat();

    private:
        void Release();
    };
}
