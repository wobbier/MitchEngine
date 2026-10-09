#pragma once
#include <bgfx/bgfx.h>
#include <array>
#include <cstdint>
#include <vector>

namespace Moonlight
{
    class ViewAllocator;
    class DynamicSky;
    struct CameraData;

    // Image-based lighting from what a camera shows behind the scene: its clear colour, its skybox
    // panorama or the procedural sky. Each distinct source gets a probe: the environment captured into
    // a cube, GGX-prefiltered into a specular mip chain (roughness = mip / (kSpecularMips - 1)) and
    // convolved into diffuse irradiance, plus one shared split-sum BRDF table. Probes refresh when their
    // source changes (e.g. the sky's sun moves), spread over two frames so a refresh never costs more
    // than a few dozen tiny passes in one frame.
    class EnvironmentLighting
    {
    public:
        static constexpr uint16_t kCaptureSize = 128;
        static constexpr uint16_t kIrradianceSize = 32;
        static constexpr uint8_t kSpecularMips = 5;    // 128 .. 8
        static constexpr uint8_t kSourceLevels = 4;    // 128, 64, 32, 16 (separate cubes: no read/write hazards)
        static constexpr size_t kProbeCount = 3;

        struct Probe
        {
            bgfx::TextureHandle Source[kSourceLevels] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
            bgfx::TextureHandle Specular = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle Irradiance = BGFX_INVALID_HANDLE;
            uint64_t Key = 0;
            int SourceType = -1;
            uint64_t LastUsedFrame = 0;
            bool Captured = false;   // captured, waiting to be filtered
            bool Ready = false;      // filtered at least once
        };

        void Init();
        void Destroy();

        // The probe lighting InCamera this frame (refreshing it if its source changed), or nullptr until
        // one is ready.
        const Probe* Prepare( ViewAllocator& InViews, const CameraData& InCamera, DynamicSky* InSky, uint64_t InFrame );

        bgfx::TextureHandle GetBrdfLut() const { return m_brdfLut; }
        bgfx::TextureHandle GetFallbackCube() const { return m_fallbackCube; }
        bool IsBrdfLutReady() const { return m_brdfReady; }

        float Intensity = 1.f;

    private:
        enum SourceType : int
        {
            ColorSource = 0,
            PanoramaSource,
            ProceduralSource
        };

        struct SourceDesc
        {
            SourceType Type = ColorSource;
            float Color[4] = { 0.f, 0.f, 0.f, 1.f };
            bgfx::TextureHandle Panorama = BGFX_INVALID_HANDLE;
            uint64_t Key = 0;
        };

        SourceDesc DescribeSource( const CameraData& InCamera, DynamicSky* InSky ) const;
        void CreateProbe( Probe& OutProbe );
        void DestroyProbe( Probe& InProbe );
        bool Capture( Probe& InProbe, const SourceDesc& InSource, ViewAllocator& InViews, DynamicSky* InSky );
        bool Filter( Probe& InProbe, ViewAllocator& InViews );
        bool RenderBrdfLut( ViewAllocator& InViews );
        bgfx::ViewId BeginPass( ViewAllocator& InViews, const char* InName, bgfx::TextureHandle InTarget, uint16_t InLayer, uint16_t InMip, uint16_t InSize );
        void Submit( bgfx::ViewId InView, bgfx::ProgramHandle InProgram );
        void ReleaseFrameBuffers();
        bool HasFrameBufferHeadroom( uint32_t InNeeded ) const;

        std::array<Probe, kProbeCount> m_probes;
        std::vector<bgfx::FrameBufferHandle> m_frameBuffers;
        uint64_t m_lastUpdateFrame = UINT64_MAX;

        bgfx::ProgramHandle m_captureProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_downsampleProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_prefilterProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_irradianceProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_brdfProgram = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_envFace = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_envSource = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_envColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_envSource = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_envPanorama = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_brdfLut = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_fallbackCube = BGFX_INVALID_HANDLE;
        bool m_brdfReady = false;
        bool m_supported = false;
    };
}
