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
    //   Buffer:      the final LDR image (Texture), what views and editor panels sample. Shares
    //                DepthTexture with the scene so late overlays depth-test. Render texture cameras
    //                have none: their final image goes into the texture asset (OwnsOutput false).
    //   Bloom chain: half-resolution and smaller RGBA16F mips.
    // Targets only some cameras use (bloom, ambient occlusion, volumetric fog, temporal histories,
    // depth of field...) are created on first use: bgfx has a fixed number of framebuffers.
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
        void EnsureBloomTargets();

        // Half-resolution ambient occlusion (raw and blurred).
        Mip AOBuffer;
        Mip AOBlurBuffer;
        void EnsureAmbientOcclusionTargets();

        // Half-resolution volumetric fog (raymarched and blurred): rgb in-scattering, a transmittance.
        Mip FogBuffer;
        Mip FogBlurBuffer;
        void EnsureVolumetricFogTargets();
        // Volumetric fog history (created on first use): the march accumulated over frames,
        // ping-ponged with the history it was resolved against (PostProcess::RenderFog).
        Mip FogHistory[2];
        uint32_t FogHistoryIndex = 0;       // the target written last
        bool FogHistoryValid = false;       // FogHistory[FogHistoryIndex] holds last frame's resolve
        uint32_t FogFrame = 0;              // position in the march jitter sequence
        float FogPreviousViewProjection[16] = {};   // last frame's (unjittered), kept by the fog pass
        float FogPreviousOrigin[3] = {};            // last frame's camera position and forward axis
        float FogPreviousForward[3] = {};
        void EnsureFogHistoryTargets();

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

        // Depth of field, allocated only when used. Full-resolution signed CoC and output;
        // half-resolution colour/CoC, conservative foreground radius, near/far gathers.
        Mip DoFCoC;
        Mip DoFPrefilter;
        Mip DoFRadius[2];
        Mip DoFNear;
        Mip DoFFar;
        Mip DoFOutput;
        Mip DoFSplitOutput; // allocated only for the second split-diopter lens
        void EnsureDepthOfFieldTargets( bool InSplit = false );

        // Procedural sky clouds (created on first use): half resolution, rgb in-scattering and a
        // transmittance, accumulated over frames (ping-pong with the history they blend).
        Mip CloudHistory[2];
        uint32_t CloudIndex = 0;            // the target written last
        bool CloudValid = false;            // CloudHistory[CloudIndex] holds last frame's clouds
        uint64_t CloudSkyKey = 0;           // reset history when time, orbit, weather or eclipse changes
        uint32_t CloudFrame = 0;            // position in the sub-pixel jitter sequence
        float CloudPreviousBasis[9] = {};   // last frame's right / tan, up / tan, forward (see DynamicSky)
        void EnsureCloudTargets();
        // Shader graph post-process passes (created on first use): ping-pong targets for passes on
        // the HDR scene (RGBA16F) and on the display image (RGBA8).
        Mip GraphHdr[2];
        Mip GraphLdr[2];
        void EnsureGraphTargets( bool InHdr, bool InLdr );
        // Optional filtered graph input, separate from the original scene colour.
        Mip GraphSceneMips;
        void EnsureGraphSceneMips();

        // This frame's HDR image for bloom, exposure and tonemapping: SceneColor, optionally
        // replaced by the temporal resolve, HDR shader graphs and depth of field.
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
        // Whether ReCreate makes the final image (Texture, Buffer): off for render texture cameras.
        bool OwnsOutput = true;

        static bgfx::TextureFormat::Enum GetDepthFormat();

    private:
        void Release();
    };
}
